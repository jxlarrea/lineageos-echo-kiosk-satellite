/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package me.jxl.echo.service;

import android.app.ActivityManager;
import android.app.AlarmManager;
import android.adb.ADBRootService;
import android.app.admin.DevicePolicyManager;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.Resources;
import android.media.AudioManager;
import android.net.ConnectivityManager;
import android.net.LinkAddress;
import android.net.LinkProperties;
import android.net.Network;
import android.net.wifi.WifiConfiguration;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.BatteryManager;
import android.os.Handler;
import android.os.Looper;
import android.os.LocaleList;
import android.os.PowerManager;
import android.os.SystemClock;
import android.os.SystemProperties;
import android.provider.Settings;
import android.util.Log;

import com.android.internal.app.LocalePicker;
import com.android.internal.widget.LockPatternUtils;

import java.io.BufferedReader;
import java.io.FileReader;
import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.TimeZone;

/**
 * echo_service's commands. The script sends each one as a broadcast with its
 * words as extras, and the reply goes back as the broadcast's result. The
 * manifest only lets senders holding WRITE_SECURE_SETTINGS through.
 */
public final class Commands extends BroadcastReceiver {
    private static final String TAG = "EchoCommands";
    private static final String KS_PACKAGE = "me.jxl.kiosk_satellite";
    private static final String ADB_WIFI_PROPERTY = "persist.echo.adb_wifi";
    private static final int ADB_WIFI_PORT = 5555;
    // Android's default for adb keys: they expire after 7 days unused.
    private static final long ADB_KEY_DEFAULT_MS = 7L * 24 * 60 * 60 * 1000;
    private static final int PAIRING_SECONDS = 300;
    private static final String PRIVACY_STATE = "/sys/devices/platform/gpio-privacy/state";
    // PhoneWindowManager's long press actions: the power menu, and holding the
    // button past the privacy chip's threshold to turn the screen on and off
    // (patches/cronos/frameworks/base).
    static final int LONG_PRESS_POWER_MENU = 1;
    static final int LONG_PRESS_POWER_SCREEN = 7;

    private static final String[] STREAM_NAMES = { "music", "alarm", "notification", "ring", "system" };
    private static final int[] STREAMS = {
            AudioManager.STREAM_MUSIC, AudioManager.STREAM_ALARM, AudioManager.STREAM_NOTIFICATION,
            AudioManager.STREAM_RING, AudioManager.STREAM_SYSTEM };

    private static final Handler sHandler = new Handler(Looper.getMainLooper());

    /** A command the words do not make sense for. The script exits with 2. */
    private static final class UsageException extends Exception {
        UsageException(String message) {
            super(message);
        }
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        int count = intent.getIntExtra("argc", 0);
        String[] args = new String[count];
        for (int i = 0; i < count; i++) {
            String arg = intent.getStringExtra("arg" + i);
            args[i] = arg == null ? "" : arg;
        }
        int code = 0;
        String reply;
        try {
            reply = run(context, args);
        } catch (UsageException e) {
            code = 2;
            reply = e.getMessage();
        } catch (RuntimeException e) {
            Log.w(TAG, "echo_service " + String.join(" ", args) + " failed", e);
            code = 1;
            reply = "failed: " + e.getMessage();
        }
        setResultCode(code);
        setResultData(reply);
    }

    private static String run(Context context, String[] args) throws UsageException {
        if (args.length == 0) throw new UsageException("no command");
        String[] rest = Arrays.copyOfRange(args, 1, args.length);
        switch (args[0]) {
            case "status": return status(context);
            case "wifi": return wifi(context, rest);
            case "bt": return bluetooth(rest);
            case "time": return time(context, rest);
            case "adb": return adb(context, rest);
            case "lockscreen": return lockscreen(context, rest);
            case "privacy": return privacy(rest);
            case "button": return button(context, rest);
            case "locale": return locale(rest);
            case "screen": return screen(context, rest);
            case "volume": return volume(context, rest);
            case "hostname": return hostname(context, rest);
            case "update": return update(context, rest);
            default: throw new UsageException("unknown command: " + args[0]);
        }
    }

    private static String arg(String[] args, int index) {
        return index < args.length ? args[index] : "";
    }

    private static boolean onOff(String word) throws UsageException {
        if ("on".equals(word)) return true;
        if ("off".equals(word)) return false;
        throw new UsageException("expected on or off, not '" + word + "'");
    }

    // Status

    private static String status(Context context) {
        StringBuilder out = new StringBuilder();
        out.append("Release: ").append(SystemProperties.get("ro.echo.release", "unknown"))
                .append(" (").append(SystemProperties.get("ro.product.model")).append(")\n");
        out.append("Name: ").append(deviceName(context)).append('\n');
        out.append("Uptime: ").append(duration(SystemClock.elapsedRealtime() / 1000)).append('\n');
        out.append("Memory available: ").append(memAvailableMb()).append(" MB\n");
        String temperature = temperature();
        if (!temperature.isEmpty()) out.append("Temperature: ").append(temperature).append('\n');
        out.append("Wi-Fi: ").append(wifiStatus(context)).append('\n');
        out.append("Bluetooth: ").append(bluetoothStatus()).append('\n');
        out.append("adb: ").append(adbStatus(context)).append('\n');
        out.append("Time: ").append(timeStatus(context)).append('\n');
        out.append("Locale: ").append(currentLocales()).append('\n');
        out.append("Lock screen: ").append(lockscreenStatus(context)).append('\n');
        out.append("Privacy: ").append(privacyStatus()).append('\n');
        out.append("Button: ").append(buttonStatus(context)).append('\n');
        out.append("Screen: ").append(screenStatus(context)).append('\n');
        out.append("Kiosk Satellite: ").append(kioskSatelliteStatus(context)).append('\n');
        out.append("System update: ").append(SystemUpdate.get(context).summary());
        return out.toString();
    }

    private static String duration(long seconds) {
        long days = seconds / 86400, hours = seconds / 3600 % 24, minutes = seconds / 60 % 60;
        return days > 0 ? String.format("%dd %dh %dm", days, hours, minutes)
                : String.format("%dh %dm", hours, minutes);
    }

    private static long memAvailableMb() {
        try (BufferedReader in = new BufferedReader(new FileReader("/proc/meminfo"))) {
            String line;
            while ((line = in.readLine()) != null) {
                if (line.startsWith("MemAvailable:")) {
                    return Long.parseLong(line.replaceAll("[^0-9]", "")) / 1024;
                }
            }
        } catch (IOException | NumberFormatException ignored) { }
        return -1;
    }

    // The SoC's own sensor, in millidegrees.
    private static String temperature() {
        try (BufferedReader in = new BufferedReader(new FileReader("/sys/class/thermal/thermal_zone0/temp"))) {
            return String.format(Locale.US, "%.1f °C", Long.parseLong(in.readLine().trim()) / 1000f);
        } catch (IOException | NumberFormatException | NullPointerException e) {
            return "";
        }
    }

    private static String kioskSatelliteStatus(Context context) {
        PackageInfo info;
        try {
            info = context.getPackageManager().getPackageInfo(KS_PACKAGE, 0);
        } catch (PackageManager.NameNotFoundException e) {
            return "not installed";
        }
        DevicePolicyManager dpm = context.getSystemService(DevicePolicyManager.class);
        ComponentName owner = dpm == null ? null : dpm.getDeviceOwnerComponentOnAnyUser();
        boolean isOwner = owner != null && KS_PACKAGE.equals(owner.getPackageName());
        return info.versionName + (isOwner ? ", device owner" : ", not the device owner");
    }

    // Wi-Fi

    private static String wifi(Context context, String[] args) throws UsageException {
        WifiManager wifi = context.getSystemService(WifiManager.class);
        switch (arg(args, 0)) {
            case "":
            case "status":
                return wifiStatus(context);
            case "on":
                wifi.setWifiEnabled(true);
                wifi.reconnect();
                return "Wi-Fi on";
            case "off":
                wifi.setWifiEnabled(false);
                return "Wi-Fi off";
            case "connect":
                if (args.length < 2 || args[1].isEmpty()) throw new UsageException("usage: wifi connect <ssid> [password]");
                connectWifi(wifi, args[1], arg(args, 2));
                return "Connecting to " + args[1] + ". Run echo_service wifi to check.";
            default:
                throw new UsageException("usage: wifi [status|on|off|connect <ssid> [password]]");
        }
    }

    private static void connectWifi(WifiManager wifi, String ssid, String password) {
        WifiConfiguration config = new WifiConfiguration();
        // Android 11 only takes the outer quotes off before the name and
        // password reach wpa_supplicant, so nothing inside is escaped.
        config.SSID = "\"" + ssid + "\"";
        if (password.isEmpty()) {
            config.allowedKeyManagement.set(WifiConfiguration.KeyMgmt.NONE);
        } else {
            // A 64 digit hex key goes as it is, a passphrase in quotes.
            config.preSharedKey = password.matches("[0-9A-Fa-f]{64}") ? password : "\"" + password + "\"";
            config.allowedKeyManagement.set(WifiConfiguration.KeyMgmt.WPA_PSK);
        }
        wifi.setWifiEnabled(true);
        int id = -1;
        List<WifiConfiguration> saved = wifi.getConfiguredNetworks();
        for (int i = 0; saved != null && i < saved.size(); i++) {
            if (config.SSID.equals(saved.get(i).SSID)) id = saved.get(i).networkId;
        }
        if (id >= 0) {
            config.networkId = id;
            id = wifi.updateNetwork(config);
        } else {
            id = wifi.addNetwork(config);
        }
        if (id < 0) throw new IllegalStateException("Android did not take the network");
        wifi.enableNetwork(id, true);
        wifi.reconnect();
    }

    private static String wifiStatus(Context context) {
        WifiManager wifi = context.getSystemService(WifiManager.class);
        if (!wifi.isWifiEnabled()) return "off";
        WifiInfo info = wifi.getConnectionInfo();
        if (info == null || info.getNetworkId() < 0) return "on, not connected";
        StringBuilder out = new StringBuilder("connected to ").append(info.getSSID().replace("\"", ""));
        out.append(", ").append(info.getFrequency()).append(" MHz, ").append(info.getRssi()).append(" dBm");
        ConnectivityManager connectivity = context.getSystemService(ConnectivityManager.class);
        for (Network network : connectivity.getAllNetworks()) {
            LinkProperties link = connectivity.getLinkProperties(network);
            if (link == null || !"wlan0".equals(link.getInterfaceName())) continue;
            for (LinkAddress address : link.getLinkAddresses()) {
                if (address.isIpv4()) out.append(", ").append(address.getAddress().getHostAddress());
            }
        }
        return out.toString();
    }

    // Bluetooth

    private static String bluetooth(String[] args) throws UsageException {
        BluetoothAdapter adapter = BluetoothAdapter.getDefaultAdapter();
        if (adapter == null) return "no Bluetooth";
        switch (arg(args, 0)) {
            case "":
            case "status":
                return bluetoothStatus();
            case "on":
                adapter.enable();
                return "Bluetooth on";
            case "off":
                adapter.disable();
                return "Bluetooth off";
            case "pair":
                adapter.enable();
                makeDiscoverable(adapter, 0);
                return "Discoverable as " + adapter.getName() + " for " + PAIRING_SECONDS / 60
                        + " minutes. Accept the pairing on the screen.";
            default:
                throw new UsageException("usage: bt [status|on|off|pair]");
        }
    }

    // Bluetooth takes a moment to come on, and only an adapter that is on
    // can be made discoverable.
    private static void makeDiscoverable(final BluetoothAdapter adapter, final int tries) {
        if (adapter.getState() == BluetoothAdapter.STATE_ON) {
            adapter.setScanMode(BluetoothAdapter.SCAN_MODE_CONNECTABLE_DISCOVERABLE, PAIRING_SECONDS);
            return;
        }
        if (tries >= 40) {
            Log.w(TAG, "Bluetooth did not come on, so it is not discoverable");
            return;
        }
        sHandler.postDelayed(() -> makeDiscoverable(adapter, tries + 1), 250);
    }

    private static String bluetoothStatus() {
        BluetoothAdapter adapter = BluetoothAdapter.getDefaultAdapter();
        if (adapter == null) return "none";
        if (!adapter.isEnabled()) return "off";
        StringBuilder out = new StringBuilder("on as ").append(adapter.getName());
        if (adapter.getScanMode() == BluetoothAdapter.SCAN_MODE_CONNECTABLE_DISCOVERABLE) out.append(", discoverable");
        StringBuilder paired = new StringBuilder();
        for (BluetoothDevice device : adapter.getBondedDevices()) {
            if (paired.length() > 0) paired.append(", ");
            paired.append(device.getName()).append(device.isConnected() ? " (connected)" : "");
        }
        if (paired.length() > 0) out.append(", paired with ").append(paired);
        return out.toString();
    }

    // Time

    private static String time(Context context, String[] args) throws UsageException {
        ContentResolver resolver = context.getContentResolver();
        AlarmManager alarms = context.getSystemService(AlarmManager.class);
        switch (arg(args, 0)) {
            case "":
            case "status":
                return timeStatus(context);
            case "sync":
            case "set": {
                if (args.length < 2) throw new UsageException("usage: time sync <seconds since 1970> [zone]");
                long when;
                try {
                    when = Long.parseLong(args[1]);
                } catch (NumberFormatException e) {
                    throw new UsageException("not a time in seconds since 1970: " + args[1]);
                }
                // Milliseconds pass through as they are.
                if (when < 100000000000L) when *= 1000;
                if (args.length > 2) setZone(resolver, alarms, args[2]);
                alarms.setTime(when);
                return timeStatus(context);
            }
            case "zone":
                if (args.length < 2) throw new UsageException("usage: time zone <zone>");
                setZone(resolver, alarms, args[1]);
                return timeStatus(context);
            case "auto":
                Settings.Global.putInt(resolver, Settings.Global.AUTO_TIME, onOff(arg(args, 1)) ? 1 : 0);
                return timeStatus(context);
            default:
                throw new UsageException("usage: time [status|sync <seconds> [zone]|zone <zone>|auto on|off]");
        }
    }

    // A Wi-Fi only device has no way to learn its zone, so automatic time
    // zone goes off once one is set.
    private static void setZone(ContentResolver resolver, AlarmManager alarms, String zone) throws UsageException {
        String id = resolveZone(zone);
        if (id == null) throw new UsageException("unknown time zone: " + zone);
        Settings.Global.putInt(resolver, Settings.Global.AUTO_TIME_ZONE, 0);
        alarms.setTimeZone(id);
    }

    // An IANA name such as America/New_York, or a Windows one such as
    // "Eastern Standard Time", as PowerShell's Get-TimeZone gives it.
    static String resolveZone(String zone) {
        for (String id : TimeZone.getAvailableIDs()) {
            if (id.equalsIgnoreCase(zone)) return id;
        }
        return android.icu.util.TimeZone.getIDForWindowsID(zone, "001");
    }

    private static String timeStatus(Context context) {
        SimpleDateFormat format = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss Z", Locale.US);
        TimeZone zone = TimeZone.getDefault();
        format.setTimeZone(zone);
        boolean auto = Settings.Global.getInt(context.getContentResolver(), Settings.Global.AUTO_TIME, 1) == 1;
        return format.format(new Date()) + " " + zone.getID() + ", network time " + (auto ? "on" : "off");
    }

    // adb

    private static String adb(Context context, String[] args) throws UsageException {
        switch (arg(args, 0)) {
            case "":
            case "status":
                return adbStatus(context);
            case "wifi": {
                if (args.length < 2) return adbStatus(context);
                boolean on = onOff(args[1]);
                // init (init.kiosk-satellite.rc) sets the port and restarts adbd.
                SystemProperties.set(ADB_WIFI_PROPERTY, on ? "1" : "0");
                // The reply may not arrive: the restart drops every adb
                // connection, this one too.
                return (on ? "adb over Wi-Fi on, port " + ADB_WIFI_PORT : "adb over Wi-Fi off")
                        + ". adb restarts, so reconnect.";
            }
            case "root": {
                // LineageOS's Rooted debugging switch. adb root still has to
                // be run afterwards.
                if (args.length < 2) return adbStatus(context);
                boolean on = onOff(args[1]);
                new ADBRootService().setEnabled(on);
                return on ? "adb root allowed. Run adb root to use it." : "adb root not allowed";
            }
            case "keys": {
                if (!"expire".equals(arg(args, 1))) throw new UsageException("usage: adb keys expire on|off");
                boolean expire = onOff(arg(args, 2));
                Settings.Global.putLong(context.getContentResolver(),
                        Settings.Global.ADB_ALLOWED_CONNECTION_TIME, expire ? ADB_KEY_DEFAULT_MS : 0);
                return expire ? "adb keys expire after 7 days unused" : "adb keys never expire";
            }
            default:
                throw new UsageException("usage: adb [status|wifi on|off|root on|off|keys expire on|off]");
        }
    }

    private static String adbStatus(Context context) {
        ContentResolver resolver = context.getContentResolver();
        boolean usb = Settings.Global.getInt(resolver, Settings.Global.ADB_ENABLED, 0) == 1;
        int port = SystemProperties.getInt("service.adb.tcp.port", 0);
        long keys = Settings.Global.getLong(resolver, Settings.Global.ADB_ALLOWED_CONNECTION_TIME, ADB_KEY_DEFAULT_MS);
        return "USB " + (usb ? "on" : "off") + ", Wi-Fi " + (port > 0 ? "on, port " + port : "off")
                + ", root " + (new ADBRootService().getEnabled() ? "allowed" : "not allowed")
                + ", keys " + (keys == 0 ? "never expire" : "expire after " + keys / 86400000 + " days unused");
    }

    // Lock screen

    private static String lockscreen(Context context, String[] args) throws UsageException {
        LockPatternUtils lock = new LockPatternUtils(context);
        int user = ActivityManager.getCurrentUser();
        switch (arg(args, 0)) {
            case "":
            case "status":
                return lockscreenStatus(context);
            case "on":
                lock.setLockScreenDisabled(false, user);
                return lockscreenStatus(context);
            case "off":
                if (lock.isSecure(user)) {
                    throw new UsageException("a PIN, pattern or password is set. Remove it in Settings first.");
                }
                lock.setLockScreenDisabled(true, user);
                return lockscreenStatus(context);
            default:
                throw new UsageException("usage: lockscreen [status|on|off]");
        }
    }

    private static String lockscreenStatus(Context context) {
        LockPatternUtils lock = new LockPatternUtils(context);
        int user = ActivityManager.getCurrentUser();
        if (lock.isSecure(user)) return "on, with a PIN, pattern or password";
        return lock.isLockScreenDisabled(user) ? "off" : "on, swipe to unlock";
    }

    // Privacy and the button

    // A press shorter than about 3 seconds turns privacy on and off in the
    // privacy chip itself, so the only thing to set here is what holding the
    // button does.
    private static String privacy(String[] args) throws UsageException {
        if (args.length > 0 && !args[0].equals("status")) {
            throw new UsageException("usage: privacy [status]. The button turns privacy on and off.");
        }
        return privacyStatus();
    }

    private static String privacyStatus() {
        String state = readLine(PRIVACY_STATE);
        if (state.isEmpty()) return "no privacy button";
        return "1".equals(state) ? "on, microphones and camera off" : "off";
    }

    private static String button(Context context, String[] args) throws UsageException {
        ContentResolver resolver = context.getContentResolver();
        switch (arg(args, 0)) {
            case "":
            case "status":
                return buttonStatus(context);
            case "screen":
                Settings.Global.putInt(resolver, Settings.Global.POWER_BUTTON_LONG_PRESS, LONG_PRESS_POWER_SCREEN);
                return buttonStatus(context);
            case "menu":
                Settings.Global.putInt(resolver, Settings.Global.POWER_BUTTON_LONG_PRESS, LONG_PRESS_POWER_MENU);
                return buttonStatus(context);
            default:
                throw new UsageException("usage: button [status|screen|menu]");
        }
    }

    private static String buttonStatus(Context context) {
        int longPress = Settings.Global.getInt(context.getContentResolver(),
                Settings.Global.POWER_BUTTON_LONG_PRESS, LONG_PRESS_POWER_MENU);
        if (longPress == LONG_PRESS_POWER_SCREEN) {
            return "holding it " + SystemProperties.getLong("ro.config.long_press_power_screen_ms", 3200) / 1000f
                    + " s turns the screen on and off, a short press turns privacy on and off";
        }
        return (longPress == LONG_PRESS_POWER_MENU ? "holding it opens the power menu"
                : "holding it does action " + longPress)
                + ", a short press turns the screen on and off and privacy on and off";
    }

    private static String readLine(String path) {
        try (BufferedReader in = new BufferedReader(new FileReader(path))) {
            String line = in.readLine();
            return line == null ? "" : line.trim();
        } catch (IOException e) {
            return "";
        }
    }

    // Locale

    private static String locale(String[] args) throws UsageException {
        if (args.length == 0 || args[0].equals("status")) return currentLocales();
        LocaleList locales = LocaleList.forLanguageTags(args[0]);
        if (locales.isEmpty() || locales.get(0).getLanguage().isEmpty()) {
            throw new UsageException("not a language tag such as en-US or es-MX: " + args[0]);
        }
        LocalePicker.updateLocales(locales);
        return locales.toLanguageTags();
    }

    private static String currentLocales() {
        return Resources.getSystem().getConfiguration().getLocales().toLanguageTags();
    }

    // Screen

    private static String screen(Context context, String[] args) throws UsageException {
        ContentResolver resolver = context.getContentResolver();
        PowerManager power = context.getSystemService(PowerManager.class);
        switch (arg(args, 0)) {
            case "":
            case "status":
                return screenStatus(context);
            case "on":
                power.wakeUp(SystemClock.uptimeMillis(), PowerManager.WAKE_REASON_APPLICATION, "echo_service");
                return "screen on";
            case "off":
                power.goToSleep(SystemClock.uptimeMillis());
                return "screen off";
            case "brightness": {
                String value = arg(args, 1);
                if (value.equals("auto")) {
                    Settings.System.putInt(resolver, Settings.System.SCREEN_BRIGHTNESS_MODE,
                            Settings.System.SCREEN_BRIGHTNESS_MODE_AUTOMATIC);
                } else {
                    int level = number(value, 0, 255, "brightness from 0 to 255 or auto");
                    Settings.System.putInt(resolver, Settings.System.SCREEN_BRIGHTNESS_MODE,
                            Settings.System.SCREEN_BRIGHTNESS_MODE_MANUAL);
                    Settings.System.putInt(resolver, Settings.System.SCREEN_BRIGHTNESS, level);
                }
                return screenStatus(context);
            }
            case "timeout": {
                String value = arg(args, 1);
                int ms = value.equals("never") ? Integer.MAX_VALUE
                        : number(value, 5, 86400, "timeout in seconds or never") * 1000;
                Settings.System.putInt(resolver, Settings.System.SCREEN_OFF_TIMEOUT, ms);
                return screenStatus(context);
            }
            case "ambient": {
                boolean on = onOff(arg(args, 1));
                Settings.Secure.putInt(resolver, Settings.Secure.DOZE_ALWAYS_ON, on ? 1 : 0);
                Settings.Secure.putInt(resolver, Settings.Secure.DOZE_ENABLED, on ? 1 : 0);
                return screenStatus(context);
            }
            case "stay-awake":
                Settings.Global.putInt(resolver, Settings.Global.STAY_ON_WHILE_PLUGGED_IN,
                        onOff(arg(args, 1)) ? BatteryManager.BATTERY_PLUGGED_AC
                                | BatteryManager.BATTERY_PLUGGED_USB | BatteryManager.BATTERY_PLUGGED_WIRELESS : 0);
                return screenStatus(context);
            default:
                throw new UsageException("usage: screen [status|on|off|brightness <0-255|auto>"
                        + "|timeout <seconds|never>|stay-awake on|off|ambient on|off]");
        }
    }

    private static String screenStatus(Context context) {
        ContentResolver resolver = context.getContentResolver();
        PowerManager power = context.getSystemService(PowerManager.class);
        boolean auto = Settings.System.getInt(resolver, Settings.System.SCREEN_BRIGHTNESS_MODE, 0)
                == Settings.System.SCREEN_BRIGHTNESS_MODE_AUTOMATIC;
        int brightness = Settings.System.getInt(resolver, Settings.System.SCREEN_BRIGHTNESS, -1);
        int timeout = Settings.System.getInt(resolver, Settings.System.SCREEN_OFF_TIMEOUT, -1);
        boolean awake = Settings.Global.getInt(resolver, Settings.Global.STAY_ON_WHILE_PLUGGED_IN, 0) != 0;
        boolean ambient = Settings.Secure.getInt(resolver, Settings.Secure.DOZE_ALWAYS_ON, 0) != 0;
        return (power.isInteractive() ? "on" : "off")
                + ", brightness " + (auto ? "auto" : String.valueOf(brightness))
                + ", timeout " + (timeout == Integer.MAX_VALUE ? "never" : timeout / 1000 + " s")
                + ", stay awake " + (awake ? "on" : "off")
                + ", ambient display " + (ambient ? "on" : "off");
    }

    // Volume

    private static String volume(Context context, String[] args) throws UsageException {
        AudioManager audio = context.getSystemService(AudioManager.class);
        if (args.length == 0) {
            StringBuilder out = new StringBuilder();
            for (int i = 0; i < STREAMS.length; i++) {
                if (out.length() > 0) out.append(", ");
                out.append(STREAM_NAMES[i]).append(' ').append(audio.getStreamVolume(STREAMS[i]))
                        .append('/').append(audio.getStreamMaxVolume(STREAMS[i]));
            }
            return out.toString();
        }
        int index = Arrays.asList(STREAM_NAMES).indexOf(args[0]);
        if (index < 0 || args.length < 2) {
            throw new UsageException("usage: volume [music|alarm|notification|ring|system <level>]");
        }
        int stream = STREAMS[index];
        int level = number(args[1], 0, audio.getStreamMaxVolume(stream),
                "level from 0 to " + audio.getStreamMaxVolume(stream));
        audio.setStreamVolume(stream, level, 0);
        return args[0] + " " + audio.getStreamVolume(stream) + "/" + audio.getStreamMaxVolume(stream);
    }

    private static int number(String value, int min, int max, String what) throws UsageException {
        try {
            int n = Integer.parseInt(value);
            if (n >= min && n <= max) return n;
        } catch (NumberFormatException ignored) { }
        throw new UsageException("expected a " + what + ", not '" + value + "'");
    }

    // Name

    private static String hostname(Context context, String[] args) throws UsageException {
        if (args.length == 0) return deviceName(context);
        String name = args[0].toLowerCase(Locale.US);
        if (!name.matches("[a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?")) {
            throw new UsageException("a hostname has letters, digits and dashes, and no dash at either end");
        }
        Settings.Global.putString(context.getContentResolver(), Settings.Global.DEVICE_NAME, name);
        return name + ". The router sees it the next time the Show joins Wi-Fi.";
    }

    private static String deviceName(Context context) {
        String name = Settings.Global.getString(context.getContentResolver(), Settings.Global.DEVICE_NAME);
        return name == null ? "" : name;
    }

    // System updates

    private static String update(Context context, String[] args) throws UsageException {
        SystemUpdate update = SystemUpdate.get(context);
        switch (arg(args, 0)) {
            case "":
            case "status":
                return update.status();
            case "check":
                return update.check() ? "checking" : "busy: " + update.summary();
            case "install":
                return update.install() ? "installing " + update.summary() : "nothing to install: " + update.summary();
            default:
                throw new UsageException("usage: update [status|check|install]");
        }
    }
}
