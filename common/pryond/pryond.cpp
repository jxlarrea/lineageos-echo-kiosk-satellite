// A service for Kiosk Satellite that runs Amazon's wake word engine (Pryon)
// from files the user installs. The image ships none of them. This daemon
// only reports what it finds under /data/pryon and listens with it:
//
//   /data/pryon/lib/          Pryon's libraries (see libs[] in load_pryon)
//   /data/pryon/models/<name>/ one model per folder, with its pryon.manifest
//   /data/pryon/locales.txt   one line per locale: the locale, then the models
//                             it listens with, such as "en-US alexa-en-US"
//
// Amazon's models count STOP only right after the wake word, and only from
// 0.55, while a plain "stop" scores 0.20 to 0.41. Kiosk Satellite listens for
// the stop word on its own while a timer rings or a reply plays, so before a
// model loads the daemon builds a copy of it in /data/pryond: links to the
// user's files, with its own kw.cfg.json and op.cfg.json wherever a STOP
// spotter is. There STOP counts without the wake word and is accepted from
// 0.18, with near misses from 0.05. The user's files stay as they are.
//
// Nothing is loaded until a client asks to listen, so the daemon costs next
// to nothing while another engine runs. It looks at the files again on every
// connection, so a client reconnects to see what changed.
//
// Protocol, on the init socket pryond:
//
// The daemon speaks first, with one JSON line. available is true when every
// library is there and a model with a wake word has a locale. missing names the
// libraries that are not there or cannot be read. models lists every usable
// model, in the order locales.txt names them, with the locales that listen
// with it, the keywords it spots and the threshold each keyword is accepted
// at before any client property lowers it, with the stop word's from the
// daemon's copy. A keyword named STOP is a stop
// word, every other one a wake word. skipped names the models that cannot be
// used and why. Without every library, models is empty.
//
//   {"event":"hello","version":2,"available":true,"missing":[],
//    "locales":["en-US"],"models":[{"model":"alexa-en-US","locales":["en-US"],
//    "wake_words":["ALEXA"],"stop_words":["STOP"],
//    "accept_thresholds":{"ALEXA":0.923,"STOP":0.18}}],"skipped":[]}
//
// The client sends frames: a type byte, a little-endian 32-bit length and
// the payload.
//
//   'S' start listening with the comma separated models in the payload, or
//       with every model when it is empty. Answered with a "started" or an
//       "error" event.
//   'A' 16 kHz mono 16-bit little-endian audio.
//   'P' a client property, "name=value". Pryon lowers its thresholds while
//       AudioPlayerState, audio_playback or AlarmState is 1. Kept until it
//       is pushed again, and pushed again after every 'E'.
//   'E' a gap in the audio.
//
// Every keyword Pryon spots comes back as a JSON line. accepted is false for
// a near miss, and its threshold is then the lower one Pryon reports near
// misses from. begin and end count samples since the client's first audio.
//
//   {"event":"detection","model":"alexa-en-US","keyword":"ALEXA","stop":false,
//    "accepted":true,"score":0.976,"threshold":0.923,"begin":133920,
//    "end":142080}
//
// "pryond status" prints the hello the running service gives, to check what
// it found.
//
// Every client gets the hello right away. One client listens at a time: the
// next one to send a frame waits until the first disconnects.
// PRYOND_DIR, PRYOND_CACHE and PRYOND_SOCKET override /data/pryon,
// /data/pryond and the init socket for testing. PRYOND_DEBUG=1 prints every result and Pryon's detection log
// lines to stderr.

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <ftw.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include <cutils/sockets.h>
#include <json/json.h>

#define PRYON_DIR "/data/pryon"
#define CACHE_DIR "/data/pryond"
#define SOCKET_NAME "pryond"
#define MAX_FRAME (1 << 20)
// The stop word's thresholds in the daemon's copy, as on CM14.
#define STOP_ACCEPT 0.18
#define STOP_NOTIFY 0.05

// The layouts below were read from libpryon.so 6.5.7.4 and checked on a Dot:
// the decoder's audio format, a client property event and the fields of a
// result this daemon reads.
struct PryonFormat {
    int32_t encoding, sampleRate, bitsPerSample, numChannels;
    int32_t channelTypes[32];
};
struct PryonProperty { const char* name; uint32_t pad; int64_t value; };
struct PryonEvent { uint32_t one; const PryonProperty* property; };

static int (*p_set_log)(void (*)(int, const char*, const char*));
static int (*p_set_result)(void (*)(const char*, void*));
static int (*p_model_new)(const char*, const char*, const char*);
static int (*p_model_delete)(const char*);
static void (*p_format_default)(PryonFormat*);
static int (*p_decoder_new)(const char*, const char*, const char*, PryonFormat, const char*);
static int (*p_decoder_delete)(const char*);
static int (*p_push)(const char*, uint64_t, const int16_t*, uint32_t);
static int (*p_push_events)(const char*, const PryonEvent*, uint32_t);
static int (*p_session_end)(const char*);

struct Model {
    std::string name;
    std::vector<std::string> locales, wake, stop;
    std::map<std::string, double> accept;
};
struct Score { double score, threshold; };

static std::string g_lib_dir = PRYON_DIR "/lib";
static std::string g_model_dir = PRYON_DIR "/models";
static std::string g_locales = PRYON_DIR "/locales.txt";
static std::string g_cache_dir = CACHE_DIR;
static bool g_debug;
static bool g_loaded;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_client = -1;
// Pryon logs the score of a detection just before it reports the detection
// itself, both on the decoder's thread. Keyed by decoder id.
static std::map<std::string, Score> g_scores;

static void send_line(const std::string& line) {
    pthread_mutex_lock(&g_lock);
    if (g_client >= 0) send(g_client, (line + "\n").c_str(), line.size() + 1, MSG_NOSIGNAL);
    pthread_mutex_unlock(&g_lock);
}

static std::string quote(const std::string& s) {
    std::string out = "\"";
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if ((unsigned char)c < 0x20) out += ' ';
        else out += c;
    }
    return out + "\"";
}

static std::string list(const std::vector<std::string>& v) {
    std::string out = "[";
    for (size_t i = 0; i < v.size(); ++i) out += (i ? "," : "") + quote(v[i]);
    return out + "]";
}

static std::string read_file(const std::string& path) {
    std::string out;
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

// kw.cfg.json names each keyword's spotter and gives its accept threshold
// right after. A scan for the two keys does: the file is Amazon's, fixed by
// the manifest's checksum.
static double accept_threshold(const std::string& cfg, const std::string& keyword) {
    size_t at = 0;
    while ((at = cfg.find("\"name\"", at)) != std::string::npos) {
        at += 6;
        size_t open = cfg.find('"', cfg.find(':', at));
        if (open == std::string::npos) break;
        size_t close = cfg.find('"', open + 1);
        if (close == std::string::npos) break;
        if (cfg.compare(open + 1, close - open - 1, keyword)) continue;
        size_t key = cfg.find("\"accept-threshold\"", close);
        if (key == std::string::npos) break;
        size_t colon = cfg.find(':', key);
        if (colon == std::string::npos) break;
        return atof(cfg.c_str() + colon + 1);
    }
    return -1;
}

// The keyword files sit next to the manifest in the older models and in
// BDPGeneratedFiles/ in the newer, multi-stage ones.
static std::string keyword_file(const std::string& dir, const char* name) {
    std::string path = dir + "/" + name;
    if (!access(path.c_str(), R_OK)) return path;
    return dir + "/BDPGeneratedFiles/" + name;
}

// Android 11's jsoncpp is the older one, with Reader and StyledWriter.
static bool parse_json(const std::string& text, Json::Value* root) {
    Json::Reader reader;
    return reader.parse(text, *root, false) && root->isObject();
}

static std::string write_json(const Json::Value& root) {
    Json::StyledWriter writer;
    return writer.write(root);
}

static bool is_stop(const Json::Value& v) {
    return v.isObject() && v.isMember("name") && v["name"].isString() && v["name"].asString() == "STOP";
}

// kw.cfg.json with the stop word's thresholds lowered, or false when it has
// no STOP spotter.
static bool patch_kw(const std::string& text, std::string* out) {
    Json::Value root;
    if (!parse_json(text, &root) || !root.isMember("keyword-orchestra")) return false;
    Json::Value& orchestra = root["keyword-orchestra"];
    if (!orchestra.isObject() || !orchestra.isMember("keyword-spotters")) return false;
    Json::Value& spotters = orchestra["keyword-spotters"];
    if (!spotters.isArray()) return false;
    bool found = false;
    for (Json::ArrayIndex i = 0; i < spotters.size(); ++i) {
        Json::Value& s = spotters[i];
        if (!is_stop(s) || !s.isMember("spotter") || !s["spotter"].isObject()) continue;
        Json::Value& spotter = s["spotter"];
        if (!spotter.isMember("classification-thresholds")) continue;
        Json::Value& t = spotter["classification-thresholds"];
        if (!t.isObject()) continue;
        t["accept-threshold"] = STOP_ACCEPT;
        t["notify-threshold"] = STOP_NOTIFY;
        found = true;
    }
    if (found) *out = write_json(root);
    return found;
}

// op.cfg.json with STOP counted while asleep, that is without the wake word,
// or false when there is nothing to change.
static bool patch_op(const std::string& text, std::string* out) {
    Json::Value root;
    if (!parse_json(text, &root) || !root.isMember("sleep") || !root["sleep"].isObject()) return false;
    Json::Value& sleep = root["sleep"];
    if (!sleep.isMember("rules") || !sleep["rules"].isArray()) return false;
    Json::Value& rules = sleep["rules"];
    for (Json::ArrayIndex i = 0; i < rules.size(); ++i)
        if (is_stop(rules[i])) return false;
    Json::Value rule(Json::objectValue);
    rule["name"] = "STOP";
    rule["next"] = "sleep";
    rules.append(rule);
    *out = write_json(root);
    return true;
}

static bool write_file(const std::string& path, const std::string& text) {
    unlink(path.c_str());
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return false;
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    return fclose(f) == 0 && ok;
}

static int remove_entry(const char* path, const struct stat*, int, struct FTW*) {
    remove(path);
    return 0;
}

static void remove_tree(const std::string& path) {
    nftw(path.c_str(), remove_entry, 16, FTW_DEPTH | FTW_PHYS);
}

// Fills the empty folder to with links to everything in from, then puts the
// patched keyword files in place of the links.
static bool mirror(const std::string& from, const std::string& to) {
    DIR* d = opendir(from.c_str());
    if (!d) return false;
    bool ok = true, kw = false;
    while (dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        std::string src = from + "/" + name, dst = to + "/" + name;
        struct stat st;
        if (stat(src.c_str(), &st)) continue;
        if (S_ISDIR(st.st_mode)) {
            ok = ok && !mkdir(dst.c_str(), 0700) && mirror(src, dst);
        } else {
            ok = ok && !symlink(src.c_str(), dst.c_str());
            kw = kw || name == "kw.cfg.json";
        }
    }
    closedir(d);
    if (!ok || !kw) return ok;
    std::string text;
    if (!patch_kw(read_file(from + "/kw.cfg.json"), &text)) return true;
    if (!write_file(to + "/kw.cfg.json", text)) return false;
    if (patch_op(read_file(from + "/op.cfg.json"), &text) && !write_file(to + "/op.cfg.json", text)) return false;
    return true;
}

// The manifest to load a model from: the daemon's copy, or the user's own
// files when the copy cannot be made.
static std::string prepare(const std::string& name) {
    std::string src = g_model_dir + "/" + name, models = g_cache_dir + "/models", dst = models + "/" + name;
    remove_tree(dst);
    mkdir(models.c_str(), 0700);
    if (mkdir(dst.c_str(), 0700) || !mirror(src, dst)) {
        fprintf(stderr, "cannot copy %s to %s, so its stop word needs the wake word first\n", src.c_str(), dst.c_str());
        return src + "/pryon.manifest";
    }
    return dst + "/pryon.manifest";
}

// locales.txt: one line per locale, the locale and then its model sets.
static std::vector<std::pair<std::string, std::string> > read_locales() {
    std::vector<std::pair<std::string, std::string> > out;
    FILE* f = fopen(g_locales.c_str(), "r");
    if (!f) return out;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        char* save = NULL;
        char* locale = strtok_r(line, " \t\r\n", &save);
        if (!locale) continue;
        while (char* set = strtok_r(NULL, " \t\r\n", &save)) out.push_back(std::make_pair(set, locale));
    }
    fclose(f);
    return out;
}

// The libraries a model's pryon.config loads by their full path, such as
// /system/lib/libtvm_modelops_ww_v5_cnet.so, that cannot be read. The image
// links that one to /data/pryon/lib.
static std::vector<std::string> missing_model_libs(const std::string& dir) {
    std::vector<std::string> out;
    std::string cfg = read_file(dir + "/pryon.config");
    size_t at = 0;
    while ((at = cfg.find("/system/", at)) != std::string::npos) {
        size_t end = cfg.find(".so", at);
        if (end == std::string::npos) break;
        end += 3;
        std::string path = cfg.substr(at, end - at);
        at = end;
        if (path.find_first_of(std::string("\0\"\n ", 4)) != std::string::npos) continue;
        if (access(path.c_str(), R_OK) && std::find(out.begin(), out.end(), path) == out.end())
            out.push_back(path);
    }
    return out;
}

// Reads a model, or says why it cannot be used.
static bool read_model(const std::string& name, Model* m, std::string* why) {
    std::string dir = g_model_dir + "/" + name;
    if (access((dir + "/pryon.manifest").c_str(), R_OK)) { *why = "no readable pryon.manifest"; return false; }
    std::vector<std::string> libs = missing_model_libs(dir);
    if (!libs.empty()) { *why = "cannot read " + libs[0]; return false; }
    // words.shrunk.txt: "<eps> 0", then one keyword and its id per line. The
    // newer models also list background tokens such as _BG_WORD_[S].
    FILE* f = fopen(keyword_file(dir, "words.shrunk.txt").c_str(), "r");
    if (!f) { *why = "no readable words.shrunk.txt"; return false; }
    m->name = name;
    char word[128];
    int id;
    while (fscanf(f, "%127s %d", word, &id) == 2) {
        if (word[0] == '<' || word[0] == '_') continue;
        (strcmp(word, "STOP") ? m->wake : m->stop).push_back(word);
    }
    fclose(f);
    std::string cfg = read_file(keyword_file(dir, "kw.cfg.json"));
    for (size_t i = 0; i < m->wake.size(); ++i) m->accept[m->wake[i]] = accept_threshold(cfg, m->wake[i]);
    std::string patched;
    bool lowered = patch_kw(cfg, &patched);
    for (size_t i = 0; i < m->stop.size(); ++i)
        m->accept[m->stop[i]] = lowered ? STOP_ACCEPT : accept_threshold(cfg, m->stop[i]);
    if (m->wake.empty() && m->stop.empty()) { *why = "no keywords in words.shrunk.txt"; return false; }
    return true;
}

typedef std::vector<std::pair<std::string, std::string> > Skipped;

// Every usable model, in the order locales.txt names them, then the ones it
// does not name, which listen for no locale. The others go in skipped.
static std::vector<Model> find_models(Skipped* skipped) {
    std::vector<Model> models;
    std::map<std::string, size_t> index;
    std::map<std::string, bool> seen;
    std::vector<std::pair<std::string, std::string> > locales = read_locales();
    for (size_t i = 0; i < locales.size(); ++i) {
        const std::string& name = locales[i].first;
        std::map<std::string, size_t>::iterator it = index.find(name);
        if (it == index.end()) {
            if (seen[name]) continue;
            seen[name] = true;
            Model m;
            std::string why;
            if (!read_model(name, &m, &why)) { skipped->push_back(std::make_pair(name, why)); continue; }
            index[name] = models.size();
            models.push_back(m);
            it = index.find(name);
        }
        models[it->second].locales.push_back(locales[i].second);
    }
    DIR* d = opendir(g_model_dir.c_str());
    if (!d) return models;
    std::vector<std::string> rest;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] != '.' && !seen.count(e->d_name)) rest.push_back(e->d_name);
    }
    closedir(d);
    std::sort(rest.begin(), rest.end());
    for (size_t i = 0; i < rest.size(); ++i) {
        Model m;
        std::string why;
        if (read_model(rest[i], &m, &why)) models.push_back(m);
        else skipped->push_back(std::make_pair(rest[i], why));
    }
    return models;
}

static void on_log(int level, const char*, const char* msg) {
    if (!msg) return;
    const char* id = strstr(msg, "DecoderID=");
    const char* sc = strstr(msg, "classifier_score=");
    const char* th = strstr(msg, "active_threshold=");
    if (g_debug && (strstr(msg, "detection_type") || strstr(msg, "ActiveKeywords") ||
                    strstr(msg, "keyword") || level <= 3))
        fprintf(stderr, "pryon[%d]: %s\n", level, msg);
    if (id && sc && th) {
        id += 10;
        const char* end = strchr(id, ':');
        if (!end) return;
        Score s = { atof(sc + 17), atof(th + 17) };
        pthread_mutex_lock(&g_lock);
        g_scores[std::string(id, end - id)] = s;
        pthread_mutex_unlock(&g_lock);
    } else if (level <= 2) {
        fprintf(stderr, "pryon: %s\n", msg);
    }
}

static void on_result(const char* decoder, void* result) {
    const uint8_t* r = (const uint8_t*)result;
    uint64_t begin, end;
    const char* keyword;
    int32_t type;
    memcpy(&begin, r + 0x08, 8);
    memcpy(&keyword, r + 0x1c, sizeof(keyword));
    memcpy(&type, r + 0x24, 4);
    memcpy(&end, r + 0x40, 8);
    if (g_debug)
        fprintf(stderr, "result %s keyword=%s type=%d begin=%llu end=%llu\n", decoder,
                keyword ? keyword : "(null)", type, (unsigned long long)begin, (unsigned long long)end);
    if (!keyword) return;
    // 2 is an accept, 0 a near miss. Fire OS also acts on 3.
    bool accepted = type == 2 || type == 3;
    Score s = { -1, -1 };
    pthread_mutex_lock(&g_lock);
    std::map<std::string, Score>::iterator it = g_scores.find(decoder);
    if (it != g_scores.end()) { s = it->second; g_scores.erase(it); }
    pthread_mutex_unlock(&g_lock);
    // Decoder ids are "ks-<model>".
    std::string model = strncmp(decoder, "ks-", 3) ? decoder : decoder + 3;
    char nums[160];
    snprintf(nums, sizeof(nums), "\"score\":%.4f,\"threshold\":%.4f,\"begin\":%llu,\"end\":%llu}",
             s.score, s.threshold, (unsigned long long)begin, (unsigned long long)end);
    send_line("{\"event\":\"detection\",\"model\":" + quote(model) + ",\"keyword\":" + quote(keyword) +
              ",\"stop\":" + (strcmp(keyword, "STOP") ? "false" : "true") +
              ",\"accepted\":" + (accepted ? "true" : "false") + "," + nums);
}

// Pryon's libraries, each after the ones it needs.
static const char* libs[] = {
    "libc++_shared.so", "libfst.so", "libfstfar.so", "libfstscript.so", "libfstngram.so",
    "libpugixml.so", "libpryonConfig.so", "libwatermars.so", "libAFEMetadataDecoder.so",
    "libNeMoRt.so", "libnemort-cnpy.so", "libacsl.so", "libblu_native_toolkit.so",
    "libgrxml_parser.so", "libpryon.so",
};

// The libraries that are not there or cannot be read.
static std::vector<std::string> missing_libs() {
    std::vector<std::string> out;
    for (size_t i = 0; i < sizeof(libs) / sizeof(*libs); ++i)
        if (access((g_lib_dir + "/" + libs[i]).c_str(), R_OK)) out.push_back(libs[i]);
    return out;
}

#define SYM(var, name) \
    if (!(*(void**)&var = dlsym(lib, name))) { fprintf(stderr, "missing %s\n", name); return false; }

// An app cannot point the linker at a library folder, and neither can this
// daemon without LD_LIBRARY_PATH, so each library is loaded by its full path
// after the ones it needs.
static bool load_pryon() {
    if (g_loaded) return true;
    void* lib = NULL;
    for (size_t i = 0; i < sizeof(libs) / sizeof(*libs); ++i) {
        std::string path = g_lib_dir + "/" + libs[i];
        if (!(lib = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL))) {
            fprintf(stderr, "dlopen %s: %s\n", path.c_str(), dlerror());
            return false;
        }
    }
    SYM(p_set_log, "PryonApi_SetLoggingCallback");
    SYM(p_set_result, "PryonApi_SetEnumeratedResultCallback");
    SYM(p_model_new, "PryonModelSet_New");
    SYM(p_model_delete, "PryonModelSet_Delete");
    SYM(p_format_default, "PryonDecoder_NewPryonMultichannelAudioFormat_Default");
    SYM(p_decoder_new, "PryonDecoder_NewSpotterAudioDecoder");
    SYM(p_decoder_delete, "PryonDecoder_Delete");
    SYM(p_push, "PryonDecoder_PushAudioEventSamples");
    SYM(p_push_events, "PryonDecoder_PushClientEvents");
    SYM(p_session_end, "PryonDecoder_SessionEnd");
    p_set_log(on_log);
    p_set_result(on_result);
    g_loaded = true;
    return true;
}

struct Session {
    std::vector<std::string> decoders, model_sets;
    std::map<std::string, int64_t> properties;
    uint64_t index;
};

static void push_properties(const std::vector<std::string>& decoders, const std::map<std::string, int64_t>& properties) {
    std::vector<PryonProperty> props;
    for (std::map<std::string, int64_t>::const_iterator it = properties.begin(); it != properties.end(); ++it) {
        PryonProperty p = { it->first.c_str(), 0, it->second };
        props.push_back(p);
    }
    if (props.empty()) return;
    std::vector<PryonEvent> events;
    for (size_t i = 0; i < props.size(); ++i) {
        PryonEvent e = { 1, &props[i] };
        events.push_back(e);
    }
    for (size_t i = 0; i < decoders.size(); ++i)
        p_push_events(decoders[i].c_str(), &events[0], events.size());
}

static void stop(Session& s) {
    for (size_t i = 0; i < s.decoders.size(); ++i) p_decoder_delete(s.decoders[i].c_str());
    for (size_t i = 0; i < s.model_sets.size(); ++i) p_model_delete(s.model_sets[i].c_str());
    s.decoders.clear();
    s.model_sets.clear();
    pthread_mutex_lock(&g_lock);
    g_scores.clear();
    pthread_mutex_unlock(&g_lock);
}

static void error(const std::string& message) {
    send_line("{\"event\":\"error\",\"message\":" + quote(message) + "}");
}

static void start(Session& s, const std::string& wanted) {
    stop(s);
    if (!load_pryon()) { error("cannot load Pryon from " + g_lib_dir); return; }
    Skipped skipped;
    std::vector<Model> models = find_models(&skipped);
    std::vector<std::string> started;
    for (size_t i = 0; i < models.size(); ++i) {
        const Model& m = models[i];
        if (!wanted.empty() && ("," + wanted + ",").find("," + m.name + ",") == std::string::npos) continue;
        std::string set = "ks-set-" + m.name, decoder = "ks-" + m.name;
        std::string manifest = prepare(m.name);
        if (p_model_new(set.c_str(), manifest.c_str(), "")) { error("cannot load model " + m.name); continue; }
        s.model_sets.push_back(set);
        // Without this Pryon spots only the model's default keyword, which
        // leaves out STOP.
        std::vector<std::string> all = m.wake;
        all.insert(all.end(), m.stop.begin(), m.stop.end());
        std::string config = "{\"keyword\":{\"enabled-keyword-list\":" + list(all) + "}}";
        PryonFormat format;
        p_format_default(&format);
        if (p_decoder_new(decoder.c_str(), set.c_str(), "pryon", format, config.c_str())) {
            error("cannot start a decoder for " + m.name);
            continue;
        }
        s.decoders.push_back(decoder);
        started.push_back(m.name);
    }
    s.index = 0;
    push_properties(s.decoders, s.properties);
    if (started.empty()) error("no model to listen with");
    else send_line("{\"event\":\"started\",\"models\":" + list(started) + "}");
}

static bool read_all(int fd, void* buf, size_t len) {
    uint8_t* p = (uint8_t*)buf;
    while (len) {
        ssize_t n = read(fd, p, len);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        p += n;
        len -= n;
    }
    return true;
}

// The first line every client gets: what is installed and usable.
static std::string hello() {
    std::vector<std::string> missing = missing_libs();
    Skipped skipped;
    std::vector<Model> models;
    if (missing.empty()) models = find_models(&skipped);
    std::vector<std::string> locales;
    bool wake = false;
    std::string out;
    for (size_t i = 0; i < models.size(); ++i) {
        const Model& m = models[i];
        wake = wake || (!m.wake.empty() && !m.locales.empty());
        for (size_t j = 0; j < m.locales.size(); ++j)
            if (std::find(locales.begin(), locales.end(), m.locales[j]) == locales.end()) locales.push_back(m.locales[j]);
        std::string accept;
        for (std::map<std::string, double>::const_iterator it = m.accept.begin(); it != m.accept.end(); ++it) {
            if (it->second < 0) continue;
            char num[32];
            snprintf(num, sizeof(num), "%.4g", it->second);
            accept += (accept.empty() ? "" : ",") + quote(it->first) + ":" + num;
        }
        out += (i ? "," : "") + std::string("{\"model\":") + quote(m.name) + ",\"locales\":" + list(m.locales) +
               ",\"wake_words\":" + list(m.wake) + ",\"stop_words\":" + list(m.stop) +
               ",\"accept_thresholds\":{" + accept + "}}";
    }
    std::string skip;
    for (size_t i = 0; i < skipped.size(); ++i)
        skip += (i ? "," : "") + std::string("{\"model\":") + quote(skipped[i].first) +
                ",\"reason\":" + quote(skipped[i].second) + "}";
    return std::string("{\"event\":\"hello\",\"version\":2,\"available\":") + (missing.empty() && wake ? "true" : "false") +
           ",\"missing\":" + list(missing) + ",\"locales\":" + list(locales) + ",\"models\":[" + out +
           "],\"skipped\":[" + skip + "]}";
}

static bool read_frame(int fd, uint8_t* type, std::vector<uint8_t>* payload) {
    uint8_t header[5];
    if (!read_all(fd, header, sizeof(header))) return false;
    uint32_t len = header[1] | header[2] << 8 | header[3] << 16 | (uint32_t)header[4] << 24;
    if (len > MAX_FRAME) return false;
    payload->resize(len);
    if (len && !read_all(fd, &(*payload)[0], len)) return false;
    *type = header[0];
    return true;
}

// Held by the client that listens.
static pthread_mutex_t g_session = PTHREAD_MUTEX_INITIALIZER;

static void* serve(void* arg) {
    int fd = (int)(intptr_t)arg;
    std::string line = hello() + "\n";
    send(fd, line.data(), line.size(), MSG_NOSIGNAL);

    // A client that only wants the hello, such as pryond status, hangs up
    // here and never waits for the one listening.
    uint8_t type;
    std::vector<uint8_t> payload;
    if (!read_frame(fd, &type, &payload)) {
        close(fd);
        return NULL;
    }
    pthread_mutex_lock(&g_session);
    pthread_mutex_lock(&g_lock);
    g_client = fd;
    pthread_mutex_unlock(&g_lock);

    Session s;
    s.index = 0;
    do {
        std::string text(payload.begin(), payload.end());
        switch (type) {
        case 'S':
            start(s, text);
            break;
        case 'A': {
            uint32_t samples = payload.size() / 2;
            if (!samples) break;
            for (size_t i = 0; i < s.decoders.size(); ++i)
                p_push(s.decoders[i].c_str(), s.index, (const int16_t*)&payload[0], samples);
            s.index += samples;
            break;
        }
        case 'P': {
            size_t eq = text.find('=');
            if (eq == std::string::npos || !eq) { error("bad property"); break; }
            std::map<std::string, int64_t> one;
            one[text.substr(0, eq)] = atoll(text.c_str() + eq + 1);
            s.properties[text.substr(0, eq)] = one.begin()->second;
            push_properties(s.decoders, one);
            break;
        }
        case 'E':
            for (size_t i = 0; i < s.decoders.size(); ++i) p_session_end(s.decoders[i].c_str());
            push_properties(s.decoders, s.properties);
            break;
        default:
            error("unknown frame");
        }
    } while (read_frame(fd, &type, &payload));
    if (g_loaded) stop(s);
    pthread_mutex_lock(&g_lock);
    g_client = -1;
    pthread_mutex_unlock(&g_lock);
    pthread_mutex_unlock(&g_session);
    close(fd);
    return NULL;
}

static int make_socket(const char* path) {
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) return -1;
    unlink(path);
    sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (bind(s, (sockaddr*)&addr, sizeof(addr)) < 0) { close(s); return -1; }
    chmod(path, 0666);
    return s;
}

// Prints the running service's hello.
static int status() {
    const char* path = getenv("PRYOND_SOCKET") ? getenv("PRYOND_SOCKET") : "/dev/socket/" SOCKET_NAME;
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (s < 0 || connect(s, (sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "pryond is not running: cannot connect to %s\n", path);
        return 1;
    }
    std::string line;
    char c;
    while (read(s, &c, 1) == 1 && c != '\n') line += c;
    close(s);
    if (line.empty()) { fprintf(stderr, "pryond did not answer\n"); return 1; }
    printf("%s\n", line.c_str());
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1) {
        if (!strcmp(argv[1], "status")) return status();
        fprintf(stderr, "usage: pryond [status]\n");
        return 2;
    }
    signal(SIGPIPE, SIG_IGN);
    if (const char* dir = getenv("PRYOND_DIR")) {
        g_lib_dir = std::string(dir) + "/lib";
        g_model_dir = std::string(dir) + "/models";
        g_locales = std::string(dir) + "/locales.txt";
    }
    if (const char* dir = getenv("PRYOND_CACHE")) g_cache_dir = dir;
    g_debug = getenv("PRYOND_DEBUG") && !strcmp(getenv("PRYOND_DEBUG"), "1");
    setvbuf(stderr, NULL, _IOLBF, 0);
    // init's socket, which Android 11 publishes as ANDROID_SOCKET_pryond.
    // The daemon runs as nobody, so it cannot make /dev/socket/pryond itself
    // when init has.
    int s = -1;
    if (getenv("PRYOND_SOCKET")) s = make_socket(getenv("PRYOND_SOCKET"));
    else if ((s = android_get_control_socket(SOCKET_NAME)) < 0) s = make_socket("/dev/socket/" SOCKET_NAME);
    if (s < 0 || listen(s, 4) < 0) return 1;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    for (;;) {
        int c = accept(s, NULL, NULL);
        if (c < 0) continue;
        pthread_t t;
        if (pthread_create(&t, &attr, serve, (void*)(intptr_t)c)) close(c);
    }
}
