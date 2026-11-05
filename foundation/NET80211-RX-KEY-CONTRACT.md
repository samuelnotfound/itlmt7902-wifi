# MT7902 packet and key boundary

The portable normalizer and key encoder do not enable a radio path themselves.
They are boundary implementations for the pinned net80211 bridge, with fixtures
that can run under ASan/UBSan without kernel hardware.

## Receive crypto

Pinned [net80211 input_hwdecrypt](https://github.com/OpenIntelWireless/itlwm/blob/53c51c2cdd6e4b69beb91f310d74c53422b0f8bd/itl80211/openbsd/net80211/ieee80211_input.c)
uses the real CCMP IV to check/update replay counters when Protected is set. It
then clears Protected and removes that IV; the driver has already removed MIC.
Clearing Protected before input_hwdecrypt bypasses this PN path and tells the
stack to trust hardware replay validation. This port does not make that claim.

The Connac2 [RX routine](https://github.com/torvalds/linux/blob/05441d697f7f9a927264f4058cbbc6a03dfcac9d/drivers/net/wireless/mediatek/mt76/mt7921/mac.c)
marks decrypted data only when security mode is nonzero and CM/CLM are clear;
SEC_DONE is not required. It obtains real packet-number bytes from group1.
[mt76_insert_ccmp_hdr](https://github.com/torvalds/linux/blob/05441d697f7f9a927264f4058cbbc6a03dfcac9d/drivers/net/wireless/mediatek/mt76/mac80211.c)
shows the mapping between those PN bytes and the8-byte CCMP IV. RXD1 bits22:21
provide key ID. The normalizer reconstructs that header with the actual metadata,
retains Protected and sets only its hardware_decrypted indication for the bridge
to map to IEEE80211_RXI_HWDEC. Missing group1 fails closed. No fake PN, RSSI,
timestamp, SAME_SEQ or HWDEC_SAME_PN is supplied.

The initial legacy-mode boundary rejects hardware-split A-MSDU and fragmented
frames until their reassembly/duplicate/replay ownership is integrated. Whole
raw frames remain raw802.11; net80211 owns LLC, security and Ethernet decapsulation.
Software-crypto policy instead preserves complete actual encrypted wire bytes
without HWDEC and refuses already-decrypted/IV-stripped data. Using that policy
requires disabling firmware key offload; it cannot repair an already-stripped IV.

## Keys

[Connac2 key builder](https://github.com/torvalds/linux/blob/05441d697f7f9a927264f4058cbbc6a03dfcac9d/drivers/net/wireless/mediatek/mt76/mt76_connac_mcu.c)
and its [wire structures](https://github.com/torvalds/linux/blob/05441d697f7f9a927264f4058cbbc6a03dfcac9d/drivers/net/wireless/mediatek/mt76/mt76_connac_mcu.h)
define STA_REC_KEY_V2(0x11), MCU_CIPHER_AES_CCMP(5), SET_KEY(0), DISABLE_KEY(1).
One install has a44-byte TLV:8-byte header plus36-byte security-key record.
The32-byte key field contains the16-byte CCMP key and zero padding. Clearing
uses an8-byte TLV with add=1 and n_cipher=0. A STA request adds8 bytes in front,
for total52/16 bytes.

The short clear contains no key ID: it clears the selected WCID's cipher state,
not a selected GTK slot. An old GTK deletion must not clear a newly installed GTK
sharing BMC WCID19. The backend must serialize or generation-check such requests.
The encoder enforces this first port's BSS0/AP WCID1/pairwise key0 and BMC WCID19/
group key0..3 roles. A clear is a separate operation with no key ID or key bytes.

## Transmit ownership

Existing m3Tx copies the complete raw frame into driver-owned DMA staging before
returning success. That staging remains owned until matching TXFREE or confirmed
DMA quiescence. A bridge may release its separate mbuf/node after the copy if no
completion path retains it; otherwise it must complete the corresponding token
exactly once. A full ring means not accepted: the caller retains the frame.
