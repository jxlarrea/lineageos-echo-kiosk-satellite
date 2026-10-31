#
# What the Echo Show 8 (1st gen) gets on top of the amazon-oss crown tree.
# stage-tree.sh copies device/crown/ to device/kiosk-satellite/crown in the
# tree.
#

# The network name EchoService gives the Show: echo-show-8-ks and the end of
# its serial.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    ro.echo.hostname=echo-show-8-ks

# Kiosk Satellite's MIC capture stays raw: microphone 0 as captured, with
# nothing from Amazon's audio HAL on it. ro.vendor.audio.asr_capture=1 would
# have the wrapper open it as VOICE_RECOGNITION instead, on Amazon's ASR
# pipeline (patches/crown/hardware/amazon). That pipeline's echo canceller
# and residual echo suppression leave an echo that moves against the speaker
# by about 16 ms every few seconds as it adapts, so Kiosk Satellite's own
# echo canceller, which needs a fixed echo path, never settles. Measured on
# the raw capture: every frame in order and the echo 2 to 3 ms behind the
# speaker loopback the whole time.

# The Kiosk Satellite Edition's echo canceller (common/audio/amznaec) stays
# loaded but passes the microphones through. Its settings are kept for
# persist.vendor.amznaec.enable 1: engine 3, Speex's linear filter, then
# WebRTC's moderate suppression and light noise reduction. audio/speaker-null.txt is a speaker null
# calibration for it (speaker_null.h), off: it scored better on the audio
# test's mixes but lost wake words in a live session.
PRODUCT_PROPERTY_OVERRIDES += \
    ro.vendor.amznaec.enable=0 \
    ro.vendor.amznaec.engine=3 \
    ro.vendor.amznaec.aec_level=1 \
    ro.vendor.amznaec.gain_db=20

# EchoParts' noise suppression off on a fresh install. When it is on, the
# audio HAL wrapper sends every MIC recording through Amazon's VoIP pipeline,
# whose AGC turns the level down by up to 25 dB after a loud sound and keeps
# it there until the stream closes. With it off, Android gets microphone 0 as
# captured.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    persist.sys.audio.noise_suppression=0

# Media volume 8 of 15 on a fresh install, which Kiosk Satellite shows as
# 50%. The Show 8's speakers are much louder than the Echo Show 5's, and at
# 70% a voice across the room ends up about 18 dB under music, where the
# audio test caught at most 6 of 10 wake words with any setting.
PRODUCT_PROPERTY_OVERRIDES += \
    ro.config.media_vol_default=8

# The Show 8's privacy chip turns privacy on and off at the release of a hold
# of about a second and a half. Any press turns it off. The Echo Show 5's chip
# acts on a short press instead. So a press released before half a second turns the
# screen on and off, Android does nothing on a longer one and leaves a press
# to the chip while privacy is on (LONG_PRESS_POWER_PRIVACY_HOLD,
# patches/crown/frameworks/base, set by EchoService).
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    ro.echo.privacy_hold=1 \
    ro.echo.button_long_press=8 \
    ro.config.power_privacy_state=/sys/devices/platform/amazon-gating/state
