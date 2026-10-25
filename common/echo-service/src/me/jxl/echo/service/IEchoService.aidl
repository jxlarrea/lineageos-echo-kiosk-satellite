/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package me.jxl.echo.service;

/**
 * What Kiosk Satellite calls on an Echo Show. Kiosk Satellite ships without
 * this interface and calls it by transaction code, so the codes stay the ones
 * the Echo Dot's IBiscuitService uses for the same calls: 8, 9 and 10.
 */
interface IEchoService {
    /** The update state as JSON, the same fields as on the Echo Dot. */
    String systemUpdateStatus() = 7;

    /** Asks GitHub for a newer release. False while a check or install runs. */
    boolean checkSystemUpdate() = 8;

    /** Downloads and installs the newest release. Only the device owner may. */
    boolean installSystemUpdate() = 9;
}
