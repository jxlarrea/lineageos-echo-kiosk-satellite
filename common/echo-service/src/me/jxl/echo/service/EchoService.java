/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package me.jxl.echo.service;

import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Binder;
import android.os.IBinder;
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
        SystemUpdate.get(this).resume();
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
        if (Settings.Secure.getString(context.getContentResolver(),
                Settings.Secure.IMMERSIVE_MODE_CONFIRMATIONS) == null) {
            Settings.Secure.putString(context.getContentResolver(),
                    Settings.Secure.IMMERSIVE_MODE_CONFIRMATIONS, "confirmed");
        }
        nameDevice(context);
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
