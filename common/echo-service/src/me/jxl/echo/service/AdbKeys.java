/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package me.jxl.echo.service;

import android.os.SystemProperties;
import android.util.Base64;
import android.util.Log;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.ArrayList;
import java.util.List;

/**
 * Key authentication for adb, which an Echo cannot set up the usual way: the
 * prompt an unknown key needs has nowhere to appear on a device with no screen.
 *
 * A key already in adb_keys is accepted without a prompt, which is all adbd
 * looks at once auth is on, so a device can be handed one over the connection
 * it boots with and require one from then on. `adb key add` writes it and
 * `adb secure on` flips the switch, which the patch in
 * patches/common/system/core reads as persist.echo.adb_secure.
 *
 * Both services run in system_server, the process that already writes this
 * file when it authorizes a key, so neither needs a privilege for it.
 *
 * Failures are plain exceptions. Each service turns them into the reply its
 * own command line prints.
 */
public final class AdbKeys {
    private static final String TAG = "EchoAdbKeys";

    /** Read by adb/daemon/main.cpp, which requires a key when this is 1. */
    public static final String SECURE_PROPERTY = "persist.echo.adb_secure";

    /** Where adbd reads the keys it accepts, as the framework writes them. */
    private static final File KEYS = new File("/data/misc/adb/adb_keys");

    /** One Android public key, as android_pubkey_encode() lays it out. */
    private static final int PUBKEY_BYTES = 524;

    /** Enough for a few machines, and no reason to grow. */
    private static final int MAX_KEYS = 32;

    private static final int MAX_LINE_BYTES = 4096;

    private AdbKeys() {}

    /** Whether adb requires a key. */
    public static boolean isSecure() {
        return SystemProperties.getBoolean(SECURE_PROPERTY, false);
    }

    /** How many keys a connection can present. */
    public static int count() {
        return read().size();
    }

    /**
     * Adds a public key, as adbkey.pub holds it: the base64 key, then the
     * comment the machine wrote. The comment is kept, since it is what tells
     * two keys apart in the file.
     */
    public static String add(String key) {
        String trimmed = key == null ? "" : key.trim();
        if (trimmed.isEmpty()) {
            throw new IllegalArgumentException("usage: adb key add <base64-key>");
        }

        // adbd takes the first whitespace-separated word as the key, so the
        // rest only has to be a comment.
        String first = trimmed.split("[ \t]")[0];
        byte[] decoded;
        try {
            decoded = Base64.decode(first, Base64.DEFAULT);
        } catch (IllegalArgumentException e) {
            throw new IllegalArgumentException("that is not a base64 adb public key");
        }
        if (decoded.length != PUBKEY_BYTES) {
            throw new IllegalArgumentException(
                    "that key is "
                            + decoded.length
                            + " bytes, and an adb public key is "
                            + PUBKEY_BYTES);
        }

        List<String> keys = read();
        for (String existing : keys) {
            if (existing.split("[ \t]")[0].equals(first)) {
                return "that key is already installed";
            }
        }
        if (keys.size() >= MAX_KEYS) {
            throw new IllegalStateException(
                    "already " + keys.size() + " keys. Remove one from " + KEYS + " first.");
        }

        keys.add(trimmed);
        write(keys);
        Log.i(TAG, "added an adb key, " + keys.size() + " installed");
        return "key installed, " + keys.size() + " in all";
    }

    /**
     * Turns the requirement on or off. On is refused with nothing installed,
     * since that would leave the device unreachable and no way back but
     * recovery. Off only takes back what this turned on: a device that asks
     * for a key by itself, as the Echo Shows do, still does.
     */
    public static String secure(boolean on) {
        if (on) {
            int keys = count();
            if (keys == 0) {
                throw new IllegalStateException(
                        "no key is installed, so adb would stop answering. "
                                + "Install one with `adb key add` first.");
            }
            SystemProperties.set(SECURE_PROPERTY, "1");
            Log.i(TAG, "adb now requires one of " + keys + " keys");
            return "adb requires a key now. adbd restarts, so this connection drops; "
                    + "reconnect and it will use the key you installed.";
        }

        SystemProperties.set(SECURE_PROPERTY, "0");
        Log.i(TAG, "adb secure off");
        return "adb secure off. This can only add the requirement, so a device "
                + "that asked for a key by itself still does.";
    }

    /** The keys on disk, in order, blank lines and comments dropped. */
    private static List<String> read() {
        List<String> keys = new ArrayList<>();
        if (!KEYS.isFile()) {
            return keys;
        }
        try {
            for (String line : Files.readAllLines(KEYS.toPath(), StandardCharsets.UTF_8)) {
                String trimmed = line.trim();
                if (!trimmed.isEmpty() && !trimmed.startsWith("#")) {
                    keys.add(trimmed);
                }
            }
        } catch (IOException e) {
            // An unreadable file reads as no keys, which only makes `secure on`
            // refuse and `key add` write it afresh.
            Log.w(TAG, "cannot read " + KEYS + ": " + e);
        }
        return keys;
    }

    /**
     * Writes the keys back the way the framework leaves them: owned by the
     * system, readable by adbd, and not readable by anything else.
     */
    private static void write(List<String> keys) {
        File parent = KEYS.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IllegalStateException("cannot create " + parent);
        }
        StringBuilder out = new StringBuilder();
        for (String key : keys) {
            if (key.length() > MAX_LINE_BYTES) {
                throw new IllegalArgumentException("that key line is too long");
            }
            out.append(key).append('\n');
        }
        try {
            Files.write(KEYS.toPath(), out.toString().getBytes(StandardCharsets.UTF_8));
            // What adbd expects: owned by the system, which is who writes it,
            // and readable by nothing but root and the system.
            Files.setPosixFilePermissions(
                    KEYS.toPath(), PosixFilePermissions.fromString("rw-r-----"));
        } catch (IOException e) {
            throw new IllegalStateException("cannot write " + KEYS + ": " + e);
        }
    }
}
