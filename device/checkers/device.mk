#
# What the Echo Show 5 (1st gen) gets on top of the amazon-oss checkers
# tree. stage-tree.sh copies device/checkers/ to
# device/kiosk-satellite/checkers in the tree.
#

# The network name EchoService gives the Show: echo-show-5-g1-ks and the end
# of its serial.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    ro.echo.hostname=echo-show-5-g1-ks

# The Kiosk Satellite Edition's echo canceller (common/audio/amznaec) stays
# loaded but passes the microphones through, as on the other Echo Shows.
# checkers' raw microphones read as the Echo Show 5 (2nd gen)'s at Amazon's
# MICPGA 40 (about -66 dBFS in a quiet room), so it takes the same settings.
# Kiosk Satellite adds its 20 dB base gain for checkers, as for cronos.
PRODUCT_PROPERTY_OVERRIDES += \
    ro.vendor.amznaec.enable=0 \
    ro.vendor.amznaec.engine=2 \
    ro.vendor.amznaec.gain_db=20

# EchoParts' noise suppression off on a fresh install.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    persist.sys.audio.noise_suppression=0

# Media volume 10 of 15 on a fresh install, which Kiosk Satellite shows as
# 70%. checkers has the Echo Show 5 (2nd gen)'s speaker, so it takes that
# Show's level.
PRODUCT_PROPERTY_OVERRIDES += \
    ro.config.media_vol_default=10

# checkers' privacy chip works as the Echo Show 8's: it turns privacy on at
# the release of a hold of about a second and a half, and any press turns it
# off. A press released before half a second turns the screen on and off,
# Android does nothing on a longer one and leaves a press to the chip while
# privacy is on (LONG_PRESS_POWER_PRIVACY_HOLD, set by EchoService).
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    ro.echo.privacy_hold=1 \
    ro.echo.button_long_press=8 \
    ro.config.power_privacy_state=/sys/devices/platform/amazon-gating/state
