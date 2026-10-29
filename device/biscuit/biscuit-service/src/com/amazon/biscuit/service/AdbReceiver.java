/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package com.amazon.biscuit.service;

import android.adb.ADBRootService;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

import me.jxl.echo.service.AdbKeys;

/**
 * The adb switches a Dot has no screen to reach: biscuit_service adb root,
 * adb key add and adb secure.
 *
 * Only the system may change them, which BiscuitService runs as. The manifest
 * only lets senders holding WRITE_SECURE_SETTINGS through: adb, root and the
 * system.
 */
public final class AdbReceiver extends BroadcastReceiver {
    static final String ACTION_ROOT = "com.amazon.biscuit.service.ADB_ROOT";
    static final String ACTION_KEY = "com.amazon.biscuit.service.ADB_KEY";
    static final String ACTION_SECURE = "com.amazon.biscuit.service.ADB_SECURE";

    @Override
    public void onReceive(Context context, Intent intent) {
        String action = intent.getAction();
        try {
            if (ACTION_ROOT.equals(action)) {
                ADBRootService root = new ADBRootService();
                if (intent.hasExtra("enabled")) {
                    root.setEnabled(intent.getBooleanExtra("enabled", false));
                }
                reply(root.getEnabled() ? "adb root allowed" : "adb root not allowed");
            } else if (ACTION_KEY.equals(action)) {
                reply(AdbKeys.add(intent.getStringExtra("key")));
            } else if (ACTION_SECURE.equals(action)) {
                reply(AdbKeys.secure(intent.getBooleanExtra("enabled", false)));
            }
        } catch (IllegalArgumentException | IllegalStateException e) {
            // biscuit_service prints the data either way, so the code is what
            // tells it the command failed.
            setResultData(e.getMessage());
            setResultCode(1);
        }
    }

    private void reply(String said) {
        setResultData(said);
        setResultCode(0);
    }
}
