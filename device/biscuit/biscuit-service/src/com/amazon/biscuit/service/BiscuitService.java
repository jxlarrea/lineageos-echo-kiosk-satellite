/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package com.amazon.biscuit.service;

import android.app.Service;
import android.bluetooth.BluetoothA2dp;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothClass;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.content.ContentResolver;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.media.AudioManager;
import android.net.LocalSocket;
import android.net.wifi.WifiConfiguration;
import android.net.wifi.WifiManager;
import android.net.LocalSocketAddress;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.RemoteException;
import android.os.SystemClock;
import android.os.UserHandle;
import android.provider.Settings;
import android.util.Slog;

import java.io.BufferedReader;
import java.io.FileDescriptor;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.util.List;

/**
 * The Echo Dot's service for Kiosk Satellite: the LED ring through
 * biscuit-ledd, the buttons (KeyHandler), volume, microphone mute, Bluetooth
 * pairing and Wi-Fi, through IBiscuitService and the intents biscuit_service
 * sends. It runs inside system_server and stays started from boot.
 */
public final class BiscuitService extends Service {
    public static final class BootReceiver extends BroadcastReceiver {
        @Override
        public void onReceive(Context context, Intent intent) {
            context.startService(new Intent(context, BiscuitService.class));
        }
    }

    // Android 11 sends these to registered receivers only.
    private final BroadcastReceiver mAudioReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            AudioManager audio = (AudioManager) getSystemService(AUDIO_SERVICE);
            if (VOLUME_CHANGED.equals(intent.getAction())) {
                int stream = intent.getIntExtra(EXTRA_STREAM_TYPE, AudioManager.STREAM_MUSIC);
                if (stream != AudioManager.STREAM_MUSIC) return;
                int current = intent.getIntExtra(EXTRA_STREAM_VALUE, audio.getStreamVolume(stream));
                try {
                    sendOk("VOLUME " + current + " " + audio.getStreamMaxVolume(stream));
                } catch (RemoteException ignored) { }
            } else if (AudioManager.ACTION_MICROPHONE_MUTE_CHANGED.equals(intent.getAction())) {
                mMicMuted = audio.isMicrophoneMute();
                announceMicMuted(mMicMuted);
            }
        }
    };

    private static final String SOCKET = "biscuit-ledd";
    private static final String VOLUME_CHANGED = "android.media.VOLUME_CHANGED_ACTION";
    private static final String MIC_MUTE_CHANGED = "com.amazon.biscuit.service.MICROPHONE_MUTE_CHANGED";
    public static final String BLUETOOTH_PAIRING_MODE = "com.amazon.biscuit.service.BLUETOOTH_PAIRING_MODE";
    public static final String BLUETOOTH_OFF = "com.amazon.biscuit.service.BLUETOOTH_OFF";
    public static final String BLUETOOTH_CONNECT = "com.amazon.biscuit.service.BLUETOOTH_CONNECT";
    public static final String WIFI_ON = "com.amazon.biscuit.service.WIFI_ON";
    public static final String WIFI_OFF = "com.amazon.biscuit.service.WIFI_OFF";
    public static final String WIFI_CONNECT = "com.amazon.biscuit.service.WIFI_CONNECT";
    public static final String VOLUME_UP = "com.amazon.biscuit.service.VOLUME_UP";
    public static final String VOLUME_DOWN = "com.amazon.biscuit.service.VOLUME_DOWN";
    public static final String VOLUME_SET = "com.amazon.biscuit.service.VOLUME_SET";
    public static final String MIC_MUTE_ON = "com.amazon.biscuit.service.MICROPHONE_MUTE_ON";
    public static final String MIC_MUTE_OFF = "com.amazon.biscuit.service.MICROPHONE_MUTE_OFF";
    public static final String MIC_MUTE_TOGGLE = "com.amazon.biscuit.service.MICROPHONE_MUTE_TOGGLE";
    public static final String COUNTDOWN_PROGRESS = "com.amazon.biscuit.service.COUNTDOWN_PROGRESS";
    public static final String COUNTDOWN_CLEAR = "com.amazon.biscuit.service.COUNTDOWN_CLEAR";
    private static final String EXTRA_SSID = "ssid";
    private static final String EXTRA_PSK = "psk";
    private static final String EXTRA_VOLUME = "volume";
    private static final String EXTRA_NAME = "name";
    private static final String EXTRA_COUNTDOWN_REMAINING_MS = "com.amazon.biscuit.service.EXTRA_COUNTDOWN_REMAINING_MS";
    private static final String EXTRA_COUNTDOWN_TOTAL_MS = "com.amazon.biscuit.service.EXTRA_COUNTDOWN_TOTAL_MS";
    private static final String EXTRA_STREAM_TYPE = "android.media.EXTRA_VOLUME_STREAM_TYPE";
    private static final String EXTRA_STREAM_VALUE = "android.media.EXTRA_VOLUME_STREAM_VALUE";
    private static final String EXTRA_MICROPHONE_MUTED = "com.amazon.biscuit.service.EXTRA_MICROPHONE_MUTED";
    private static final String EXTRA_FROM_BUTTON = "com.amazon.biscuit.service.EXTRA_FROM_BUTTON";
    private static final String VOLUME_BUTTON = "com.amazon.biscuit.service.VOLUME_BUTTON";
    private static final String EXTRA_DIRECTION = "direction";
    private static final int PAIRING_MODE_SECONDS = 300;
    private static final long CONNECT_SEARCH_MS = 60000;
    private static final long CONNECT_PAIR_MS = 60000;
    private static final String TAG = "BiscuitService";
    // The profile that takes audio from phones and the one that plays to
    // headphones and speakers. AVRCP stays on in both: the stack opens it
    // with every A2DP connection and aborts when its service is missing.
    private static final long SINK_ROLE_MASK = 1L << BluetoothProfile.A2DP_SINK;
    private static final long SOURCE_ROLE_MASK = 1L << BluetoothProfile.A2DP;
    // Every profile the role decides on. AVRCP is among them so that a Dot
    // which once left it out turns it back on.
    private static final long ROLE_PROFILES_MASK = SINK_ROLE_MASK | SOURCE_ROLE_MASK
            | (1L << BluetoothProfile.AVRCP) | (1L << BluetoothProfile.AVRCP_CONTROLLER);
    private static final String SYSTEM_UPDATE_UNAVAILABLE =
            "{\"available\":false,\"state\":\"idle\","
            + "\"checkError\":\"System updates are not available on this image yet.\"}";
    private final Object mLock = new Object();
    private volatile boolean mMicMuted;

    // A search for Bluetooth headphones or a speaker to play to. Only the
    // main thread touches these.
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private boolean mConnecting;
    private BluetoothAdapter mConnectAdapter;
    private String mConnectName;
    private long mConnectDeadline;
    private BluetoothDevice mConnectDevice;
    private boolean mConnectFromBond;

    // While pairing mode lasts, the pairing a phone asks for, which a Dot
    // has no screen to confirm on. Main thread only.
    private boolean mAcceptingPairing;
    private final BroadcastReceiver mPairingReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            BluetoothDevice device = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
            if (device == null) return;
            Slog.i(TAG, "Accepting pairing from " + device.getName());
            answerPairingRequest(device, intent);
            abortBroadcast();
        }
    };
    private final Runnable mStopAcceptingPairing = new Runnable() {
        public void run() {
            stopAcceptingPairing();
        }
    };
    private final Runnable mConnectTimeout = new Runnable() {
        public void run() {
            if (mConnecting) finishConnect("timed out");
        }
    };
    private final BroadcastReceiver mConnectReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (onConnectEvent(intent)) abortBroadcast();
        }
    };

    // The app holding the volume buttons, if any, and the token whose death
    // gives them back, so a crashed app never leaves the buttons dead.
    // Its own lock: mLock is held across slow daemon calls.
    private final Object mClaimLock = new Object();
    private IBinder mVolumeClaim;
    private String mVolumeClaimPackage;
    private final IBinder.DeathRecipient mVolumeClaimDeath = new IBinder.DeathRecipient() {
        public void binderDied() {
            synchronized (mClaimLock) {
                mVolumeClaim = null;
                mVolumeClaimPackage = null;
            }
        }
    };

    private final IBiscuitService.Stub mBinder = new IBiscuitService.Stub() {
        public boolean play(String name) throws RemoteException {
            checkName(name);
            return sendOk("PLAY " + name);
        }

        public boolean setFrame(String rgbHex72) throws RemoteException {
            checkFrame(rgbHex72);
            return sendOk("FRAME " + rgbHex72);
        }

        public boolean setVolume(int current, int max) throws RemoteException {
            if (current < 0 || max <= 0 || current > max) throw new RemoteException("bad volume");
            return sendOk("VOLUME " + current + " " + max);
        }

        public boolean setMicMuted(boolean muted) throws RemoteException {
            setRealMicMuted(muted);
            return true;
        }

        public boolean clear() throws RemoteException {
            return sendOk("OFF");
        }

        public String status() throws RemoteException {
            return send("STATUS");
        }

        public boolean claimVolumeButtons(IBinder token, boolean claimed) throws RemoteException {
            if (token == null) throw new RemoteException("missing token");
            String[] packages = getPackageManager().getPackagesForUid(Binder.getCallingUid());
            if (packages == null || packages.length == 0) return false;
            synchronized (mClaimLock) {
                if (!claimed) {
                    if (token != mVolumeClaim) return false;
                    releaseVolumeClaim();
                    return true;
                }
                if (token == mVolumeClaim) return true;
                releaseVolumeClaim();
                token.linkToDeath(mVolumeClaimDeath, 0);
                mVolumeClaim = token;
                mVolumeClaimPackage = packages[0];
                return true;
            }
        }

        // System updates are not ported to this image yet.
        public String systemUpdateStatus() throws RemoteException {
            return SYSTEM_UPDATE_UNAVAILABLE;
        }

        public boolean checkSystemUpdate() throws RemoteException {
            return false;
        }

        public boolean installSystemUpdate() throws RemoteException {
            return false;
        }
    };

    // Callers hold mClaimLock.
    private void releaseVolumeClaim() {
        if (mVolumeClaim != null) {
            try {
                mVolumeClaim.unlinkToDeath(mVolumeClaimDeath, 0);
            } catch (RuntimeException ignored) { }
        }
        mVolumeClaim = null;
        mVolumeClaimPackage = null;
    }

    // A volume button press goes to the app that claimed the buttons.
    // False when nothing holds them, so the press changes the volume.
    private boolean forwardVolumeButton(Intent intent, String direction) {
        if (!intent.getBooleanExtra(EXTRA_FROM_BUTTON, false)) return false;
        String target;
        synchronized (mClaimLock) {
            target = mVolumeClaimPackage;
        }
        if (target == null) return false;
        Intent press = new Intent(VOLUME_BUTTON);
        press.setPackage(target);
        press.addFlags(Intent.FLAG_RECEIVER_FOREGROUND | Intent.FLAG_RECEIVER_REGISTERED_ONLY);
        press.putExtra(EXTRA_DIRECTION, direction);
        sendBroadcast(press);
        return true;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        mMicMuted = ((AudioManager) getSystemService(AUDIO_SERVICE)).isMicrophoneMute();
        IntentFilter filter = new IntentFilter(VOLUME_CHANGED);
        filter.addAction(AudioManager.ACTION_MICROPHONE_MUTE_CHANGED);
        registerReceiver(mAudioReceiver, filter);
        startInBluetoothSpeakerRole();
    }

    // A Dot that never chose takes audio from phones, which is what pairing
    // mode is for. Bluetooth is already up by now, so it restarts once.
    private void startInBluetoothSpeakerRole() {
        if (Settings.Global.getString(getContentResolver(),
                Settings.Global.BLUETOOTH_DISABLED_PROFILES) != null) {
            return;
        }
        final BluetoothAdapter adapter = BluetoothAdapter.getDefaultAdapter();
        if (adapter == null) return;
        new Thread(new Runnable() {
            public void run() {
                useBluetoothRole(adapter, false, adapter.isEnabled());
            }
        }, "BiscuitBtRole").start();
    }

    @Override
    public void onDestroy() {
        unregisterReceiver(mAudioReceiver);
        super.onDestroy();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && MIC_MUTE_TOGGLE.equals(intent.getAction())) {
            setRealMicMuted(!mMicMuted);
        } else if (intent != null && BLUETOOTH_PAIRING_MODE.equals(intent.getAction())) {
            startBluetoothPairingMode();
        } else if (intent != null && BLUETOOTH_OFF.equals(intent.getAction())) {
            stopBluetooth();
        } else if (intent != null && BLUETOOTH_CONNECT.equals(intent.getAction())) {
            connectBluetoothAudio(intent.getStringExtra(EXTRA_NAME));
        } else if (intent != null && WIFI_ON.equals(intent.getAction())) {
            setWifiEnabled(true);
        } else if (intent != null && WIFI_OFF.equals(intent.getAction())) {
            setWifiEnabled(false);
        } else if (intent != null && WIFI_CONNECT.equals(intent.getAction())) {
            connectWifi(intent.getStringExtra(EXTRA_SSID), intent.getStringExtra(EXTRA_PSK));
        } else if (intent != null && VOLUME_UP.equals(intent.getAction())) {
            if (!forwardVolumeButton(intent, "up")) adjustVolume(AudioManager.ADJUST_RAISE);
        } else if (intent != null && VOLUME_DOWN.equals(intent.getAction())) {
            if (!forwardVolumeButton(intent, "down")) adjustVolume(AudioManager.ADJUST_LOWER);
        } else if (intent != null && VOLUME_SET.equals(intent.getAction())) {
            setVolume(intent.getIntExtra(EXTRA_VOLUME, -1));
        } else if (intent != null && COUNTDOWN_PROGRESS.equals(intent.getAction())) {
            setCountdown(intent.getLongExtra(EXTRA_COUNTDOWN_REMAINING_MS, -1),
                    intent.getLongExtra(EXTRA_COUNTDOWN_TOTAL_MS, -1));
        } else if (intent != null && COUNTDOWN_CLEAR.equals(intent.getAction())) {
            clearCountdown();
        } else if (intent != null && MIC_MUTE_ON.equals(intent.getAction())) {
            setRealMicMuted(true);
        } else if (intent != null && MIC_MUTE_OFF.equals(intent.getAction())) {
            setRealMicMuted(false);
        }
        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return mBinder;
    }

    // dumpsys activity service com.amazon.biscuit.service/.BiscuitService
    @Override
    protected void dump(FileDescriptor fd, PrintWriter writer, String[] args) {
        writer.println("Microphone muted: " + mMicMuted);
        synchronized (mClaimLock) {
            writer.println("Volume buttons claimed by: "
                    + (mVolumeClaimPackage != null ? mVolumeClaimPackage : "nobody"));
        }
        writer.println("System update: " + SYSTEM_UPDATE_UNAVAILABLE);
    }

    // Kiosk Satellite listens for this one, as the CM14 image sent it.
    private void announceMicMuted(boolean muted) {
        Intent intent = new Intent(MIC_MUTE_CHANGED);
        intent.putExtra(EXTRA_MICROPHONE_MUTED, muted);
        intent.addFlags(Intent.FLAG_RECEIVER_REGISTERED_ONLY_BEFORE_BOOT
                | Intent.FLAG_RECEIVER_REPLACE_PENDING);
        sendStickyBroadcastAsUser(intent, UserHandle.ALL);
    }

    private static void checkName(String name) throws RemoteException {
        if (name == null || name.length() < 1 || name.length() > 64) throw new RemoteException("bad name");
        for (int i = 0; i < name.length(); i++) {
            char c = name.charAt(i);
            boolean ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                    || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
            if (!ok) throw new RemoteException("bad name");
        }
        if (name.indexOf("..") >= 0) throw new RemoteException("bad name");
    }

    private static void checkFrame(String frame) throws RemoteException {
        if (frame == null || frame.length() != 72) throw new RemoteException("bad frame");
        for (int i = 0; i < frame.length(); i++) {
            char c = frame.charAt(i);
            boolean ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            if (!ok) throw new RemoteException("bad frame");
        }
    }

    private void setRealMicMuted(boolean muted) {
        ((AudioManager) getSystemService(AUDIO_SERVICE)).setMicrophoneMute(muted);
        mMicMuted = muted;
    }

    private void updateVolumeLed(AudioManager audio) {
        try {
            sendOk("VOLUME " + audio.getStreamVolume(AudioManager.STREAM_MUSIC)
                    + " " + audio.getStreamMaxVolume(AudioManager.STREAM_MUSIC));
        } catch (RemoteException ignored) { }
    }

    private void adjustVolume(int direction) {
        AudioManager audio = (AudioManager) getSystemService(AUDIO_SERVICE);
        audio.adjustStreamVolume(AudioManager.STREAM_MUSIC, direction, 0);
        updateVolumeLed(audio);
    }

    private void setVolume(int volume) {
        AudioManager audio = (AudioManager) getSystemService(AUDIO_SERVICE);
        int max = audio.getStreamMaxVolume(AudioManager.STREAM_MUSIC);
        if (volume < 0) volume = 0;
        if (volume > max) volume = max;
        audio.setStreamVolume(AudioManager.STREAM_MUSIC, volume, 0);
        updateVolumeLed(audio);
    }

    private void setCountdown(long remainingMs, long totalMs) {
        if (totalMs <= 0 || remainingMs < 0 || remainingMs > totalMs) return;
        if (remainingMs == 0) {
            clearCountdown();
            return;
        }
        try {
            sendOk("COUNTDOWN " + remainingMs + " " + totalMs);
        } catch (RemoteException ignored) { }
    }

    private void clearCountdown() {
        try {
            sendOk("CLEAR");
        } catch (RemoteException ignored) { }
    }

    private void startBluetoothPairingMode() {
        final BluetoothAdapter adapter = BluetoothAdapter.getDefaultAdapter();
        if (adapter == null) return;
        new Thread(new Runnable() {
            public void run() {
                if (!useBluetoothRole(adapter, false)) return;
                adapter.setScanMode(BluetoothAdapter.SCAN_MODE_CONNECTABLE_DISCOVERABLE,
                        PAIRING_MODE_SECONDS);
                mHandler.post(new Runnable() {
                    public void run() {
                        startAcceptingPairing();
                    }
                });
            }
        }, "BiscuitBtPairingMode").start();
    }

    private void startAcceptingPairing() {
        if (!mAcceptingPairing) {
            IntentFilter filter = new IntentFilter(BluetoothDevice.ACTION_PAIRING_REQUEST);
            // Ahead of Settings, whose pairing prompt nobody could answer.
            filter.setPriority(IntentFilter.SYSTEM_HIGH_PRIORITY);
            registerReceiver(mPairingReceiver, filter);
            mAcceptingPairing = true;
        }
        mHandler.removeCallbacks(mStopAcceptingPairing);
        mHandler.postDelayed(mStopAcceptingPairing, PAIRING_MODE_SECONDS * 1000L);
    }

    private void stopAcceptingPairing() {
        mHandler.removeCallbacks(mStopAcceptingPairing);
        if (!mAcceptingPairing) return;
        unregisterReceiver(mPairingReceiver);
        mAcceptingPairing = false;
    }

    private static void answerPairingRequest(BluetoothDevice device, Intent request) {
        int variant = request.getIntExtra(BluetoothDevice.EXTRA_PAIRING_VARIANT, -1);
        if (variant == BluetoothDevice.PAIRING_VARIANT_PIN) {
            // What headphones and old phones without a keypad on the other
            // end use.
            device.setPin("0000".getBytes());
        } else {
            device.setPairingConfirmation(true);
        }
    }

    // Android 11's Bluetooth stack runs A2DP sink, for phones that play to
    // the Dot, or A2DP source, for the Dot to play to headphones, not both:
    // the second to start takes over from the first. A phone that sees
    // both roles also takes the Dot for a phone. The Dot runs the role asked
    // for last and keeps the choice in the profiles Bluetooth leaves out,
    // which it reads each time it starts. Runs on a worker thread and is
    // false when Bluetooth did not come on.
    private boolean useBluetoothRole(BluetoothAdapter adapter, boolean source) {
        return useBluetoothRole(adapter, source, true);
    }

    private boolean useBluetoothRole(BluetoothAdapter adapter, boolean source, boolean enable) {
        ContentResolver resolver = getContentResolver();
        String saved = Settings.Global.getString(resolver,
                Settings.Global.BLUETOOTH_DISABLED_PROFILES);
        long disabled = Settings.Global.getLong(resolver,
                Settings.Global.BLUETOOTH_DISABLED_PROFILES, 0);
        long wanted = (disabled & ~ROLE_PROFILES_MASK)
                | (source ? SINK_ROLE_MASK : SOURCE_ROLE_MASK);
        if (wanted != disabled || saved == null) {
            Settings.Global.putLong(resolver, Settings.Global.BLUETOOTH_DISABLED_PROFILES, wanted);
            Slog.i(TAG, source ? "Bluetooth now plays to headphones and speakers"
                    : "Bluetooth now plays what phones send");
            if (adapter.getState() != BluetoothAdapter.STATE_OFF) {
                adapter.disable();
                waitForBluetooth(adapter, BluetoothAdapter.STATE_OFF);
            }
        }
        if (!enable) return adapter.isEnabled();
        if (!adapter.isEnabled()) adapter.enable();
        return waitForBluetooth(adapter, BluetoothAdapter.STATE_ON);
    }

    private static boolean waitForBluetooth(BluetoothAdapter adapter, int state) {
        for (int i = 0; i < 40 && adapter.getState() != state; i++) {
            try { Thread.sleep(250); } catch (InterruptedException e) { return false; }
        }
        return adapter.getState() == state;
    }

    // Plays to Bluetooth headphones or a speaker in pairing mode: the first
    // one found that plays A2DP audio, or the first whose name also has the
    // given text in it. With a name, one paired before connects without
    // pairing mode. The Dot confirms the pairing itself, since it has no
    // screen to ask on, then connects A2DP.
    private void connectBluetoothAudio(final String name) {
        // An adapter of its own: system_server's shared one searches as the
        // android package, and the devices found then go to that package only.
        BluetoothManager manager =
                createAttributionContext(TAG).getSystemService(BluetoothManager.class);
        final BluetoothAdapter adapter = manager != null ? manager.getAdapter() : null;
        if (adapter == null) return;
        new Thread(new Runnable() {
            public void run() {
                if (!useBluetoothRole(adapter, true)) return;
                mHandler.post(new Runnable() {
                    public void run() {
                        startConnectSearch(adapter, name);
                    }
                });
            }
        }, "BiscuitBtConnect").start();
    }

    private void startConnectSearch(BluetoothAdapter adapter, String name) {
        if (mConnecting) return;
        stopAcceptingPairing();
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
        // Ahead of Settings, whose pairing prompt nobody could answer.
        filter.setPriority(IntentFilter.SYSTEM_HIGH_PRIORITY);
        registerReceiver(mConnectReceiver, filter);
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
                Slog.i(TAG, "Connecting to " + bonded);
                mConnectDevice = device;
                mConnectFromBond = true;
                mHandler.postDelayed(mConnectTimeout, CONNECT_PAIR_MS);
                adapter.connectAllEnabledProfiles(device);
                return;
            }
        }
        searchForBluetoothAudio(adapter);
    }

    private void searchForBluetoothAudio(BluetoothAdapter adapter) {
        Slog.i(TAG, "Looking for Bluetooth audio" + (mConnectName != null ? " named " + mConnectName : ""));
        if (adapter.isDiscovering()) adapter.cancelDiscovery();
        if (!adapter.startDiscovery()) finishConnect("could not search");
    }

    // True when the pairing request was answered here.
    private boolean onConnectEvent(Intent intent) {
        if (!mConnecting) return false;
        BluetoothAdapter adapter = mConnectAdapter;
        BluetoothDevice device = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
        String action = intent.getAction();
        if (BluetoothDevice.ACTION_FOUND.equals(action)) {
            if (mConnectDevice != null || device == null) return false;
            BluetoothClass cls = intent.getParcelableExtra(BluetoothDevice.EXTRA_CLASS);
            // Not just any audio and video device: a TV box is one too.
            if (cls == null || !cls.doesClassMatch(BluetoothClass.PROFILE_A2DP)) return false;
            String found = intent.getStringExtra(BluetoothDevice.EXTRA_NAME);
            if (found == null) found = device.getName();
            if (mConnectName != null && (found == null || !found.toLowerCase().contains(mConnectName))) return false;
            Slog.i(TAG, "Pairing with " + found);
            mConnectDevice = device;
            adapter.cancelDiscovery();
            mHandler.postDelayed(mConnectTimeout, CONNECT_PAIR_MS);
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
        } else if (!isConnectDevice(device)) {
            return false;
        } else if (BluetoothDevice.ACTION_PAIRING_REQUEST.equals(action)) {
            answerPairingRequest(device, intent);
            return true;
        } else if (BluetoothDevice.ACTION_BOND_STATE_CHANGED.equals(action)) {
            int state = intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.ERROR);
            int previous = intent.getIntExtra(BluetoothDevice.EXTRA_PREVIOUS_BOND_STATE,
                    BluetoothDevice.ERROR);
            if (state == BluetoothDevice.BOND_BONDED) {
                Slog.i(TAG, "Paired, connecting");
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
                mHandler.removeCallbacks(mConnectTimeout);
                mConnectDeadline = SystemClock.elapsedRealtime() + CONNECT_SEARCH_MS;
                searchForBluetoothAudio(adapter);
            }
        }
        return false;
    }

    private boolean isConnectDevice(BluetoothDevice device) {
        return device != null && device.equals(mConnectDevice);
    }

    private void finishConnect(String result) {
        Slog.i(TAG, "Bluetooth audio: " + result);
        unregisterReceiver(mConnectReceiver);
        mHandler.removeCallbacks(mConnectTimeout);
        mConnecting = false;
        mConnectAdapter = null;
        mConnectDevice = null;
    }

    private void stopBluetooth() {
        BluetoothAdapter adapter = BluetoothAdapter.getDefaultAdapter();
        if (adapter != null && adapter.isEnabled()) adapter.disable();
    }

    private void setWifiEnabled(boolean enabled) {
        WifiManager wifi = (WifiManager) getSystemService(WIFI_SERVICE);
        if (wifi == null) return;
        wifi.setWifiEnabled(enabled);
        if (enabled) wifi.reconnect();
    }

    private int findNetwork(WifiManager wifi, String ssid) {
        List<WifiConfiguration> configs = wifi.getConfiguredNetworks();
        if (configs == null) return -1;
        for (WifiConfiguration config : configs) {
            if (config != null && ssid.equals(config.SSID)) return config.networkId;
        }
        return -1;
    }

    private void connectWifi(String ssid, String psk) {
        if (ssid == null || ssid.length() == 0) return;
        WifiManager wifi = (WifiManager) getSystemService(WIFI_SERVICE);
        if (wifi == null) return;
        WifiConfiguration config = new WifiConfiguration();
        config.SSID = quote(ssid);
        if (psk == null || psk.length() == 0) {
            config.allowedKeyManagement.set(WifiConfiguration.KeyMgmt.NONE);
        } else {
            config.preSharedKey = isHexPsk(psk) ? psk : quote(psk);
            config.allowedKeyManagement.set(WifiConfiguration.KeyMgmt.WPA_PSK);
        }
        wifi.setWifiEnabled(true);
        int id = findNetwork(wifi, config.SSID);
        if (id >= 0) {
            config.networkId = id;
            id = wifi.updateNetwork(config);
        } else {
            id = wifi.addNetwork(config);
        }
        if (id >= 0) {
            wifi.enableNetwork(id, true);
            wifi.saveConfiguration();
            wifi.reconnect();
        }
    }

    private static String quote(String s) {
        return "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }

    private static boolean isHexPsk(String s) {
        if (s.length() != 64) return false;
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
        }
        return true;
    }

    private boolean sendOk(String command) throws RemoteException {
        return "OK".equals(send(command));
    }

    private String send(String command) throws RemoteException {
        synchronized (mLock) {
            LocalSocket socket = new LocalSocket();
            try {
                socket.connect(new LocalSocketAddress(SOCKET, LocalSocketAddress.Namespace.RESERVED));
                OutputStream out = socket.getOutputStream();
                out.write((command + "\n").getBytes("US-ASCII"));
                out.flush();
                BufferedReader in = new BufferedReader(new InputStreamReader(socket.getInputStream(), "US-ASCII"));
                String reply = in.readLine();
                if (reply == null) throw new RemoteException("no daemon reply");
                return reply;
            } catch (IOException e) {
                throw new RemoteException(e.toString());
            } finally {
                try { socket.close(); } catch (IOException ignored) { }
            }
        }
    }
}
