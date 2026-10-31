/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package me.jxl.echo.service;

import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.os.SystemProperties;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;

/**
 * The privacy chip, which turns privacy on and off on the button. Its driver
 * also turns privacy on, never off, when 1 is written to its enable file,
 * which init gives to system (init.kiosk-satellite.rc). So nothing here can
 * turn a microphone on.
 *
 * With persist.echo.privacy_timeout set to a number of seconds, EchoService
 * turns privacy back on once the microphones have been on that long. The
 * chip sends no event when its state changes, so its state is read every 2
 * seconds while a timeout is set.
 */
final class Privacy {
    private static final String TAG = "EchoPrivacy";

    static final String TIMEOUT_PROPERTY = "persist.echo.privacy_timeout";
    // gpio-privacy on the Echo Show 5, amazon-gating on the Echo Show 8.
    private static final String[] CHIPS = {
        "/sys/devices/platform/gpio-privacy",
        "/sys/devices/platform/amazon-gating",
    };
    private static final long POLL_MS = 2000;

    private static Privacy sInstance;

    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private final String mChip;
    private int mTimeout;
    // When the microphones came on, in elapsedRealtime, or -1.
    private long mOnSince = -1;

    private final Runnable mPoll = new Runnable() {
        @Override
        public void run() {
            if (mTimeout <= 0) {
                mOnSince = -1;
                return;
            }
            long now = SystemClock.elapsedRealtime();
            if (state() != 0) {
                mOnSince = -1;
            } else if (mOnSince < 0) {
                mOnSince = now;
            } else if (now - mOnSince >= mTimeout * 1000L) {
                Log.i(TAG, "the microphones were on for " + mTimeout + " s, turning privacy on");
                if (turnOn()) mOnSince = -1;
            }
            mHandler.postDelayed(this, POLL_MS);
        }
    };

    static synchronized Privacy get() {
        if (sInstance == null) sInstance = new Privacy();
        return sInstance;
    }

    private Privacy() {
        String chip = null;
        for (String path : CHIPS) {
            if (new File(path, "state").canRead()) {
                chip = path;
                break;
            }
        }
        mChip = chip;
    }

    /** Reads the timeout at boot. echo_service sets it through setTimeout. */
    void start() {
        if (mChip == null) return;
        follow();
    }

    private void follow() {
        int timeout = timeout();
        if (timeout == mTimeout) return;
        mTimeout = timeout;
        mHandler.removeCallbacks(mPoll);
        if (timeout > 0) {
            mPoll.run();
        } else {
            mOnSince = -1;
        }
    }

    boolean present() {
        return mChip != null;
    }

    /** 1 while privacy is on, 0 while it is off, -1 without a chip. */
    int state() {
        if (mChip == null) return -1;
        try (BufferedReader in = new BufferedReader(new FileReader(new File(mChip, "state")))) {
            String line = in.readLine();
            return "1".equals(line == null ? "" : line.trim()) ? 1 : 0;
        } catch (IOException e) {
            return -1;
        }
    }

    /** Turns privacy on. False if the chip cannot be reached. */
    boolean turnOn() {
        if (mChip == null) return false;
        try (FileWriter out = new FileWriter(new File(mChip, "enable"))) {
            out.write("1");
            return true;
        } catch (IOException e) {
            Log.w(TAG, "cannot turn privacy on", e);
            return false;
        }
    }

    static int timeout() {
        return Math.max(0, SystemProperties.getInt(TIMEOUT_PROPERTY, 0));
    }

    void setTimeout(int seconds) {
        SystemProperties.set(TIMEOUT_PROPERTY, Integer.toString(Math.max(0, seconds)));
        follow();
    }

    /** Seconds until privacy goes back on, or -1 while no countdown runs. */
    long remaining() {
        if (mTimeout <= 0 || mOnSince < 0) return -1;
        long left = mTimeout - (SystemClock.elapsedRealtime() - mOnSince) / 1000;
        return Math.max(0, left);
    }
}
