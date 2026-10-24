/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package com.amazon.biscuit.emptylauncher;

import android.app.Activity;
import android.os.Bundle;
import android.view.View;

/** The Echo Dot's HOME: Android needs one, and the Dot has no screen. */
public final class EmptyLauncherActivity extends Activity {
    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        View black = new View(this);
        black.setBackgroundColor(0xff000000);
        setContentView(black);
    }
}
