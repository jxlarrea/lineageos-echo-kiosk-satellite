#
# What the Echo Show 5 (2nd gen) gets on top of the amazon-oss cronos tree.
# stage-tree.sh copies device/cronos/ to device/kiosk-satellite/cronos in the
# tree.
#

# The network name EchoService gives the Show: echo-show-5-ks and the end of
# its serial.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    ro.echo.hostname=echo-show-5-ks
