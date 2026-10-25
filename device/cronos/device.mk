#
# What the Echo Show 5 (2nd gen) gets on top of the amazon-oss cronos tree.
# stage-tree.sh copies device/cronos/ to device/kiosk-satellite/cronos in the
# tree.
#

# The network name EchoService gives the Show: echo-show-5-ks and the end of
# its serial.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    ro.echo.hostname=echo-show-5-ks

# Echo cancellation with Speex below 1 kHz and the pattern removal across the
# two microphones above it (common/audio/amznaec, engine 2), then 20 dB of
# gain: Amazon's analog microphone gain (MICPGA 40) leaves a voice about 20 dB
# below what Kiosk Satellite's wake word engines expect. Measured on this
# Show with music at 55%: the wake word 10 times out of 10, against 7 with
# Speex alone, 4 with WebRTC and 3 with Amazon's own front end.
PRODUCT_PROPERTY_OVERRIDES += \
    ro.vendor.amznaec.engine=2 \
    ro.vendor.amznaec.gain_db=20

# Media volume 11 of 15, about 75%, on a fresh install: with Kiosk Satellite's
# media player and assistant volumes at their default 100, the wake word over
# music worked 9 times out of 10 at this level.
PRODUCT_PROPERTY_OVERRIDES += \
    ro.config.media_vol_default=11
