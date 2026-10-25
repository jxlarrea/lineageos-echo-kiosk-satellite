/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package com.amazon.biscuit.service;

import android.adb.ADBRootService;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/**
 * biscuit_service adb root on|off: LineageOS's Rooted debugging switch,
 * which a Dot has no screen to reach. Only the system may change it, which
 * BiscuitService runs as. The manifest only lets senders holding
 * WRITE_SECURE_SETTINGS through: adb, root and the system.
 */
public final class AdbRootReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        ADBRootService root = new ADBRootService();
        if (intent.hasExtra("enabled")) root.setEnabled(intent.getBooleanExtra("enabled", false));
        setResultData(root.getEnabled() ? "adb root allowed" : "adb root not allowed");
    }
}
