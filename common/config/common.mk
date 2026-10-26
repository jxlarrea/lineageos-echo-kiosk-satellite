#
# The Kiosk Satellite Edition layer every Echo inherits, the Echo Dot
# included. What only the Echo Shows get is in show.mk. stage-tree.sh copies
# common/ to vendor/kiosk-satellite in the tree.
#

# WPA2/WPA3 transition mode networks join with WPA2 (rro/Wifi).
PRODUCT_PACKAGES += \
    EchoWifiOverlay

# Amazon's wake words for Kiosk Satellite: biscuit-pryond (pryon/) with
# Pryon and the models from the Echo Dot's Fire OS 6.5.7.4 and DAVS, which
# scripts/extract-biscuit-blobs.sh writes to pryon-vendor.mk. Every Echo is
# an MT8163 with the same 32-bit userspace, so they all run the same files.
# locales.txt names the models each locale listens with.
PRODUCT_PACKAGES += \
    biscuit-pryond

PRODUCT_COPY_FILES += \
    vendor/kiosk-satellite/pryon/locales.txt:$(TARGET_COPY_OUT_SYSTEM)/etc/pryon/locales.txt

$(call inherit-product, vendor/amazon/biscuit/pryon-vendor.mk)
