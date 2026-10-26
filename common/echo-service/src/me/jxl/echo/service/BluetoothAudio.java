/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package me.jxl.echo.service;

import android.bluetooth.BluetoothA2dp;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothClass;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.content.BroadcastReceiver;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.provider.Settings;
import android.util.Log;

/**
 * Bluetooth audio both ways, as the Echo Dot image's BiscuitService does it.
 * Android 11's Bluetooth stack runs A2DP sink, for phones that play to the
 * Show, or A2DP source, for the Show to play to headphones and speakers, not
 * both: the second to start takes over from the first. EchoService runs the
 * role asked for last and keeps it in the profiles Bluetooth leaves out,
 * which Bluetooth reads each time it starts. A fresh Show plays what phones
 * send.
 */
final class BluetoothAudio {
    private static final String TAG = "EchoBluetooth";
    private static final long CONNECT_SEARCH_MS = 60000;
    private static final long CONNECT_PAIR_MS = 60000;
    // The profile that takes audio from phones and the one that plays to
    // headphones and speakers. AVRCP stays on in both: the stack opens it
    // with every A2DP connection and aborts when its service is missing.
    private static final long SINK_ROLE_MASK = 1L << BluetoothProfile.A2DP_SINK;
    private static final long SOURCE_ROLE_MASK = 1L << BluetoothProfile.A2DP;
    // Every profile the role decides on.
    private static final long ROLE_PROFILES_MASK = SINK_ROLE_MASK | SOURCE_ROLE_MASK
            | (1L << BluetoothProfile.AVRCP) | (1L << BluetoothProfile.AVRCP_CONTROLLER);

    private static final Handler sHandler = new Handler(Looper.getMainLooper());
    private static BluetoothAudio sInstance;

    // The search for headphones. Main thread only.
    private final Context mContext;
    private boolean mConnecting;
    private BluetoothAdapter mConnectAdapter;
    private String mConnectName;
    private long mConnectDeadline;
    private BluetoothDevice mConnectDevice;
    private boolean mConnectFromBond;

    private final Runnable mConnectTimeout = () -> {
        if (mConnecting) finishConnect("timed out");
    };
    private final BroadcastReceiver mConnectReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (onConnectEvent(intent)) abortBroadcast();
        }
    };

    private BluetoothAudio(Context context) {
        mContext = context.getApplicationContext();
    }

    static synchronized BluetoothAudio get(Context context) {
        if (sInstance == null) sInstance = new BluetoothAudio(context);
        return sInstance;
    }

    /** Whether Bluetooth plays to headphones and speakers instead of taking audio from phones. */
    boolean isHeadphonesRole() {
        long disabled = Settings.Global.getLong(mContext.getContentResolver(),
                Settings.Global.BLUETOOTH_DISABLED_PROFILES, 0);
        return (disabled & SINK_ROLE_MASK) != 0;
    }

    /** Takes audio from phones, then runs [then] on the main thread once Bluetooth is on. */
    void useSpeakerRole(final Runnable then) {
        final BluetoothAdapter adapter = BluetoothAdapter.getDefaultAdapter();
        if (adapter == null) return;
        new Thread(() -> {
            if (useRole(adapter, false) && then != null) sHandler.post(then);
        }, "EchoBtRole").start();
    }

    /**
     * Plays to Bluetooth headphones or a speaker in pairing mode: the first
     * one found that plays A2DP audio, or the first whose name also has the
     * given text in it. With a name, one paired before connects without
     * pairing mode. EchoService answers the pairing itself, as it was asked
     * for from adb, then connects A2DP.
     */
    void connect(final String name) {
        // An adapter of its own: system_server's shared one searches as the
        // android package, and the devices found then go to that package only.
        BluetoothManager manager =
                mContext.createAttributionContext(TAG).getSystemService(BluetoothManager.class);
        final BluetoothAdapter adapter = manager != null ? manager.getAdapter() : null;
        if (adapter == null) return;
        new Thread(() -> {
            if (!useRole(adapter, true)) return;
            sHandler.post(() -> startConnectSearch(adapter, name));
        }, "EchoBtConnect").start();
    }

    // Runs on a worker thread and is false when Bluetooth did not come on.
    private boolean useRole(BluetoothAdapter adapter, boolean headphones) {
        ContentResolver resolver = mContext.getContentResolver();
        String saved = Settings.Global.getString(resolver, Settings.Global.BLUETOOTH_DISABLED_PROFILES);
        long disabled = Settings.Global.getLong(resolver, Settings.Global.BLUETOOTH_DISABLED_PROFILES, 0);
        long wanted = (disabled & ~ROLE_PROFILES_MASK) | (headphones ? SINK_ROLE_MASK : SOURCE_ROLE_MASK);
        if (wanted != disabled || saved == null) {
            Settings.Global.putLong(resolver, Settings.Global.BLUETOOTH_DISABLED_PROFILES, wanted);
            Log.i(TAG, headphones ? "Bluetooth now plays to headphones and speakers"
                    : "Bluetooth now plays what phones send");
            if (adapter.getState() != BluetoothAdapter.STATE_OFF) {
                adapter.disable();
                waitFor(adapter, BluetoothAdapter.STATE_OFF);
            }
        }
        if (!adapter.isEnabled()) adapter.enable();
        return waitFor(adapter, BluetoothAdapter.STATE_ON);
    }

    private static boolean waitFor(BluetoothAdapter adapter, int state) {
        for (int i = 0; i < 40 && adapter.getState() != state; i++) {
            try {
                Thread.sleep(250);
            } catch (InterruptedException e) {
                return false;
            }
        }
        return adapter.getState() == state;
    }

    private void startConnectSearch(BluetoothAdapter adapter, String name) {
        if (mConnecting) return;
        mConnecting = true;
        mConnectAdapter = adapter;
        mConnectName = name != null && name.length() > 0 ? name.toLowerCase() : null;
        mConnectDeadline = SystemClock.elapsedRealtime() + CONNECT_SEARCH_MS;
        mConnectDevice = null;
        mConnectFromBond = false;
        IntentFilter filter = new IntentFilter(BluetoothDevice.ACTION_FOUND);
        filter.addAction(BluetoothAdapter.ACTION_DISCOVERY_FINISHED);
        filter.addAction(BluetoothDevice.ACTION_PAIRING_REQUEST);
        filter.addAction(BluetoothDevice.ACTION_BOND_STATE_CHANGED);
        filter.addAction(BluetoothA2dp.ACTION_CONNECTION_STATE_CHANGED);
        // Ahead of Settings, so its pairing prompt does not cover the kiosk.
        filter.setPriority(IntentFilter.SYSTEM_HIGH_PRIORITY);
        mContext.registerReceiver(mConnectReceiver, filter);
        if (mConnectName != null) {
            for (BluetoothDevice device : adapter.getBondedDevices()) {
                String bonded = device.getName();
                BluetoothClass cls = device.getBluetoothClass();
                if (bonded == null || !bonded.toLowerCase().contains(mConnectName)) continue;
                if (cls == null || !cls.doesClassMatch(BluetoothClass.PROFILE_A2DP)) continue;
                // Headphones reconnect by themselves when they come on.
                if (device.isConnected() && adapter.getProfileConnectionState(BluetoothProfile.A2DP)
                        == BluetoothProfile.STATE_CONNECTED) {
                    finishConnect("connected");
                    return;
                }
                Log.i(TAG, "Connecting to " + bonded);
                mConnectDevice = device;
                mConnectFromBond = true;
                sHandler.postDelayed(mConnectTimeout, CONNECT_PAIR_MS);
                adapter.connectAllEnabledProfiles(device);
                return;
            }
        }
        search(adapter);
    }

    private void search(BluetoothAdapter adapter) {
        Log.i(TAG, "Looking for Bluetooth audio" + (mConnectName != null ? " named " + mConnectName : ""));
        if (adapter.isDiscovering()) adapter.cancelDiscovery();
        if (!adapter.startDiscovery()) finishConnect("could not search");
    }

    // True when the pairing request was answered here.
    private boolean onConnectEvent(Intent intent) {
        if (!mConnecting) return false;
        BluetoothAdapter adapter = mConnectAdapter;
        BluetoothDevice device = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
        String action = intent.getAction();
        if (BluetoothA2dp.ACTION_CONNECTION_STATE_CHANGED.equals(action)
                && intent.getIntExtra(BluetoothA2dp.EXTRA_STATE, -1) == BluetoothA2dp.STATE_CONNECTED
                && isWanted(device)) {
            // Headphones paired before reconnect by themselves once Bluetooth
            // is back on, which can beat the connection asked for here.
            finishConnect("connected");
        } else if (BluetoothDevice.ACTION_FOUND.equals(action)) {
            if (mConnectDevice != null || device == null) return false;
            BluetoothClass cls = intent.getParcelableExtra(BluetoothDevice.EXTRA_CLASS);
            // Not just any audio and video device: a TV box is one too.
            if (cls == null || !cls.doesClassMatch(BluetoothClass.PROFILE_A2DP)) return false;
            String found = intent.getStringExtra(BluetoothDevice.EXTRA_NAME);
            if (found == null) found = device.getName();
            if (mConnectName != null && (found == null || !found.toLowerCase().contains(mConnectName))) {
                return false;
            }
            Log.i(TAG, "Pairing with " + found);
            mConnectDevice = device;
            adapter.cancelDiscovery();
            sHandler.postDelayed(mConnectTimeout, CONNECT_PAIR_MS);
            // Headphones that forgot an earlier pairing turn the old key
            // down, and the stack pairs again, which ends up here as well.
            if (device.getBondState() == BluetoothDevice.BOND_BONDED) {
                adapter.connectAllEnabledProfiles(device);
            } else if (!device.createBond()) {
                finishConnect("could not pair with " + found);
            }
        } else if (BluetoothAdapter.ACTION_DISCOVERY_FINISHED.equals(action)) {
            // A search lasts about 12 seconds. Search again until one turns up.
            if (mConnectDevice != null) return false;
            if (SystemClock.elapsedRealtime() < mConnectDeadline && adapter.startDiscovery()) return false;
            finishConnect("found none");
        } else if (device == null || !device.equals(mConnectDevice)) {
            return false;
        } else if (BluetoothDevice.ACTION_PAIRING_REQUEST.equals(action)) {
            int variant = intent.getIntExtra(BluetoothDevice.EXTRA_PAIRING_VARIANT, -1);
            if (variant == BluetoothDevice.PAIRING_VARIANT_PIN) {
                // What headphones without a keypad use.
                device.setPin("0000".getBytes());
            } else {
                device.setPairingConfirmation(true);
            }
            return true;
        } else if (BluetoothDevice.ACTION_BOND_STATE_CHANGED.equals(action)) {
            int state = intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.ERROR);
            int previous = intent.getIntExtra(BluetoothDevice.EXTRA_PREVIOUS_BOND_STATE, BluetoothDevice.ERROR);
            if (state == BluetoothDevice.BOND_BONDED) {
                Log.i(TAG, "Paired, connecting");
                adapter.connectAllEnabledProfiles(device);
            } else if (state == BluetoothDevice.BOND_NONE && previous == BluetoothDevice.BOND_BONDING) {
                finishConnect("pairing failed");
            }
        } else if (BluetoothA2dp.ACTION_CONNECTION_STATE_CHANGED.equals(action)) {
            int state = intent.getIntExtra(BluetoothA2dp.EXTRA_STATE, -1);
            if (state == BluetoothA2dp.STATE_CONNECTED) {
                finishConnect("connected");
            } else if (state == BluetoothA2dp.STATE_DISCONNECTED && mConnectFromBond) {
                // Off or out of range: look for it in pairing mode instead.
                mConnectFromBond = false;
                mConnectDevice = null;
                sHandler.removeCallbacks(mConnectTimeout);
                mConnectDeadline = SystemClock.elapsedRealtime() + CONNECT_SEARCH_MS;
                search(adapter);
            }
        }
        return false;
    }

    // The device being connected, or while none is, any whose name has the
    // text asked for.
    private boolean isWanted(BluetoothDevice device) {
        if (device == null) return false;
        if (mConnectDevice != null) return device.equals(mConnectDevice);
        String name = device.getName();
        return mConnectName == null || (name != null && name.toLowerCase().contains(mConnectName));
    }

    private void finishConnect(String result) {
        Log.i(TAG, "Bluetooth audio: " + result);
        mContext.unregisterReceiver(mConnectReceiver);
        sHandler.removeCallbacks(mConnectTimeout);
        mConnecting = false;
        mConnectAdapter = null;
        mConnectDevice = null;
    }
}
