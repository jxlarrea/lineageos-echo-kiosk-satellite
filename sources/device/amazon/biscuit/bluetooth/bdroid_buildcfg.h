/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _BDROID_BUILDCFG_H
#define _BDROID_BUILDCFG_H

#define BTM_DEF_LOCAL_NAME "Echo Dot"

/* The A2DP sink's service record, as on the Echo Shows. Without it phones
 * pair but never see an audio sink to play to. */
#define BTA_AV_SINK_INCLUDED TRUE

/* A loudspeaker that renders audio, so phones list the Dot with speakers. */
#define BTA_DM_COD {0x24, 0x04, 0x14}

/* No screen and no keypad: phones pair without a code to compare. */
#define BTM_LOCAL_IO_CAPS BTM_IO_CAP_NONE

#endif
