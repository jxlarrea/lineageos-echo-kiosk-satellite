/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package me.jxl.echo.service;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.net.wifi.ScanResult;
import android.net.wifi.WifiConfiguration;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.SystemClock;
import android.util.Log;

import java.util.List;

/**
 * Moves a Show off 2.4 GHz once after boot (issue #35). Android picks an access
 * point from its first scan, which can miss a radio, and the Wi-Fi overlay
 * stops it from looking again while connected. A Show that missed the 5 GHz
 * radio of its network would stay on 2.4 GHz until the next disconnect. So
 * after the first connection of a boot, one scan looks for that radio. If
 * Android would pick it, a reconnect has Android choose again. This runs once
 * per boot and never touches a Show on 5 GHz, so it does not bring back the
 * roaming the overlay stops.
 */
final class WifiBandCheck {
    private static final String TAG = "EchoWifiBand";

    // Lets the first connection settle before the scan.
    private static final long DELAY_MS = 30000;
    private static final long SCAN_TIMEOUT_MS = 8000;
    // A second scan, since one can miss a radio.
    private static final int SCANS = 2;
    // The levels from common/rro/Wifi that Android stops counting signal at.
    private static final int GOOD_RSSI_5GHZ = -30;
    private static final int GOOD_RSSI_24GHZ = -50;
    // A weaker 5 GHz radio is not worth a reconnect.
    private static final int MINIMUM_RSSI_5GHZ = -70;

    private WifiBandCheck() {
    }

    static void start(Context context) {
        ConnectivityManager connectivity = context.getSystemService(ConnectivityManager.class);
        NetworkRequest wifi = new NetworkRequest.Builder()
                .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .build();
        connectivity.registerNetworkCallback(wifi, new ConnectivityManager.NetworkCallback() {
            @Override
            public void onAvailable(Network network) {
                connectivity.unregisterNetworkCallback(this);
                new Thread(() -> {
                    SystemClock.sleep(DELAY_MS);
                    try {
                        check(context.getSystemService(WifiManager.class));
                    } catch (RuntimeException e) {
                        Log.w(TAG, "Wi-Fi band check failed", e);
                    }
                }, "echo-wifi-band").start();
            }
        });
    }

    private static void check(WifiManager wifi) {
        WifiInfo info = wifi.getConnectionInfo();
        if (info == null || info.getNetworkId() < 0 || !is24GHz(info.getFrequency())) return;
        if (isLockedToAccessPoint(wifi, info.getNetworkId())) {
            Log.i(TAG, "On 2.4 GHz, locked to an access point");
            return;
        }
        String ssid = info.getSSID().replace("\"", "");
        for (int i = 0; i < SCANS; i++) {
            int best5GHz = Integer.MIN_VALUE;
            int best24GHz = info.getRssi();
            for (ScanResult result : Commands.scanFor(wifi, ssid, SCAN_TIMEOUT_MS)) {
                if (is24GHz(result.frequency)) {
                    best24GHz = Math.max(best24GHz, result.level);
                } else {
                    best5GHz = Math.max(best5GHz, result.level);
                }
            }
            if (best5GHz == Integer.MIN_VALUE) continue;
            if (!androidPrefers5GHz(best5GHz, best24GHz)) {
                Log.i(TAG, "Staying on 2.4 GHz at " + best24GHz + " dBm, 5 GHz is at " + best5GHz + " dBm");
                return;
            }
            Log.i(TAG, "Moving off 2.4 GHz at " + best24GHz + " dBm, 5 GHz is at " + best5GHz + " dBm");
            wifi.disconnect();
            wifi.reconnect();
            return;
        }
        Log.i(TAG, "On 2.4 GHz, no 5 GHz radio found");
    }

    private static boolean is24GHz(int frequency) {
        return frequency < 3000;
    }

    private static boolean isLockedToAccessPoint(WifiManager wifi, int networkId) {
        List<WifiConfiguration> saved = wifi.getConfiguredNetworks();
        for (int i = 0; saved != null && i < saved.size(); i++) {
            if (saved.get(i).networkId == networkId) return saved.get(i).BSSID != null;
        }
        return false;
    }

    // The comparison Android's network selection makes with the overlay's
    // levels, without which the reconnect would land on 2.4 GHz again.
    private static boolean androidPrefers5GHz(int rssi5GHz, int rssi24GHz) {
        return rssi5GHz >= MINIMUM_RSSI_5GHZ
                && Math.min(rssi5GHz, GOOD_RSSI_5GHZ) > Math.min(rssi24GHz, GOOD_RSSI_24GHZ);
    }
}
