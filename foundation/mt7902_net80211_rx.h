// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <stddef.h>
#include <stdint.h>

// These policies describe the configured key owner, not a guess based on LLC.
// Software mode never accepts frames which hardware has already decrypted.
enum Mt7902Net80211RxCrypto : uint8_t {
    MT7902_NET80211_HW_CCMP,
    MT7902_NET80211_SOFTWARE_CRYPTO
};
enum Mt7902Net80211RxStatus : uint8_t {
    MT7902_NET80211_RX_OK,
    MT7902_NET80211_RX_ARGUMENT,
    MT7902_NET80211_RX_DESCRIPTOR,
    MT7902_NET80211_RX_FRAME,
    MT7902_NET80211_RX_FRAGMENT,
    MT7902_NET80211_RX_SPLIT_AMSDU,
    MT7902_NET80211_RX_CRYPTO,
    MT7902_NET80211_RX_PN_MISSING,
    MT7902_NET80211_RX_CAPACITY
};
struct Mt7902Net80211Rx {
    uint32_t length;
    uint32_t timestamp;
    uint64_t packet_number;
    int16_t rssi_dbm;
    uint8_t channel, band, key_id;
    bool timestamp_valid, rssi_valid, pn_valid;
    // Caller maps this only to IEEE80211_RXI_HWDEC. Never set SAME_SEQ or
    // HWDEC_SAME_PN: hardware-split A-MSDU is deliberately rejected here.
    bool hardware_decrypted;
};

// Input is one complete Connac2 NORMAL DMA packet. Output is raw 802.11,
// never Ethernet. HW-decrypted CCMP retains Protected and gains an exact IV
// reconstructed from real group1 PN/key-id metadata; the hardware-stripped MIC
// remains absent, matching pinned net80211's input_hwdecrypt contract.
// The output buffer must have room for frame_length + 8 in that case.
// All failures leave output and result untouched. Overlap is supported.
Mt7902Net80211RxStatus mt7902_net80211_normalize_rx(
    const uint8_t *packet, uint32_t packet_length, Mt7902Net80211RxCrypto policy,
    uint8_t *output, size_t output_capacity, Mt7902Net80211Rx *result);
