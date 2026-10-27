#
# What the Echo Show 8 (1st gen) gets on top of the amazon-oss crown tree.
# stage-tree.sh copies device/crown/ to device/kiosk-satellite/crown in the
# tree.
#

# The network name EchoService gives the Show: echo-show-8-ks and the end of
# its serial.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    ro.echo.hostname=echo-show-8-ks

# Amazon's ASR pipeline does the echo cancellation: the far-field front end
# Amazon's own wake word listens to, with its echo canceller, beamformer and
# residual echo suppression over all four microphones and the speaker
# reference. The audio HAL wrapper sends Kiosk Satellite's mic capture
# through it (patches/crown/hardware/amazon) with 24 dB of gain and a soft
# limit, since it comes out about 25 dB quieter than a plain capture. At
# 12 dB a voice reached Kiosk Satellite's talk over level only now and then.
# Measured live with vsWakeWord, 40 held-out voices played from 2.5 m at 50%
# volume (scripts/audio-test.sh): 14 of 40 wake words over music and 17 of
# 40 over the assistant's voice, as many as in a quiet room (15). The Kiosk
# Satellite Edition's own echo canceller on microphone 0 caught 5 and 11.
PRODUCT_PROPERTY_OVERRIDES += \
    ro.vendor.audio.asr_capture=1 \
    ro.vendor.audio.asr_capture_gain_db=24

# The Kiosk Satellite Edition's echo canceller (common/audio/amznaec) stays
# loaded but passes the microphones through, so Amazon's pipeline gets them
# as captured. Its settings are kept for persist.vendor.amznaec.enable 1:
# engine 3, Speex's linear filter, then WebRTC's moderate suppression and
# light noise reduction. audio/speaker-null.txt is a speaker null
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
# the echo canceller leaves it.
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
