#
# What the Echo Dot (3rd gen) gets on top of its own device tree in
# sources/device/amazon/donut. stage-tree.sh copies device/donut/ to
# device/kiosk-satellite/donut in the tree. The port is at bring-up: the
# device tree does not inherit the Kiosk Satellite Edition layer yet.
#

# Wi-Fi power save off, as on the Echo Dot (2nd gen): the Dot is always on
# mains, and in power save some of the messages between kiosks got lost
# (patches/donut/frameworks/opt/net/wifi). setprop
# persist.biscuit.wifi_powersave 1 turns it back on.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    persist.biscuit.wifi_powersave=0

# One cached process and every OOM adjustment a full one, as on the Echo Dot
# (2nd gen) (patches/donut/frameworks/base).
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    ro.config.max_cached_processes=1 \
    ro.config.oomadj_update_quick=false
