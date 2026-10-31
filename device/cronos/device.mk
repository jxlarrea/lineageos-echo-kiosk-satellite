#
# What the Echo Show 5 (2nd gen) gets on top of the amazon-oss cronos tree.
# stage-tree.sh copies device/cronos/ to device/kiosk-satellite/cronos in the
# tree.
#

# The network name EchoService gives the Show: echo-show-5-ks and the end of
# its serial.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    ro.echo.hostname=echo-show-5-ks

# Kiosk Satellite's MIC capture stays raw: microphone 0 as captured, so
# Kiosk Satellite's own echo canceller sees the speaker's echo at a fixed
# delay. Any echo canceller, suppressor or gain control ahead of it changes
# that echo as it adapts, and Kiosk Satellite's canceller never settles.
#
# The Kiosk Satellite Edition's echo canceller (common/audio/amznaec) stays
# loaded but passes the microphones through. persist.vendor.amznaec.enable 1
# brings it back with these settings: Speex below 1 kHz and the pattern
# removal across the two microphones above it (engine 2), then 20 dB of gain,
# since Amazon's analog microphone gain (MICPGA 40) leaves a voice about 20 dB
# below what Kiosk Satellite's wake word engines expect.
PRODUCT_PROPERTY_OVERRIDES += \
    ro.vendor.amznaec.enable=0 \
    ro.vendor.amznaec.engine=2 \
    ro.vendor.amznaec.gain_db=20

# EchoParts' noise suppression off on a fresh install. When it is on, the
# audio HAL wrapper sends every MIC recording through Amazon's VoIP pipeline,
# with its AGC and noise reduction.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    persist.sys.audio.noise_suppression=0

# Media volume 10 of 15 on a fresh install, which Kiosk Satellite shows as
# 70%. With Kiosk Satellite's media player and assistant volumes at their
# default 100, the wake word over music worked 9 times out of 10 one step
# higher, at 11 of 15 (75%).
PRODUCT_PROPERTY_OVERRIDES += \
    ro.config.media_vol_default=10
