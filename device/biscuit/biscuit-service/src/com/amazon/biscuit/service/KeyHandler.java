/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package com.amazon.biscuit.service;

import android.content.Context;
import android.content.Intent;
import android.os.UserHandle;
import android.util.Slog;
import android.view.KeyEvent;

import com.android.internal.os.DeviceKeyHandler;

/**
 * The Echo Dot's buttons, which LineageOS's PhoneWindowManager hands to this
 * class first (config_deviceKeyHandlerClasses). Volume up and down and the
 * microphone button go to BiscuitService, which changes the volume or the
 * microphone mute, or passes the volume buttons to the app that claimed
 * them. Every press and release of the Action button also goes out as a
 * broadcast, which a kiosk with no screen listens for instead of running an
 * accessibility service.
 */
public final class KeyHandler implements DeviceKeyHandler {
    private static final String PACKAGE = "com.amazon.biscuit.service";
    private static final String SERVICE = PACKAGE + ".BiscuitService";
    private static final String ACTION_BUTTON = PACKAGE + ".ACTION_BUTTON";
    private static final String EXTRA_FROM_BUTTON = PACKAGE + ".EXTRA_FROM_BUTTON";

    private static final String TAG = "BiscuitKeyHandler";

    private final Context mContext;

    // The last Action button event broadcast. PhoneWindowManager hands a key
    // this class lets through to it twice, before queueing it and again
    // before dispatching it, so each press and release would go out twice
    // and Kiosk Satellite would act on the button twice.
    private long mLastActionTime = -1;
    private int mLastAction = -1;

    public KeyHandler(Context context) {
        mContext = context;
        Slog.i(TAG, "The Echo Dot's buttons go to BiscuitService");
    }

    @Override
    public KeyEvent handleKeyEvent(KeyEvent event) {
        boolean down = event.getAction() == KeyEvent.ACTION_DOWN;
        String action;

        switch (event.getKeyCode()) {
            case KeyEvent.KEYCODE_VOLUME_UP:
                action = BiscuitService.VOLUME_UP;
                break;
            case KeyEvent.KEYCODE_VOLUME_DOWN:
                action = BiscuitService.VOLUME_DOWN;
                break;
            case KeyEvent.KEYCODE_MUTE:
                action = BiscuitService.MIC_MUTE_TOGGLE;
                break;
            case KeyEvent.KEYCODE_HELP:
                if (event.getRepeatCount() == 0 && !isRepeatCall(event)) {
                    Intent intent = new Intent(ACTION_BUTTON);
                    intent.addFlags(Intent.FLAG_RECEIVER_FOREGROUND
                            | Intent.FLAG_RECEIVER_REGISTERED_ONLY);
                    intent.putExtra("down", down);
                    intent.putExtra("eventTime", event.getEventTime());
                    mContext.sendBroadcastAsUser(intent, UserHandle.ALL);
                }
                // The key itself goes on as before.
                return event;
            default:
                return event;
        }

        if (down && event.getRepeatCount() == 0) {
            Intent intent = new Intent(action);
            intent.setClassName(PACKAGE, SERVICE);
            // Marks a real press, so BiscuitService can hand the volume
            // buttons to an app that claimed them while biscuit_service
            // volume up and down keep their job.
            intent.putExtra(EXTRA_FROM_BUTTON, true);
            try {
                mContext.startServiceAsUser(intent, UserHandle.CURRENT);
            } catch (RuntimeException e) {
                Slog.w(TAG, "Could not send " + action + " to BiscuitService", e);
            }
        }
        return null;
    }

    // The two calls come from different input threads.
    private synchronized boolean isRepeatCall(KeyEvent event) {
        if (event.getEventTime() == mLastActionTime && event.getAction() == mLastAction) return true;
        mLastActionTime = event.getEventTime();
        mLastAction = event.getAction();
        return false;
    }
}
