/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package me.jxl.echo.service;

import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.ContentResolver;
import android.content.Intent;
import android.database.ContentObserver;
import android.net.Uri;
import android.os.Binder;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.RemoteException;
import android.os.SystemProperties;
import android.provider.Settings;
import android.util.Log;

import java.io.FileDescriptor;
import java.io.PrintWriter;

/**
 * Kiosk Satellite binds this for system updates. It also puts the image's
 * defaults in place on every boot, where a SettingsProvider default cannot.
 * echo_service reaches the rest through Commands.
 */
public class EchoService extends Service {
    static final String TAG = "EchoService";

    // init copies it to the touch driver's double_tap switch
    // (init.kiosk-satellite.rc).
    private static final String DOUBLE_TAP_PROPERTY = "persist.echo.double_tap_wake";

    public static final class BootReceiver extends BroadcastReceiver {
        @Override
        public void onReceive(Context context, Intent intent) {
            context.startService(new Intent(context, EchoService.class));
        }
    }

    // EchoService runs in system_server, so its calls into the system keep
    // the calling app's identity unless it is cleared. Without that the
    // status broadcast fails and the updater stays stuck in its state.
    private final IEchoService.Stub mBinder = new IEchoService.Stub() {
        public String systemUpdateStatus() throws RemoteException {
            return SystemUpdate.get(EchoService.this).status();
        }

        public boolean checkSystemUpdate() throws RemoteException {
            final long identity = Binder.clearCallingIdentity();
            try {
                return SystemUpdate.get(EchoService.this).check();
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }

        public boolean installSystemUpdate() throws RemoteException {
            SystemUpdate update = SystemUpdate.get(EchoService.this);
            if (!update.mayInstall(Binder.getCallingUid())) throw new SecurityException("not the device owner");
            final long identity = Binder.clearCallingIdentity();
            try {
                return update.install();
            } finally {
                Binder.restoreCallingIdentity(identity);
            }
        }
    };

    @Override
    public void onCreate() {
        super.onCreate();
        applyDefaults(this);
        followDoubleTap();
        SystemUpdate.get(this).resume();
    }

    // Android's Double tap to wake setting turns the touch driver's double
    // tap on and off. The driver has it on until init copies the setting.
    private void followDoubleTap() {
        final ContentResolver resolver = getContentResolver();
        ContentObserver observer = new ContentObserver(new Handler(Looper.getMainLooper())) {
            @Override
            public void onChange(boolean selfChange) {
                boolean on = Settings.Secure.getInt(resolver, Settings.Secure.DOUBLE_TAP_TO_WAKE, 1) != 0;
                SystemProperties.set(DOUBLE_TAP_PROPERTY, on ? "1" : "0");
            }
        };
        resolver.registerContentObserver(Settings.Secure.getUriFor(Settings.Secure.DOUBLE_TAP_TO_WAKE),
                false, observer);
        observer.onChange(false);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return mBinder;
    }

    @Override
    protected void dump(FileDescriptor fd, PrintWriter pw, String[] args) {
        pw.println("System update: " + SystemUpdate.get(this).status());
    }

    // Only settings that were never set, so a change made later stays.
    static void applyDefaults(Context context) {
        // adb keys never expire: the computer that set the Show up can come
        // back months later without someone at the screen to allow it again.
        if (Settings.Global.getString(context.getContentResolver(),
                Settings.Global.ADB_ALLOWED_CONNECTION_TIME) == null) {
            Settings.Global.putLong(context.getContentResolver(),
                    Settings.Global.ADB_ALLOWED_CONNECTION_TIME, 0);
        }
        // Android's "Viewing full screen" tip would cover the kiosk the first
        // time it goes full screen, with nobody there to dismiss it.
        String confirmations = Settings.Secure.getString(context.getContentResolver(),
                Settings.Secure.IMMERSIVE_MODE_CONFIRMATIONS);
        if (confirmations == null || confirmations.isEmpty()) {
            Settings.Secure.putString(context.getContentResolver(),
                    Settings.Secure.IMMERSIVE_MODE_CONFIRMATIONS, "confirmed");
        }
        // The screen goes on the press the privacy chip leaves alone. The
        // Echo Show 5's chip turns privacy on and off on any press shorter
        // than about 3 seconds, so holding the button turns the screen on and
        // off there. The Echo Show 8's chip does it on a hold, so a short
        // press turns the screen on and off and Android does nothing on a
        // hold (ro.echo.button_long_press 8). echo_service button changes it.
        if (Settings.Global.getString(context.getContentResolver(),
                Settings.Global.POWER_BUTTON_LONG_PRESS) == null) {
            Settings.Global.putInt(context.getContentResolver(), Settings.Global.POWER_BUTTON_LONG_PRESS,
                    SystemProperties.getInt("ro.echo.button_long_press", Commands.LONG_PRESS_POWER_SCREEN));
        }
        // A screen that is off stays dark, with no clock on it. echo_service
        // screen ambient on brings LineageOS's ambient display back.
        for (String key : new String[] { Settings.Secure.DOZE_ALWAYS_ON, Settings.Secure.DOZE_ENABLED }) {
            if (Settings.Secure.getString(context.getContentResolver(), key) == null) {
                Settings.Secure.putInt(context.getContentResolver(), key, 0);
            }
        }
        hideTrustNotifications(context);
        nameDevice(context);
    }

    // LineageOS's Trust interface posts "Discover Trust" at every boot until
    // someone opens it, and dismissing it does not count, then a warning for
    // SELinux not enforcing and one for the public test keys. Nobody reads
    // them on a kiosk. A fresh install already has both settings from the
    // image's defaults (overlay/lineage-sdk). This covers a settings database
    // from before them: the introduction counts as seen, and the warnings go
    // off only while unset, so turning them on in Settings > Trust stays. Both
    // live in LineageOS's own settings provider, reached through its calls,
    // as LineageSettings does.
    private static void hideTrustNotifications(Context context) {
        final Uri lineageSettings = Uri.parse("content://lineagesettings");
        try {
            Bundle hinted = context.getContentResolver().call(lineageSettings, "GET_system",
                    "trust_interface_hinted", null);
            if (hinted == null || !"1".equals(hinted.getString(Settings.NameValueTable.VALUE))) {
                Bundle value = new Bundle();
                value.putString(Settings.NameValueTable.VALUE, "1");
                context.getContentResolver().call(lineageSettings, "PUT_system",
                        "trust_interface_hinted", value);
            }
            Bundle warnings = context.getContentResolver().call(lineageSettings, "GET_secure",
                    "trust_warnings", null);
            if (warnings == null || warnings.getString(Settings.NameValueTable.VALUE) == null) {
                Bundle value = new Bundle();
                value.putString(Settings.NameValueTable.VALUE, "0");
                context.getContentResolver().call(lineageSettings, "PUT_secure",
                        "trust_warnings", value);
            }
        } catch (RuntimeException e) {
            Log.w(TAG, "cannot turn off LineageOS's Trust notifications", e);
        }
    }

    // Names the Show after ro.echo.hostname and the last three characters of
    // its serial, echo-show-5-ks-bqf for example, so each one shows up under
    // its own name on the network: Android sends the device name as the DHCP
    // hostname (rro/NetworkStack). Only the default name, the model, is
    // replaced.
    private static void nameDevice(Context context) {
        String prefix = SystemProperties.get("ro.echo.hostname", "");
        String serial = SystemProperties.get("ro.serialno", "").toLowerCase().replaceAll("[^a-z0-9]", "");
        if (prefix.isEmpty() || serial.length() < 3) return;
        String current = Settings.Global.getString(context.getContentResolver(), Settings.Global.DEVICE_NAME);
        String model = SystemProperties.get("ro.product.model", "");
        if (current != null && !current.isEmpty() && !current.equals(model)) return;
        String name = prefix + "-" + serial.substring(serial.length() - 3);
        Settings.Global.putString(context.getContentResolver(), Settings.Global.DEVICE_NAME, name);
        Log.i(TAG, "named the device " + name);
    }
}
