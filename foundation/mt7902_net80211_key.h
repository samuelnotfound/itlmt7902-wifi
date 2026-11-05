// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <stddef.h>
#include <stdint.h>

enum Mt7902Net80211KeyOperation : uint8_t {
    MT7902_NET80211_KEY_INSTALL_CCMP,
    // The firmware's short DISABLE_KEY command clears the selected WCID's
    // cipher state. It carries no key ID and is not a per-GTK-slot delete.
    MT7902_NET80211_KEY_CLEAR_WCID
};
struct Mt7902Net80211KeyCommand {
    Mt7902Net80211KeyOperation operation;
    uint16_t wcid;
    uint8_t bss, key_id, key_length;
    bool pairwise;
    uint8_t key[16]; // Snapshot owns its bytes; no node/key/mbuf pointers.
};

// Full UNI_STA_REC_UPDATE payload:8-byte STA request + KEY_V2 TLV.
// Install is52 bytes (TLV44); clear is16 bytes (TLV8). This first single-STA
// backend admits only BSS0/AP WCID1/pairwise ID0 or BSS0/BMC WCID19/group IDs0..3.
// A clear requires key_id/key_length/key bytes all zero, explicitly expressing
// WCID-wide deletion. Output is unchanged on invalid input/insufficient capacity.
size_t mt7902_net80211_encode_key(uint8_t *output,size_t capacity,
                                 const Mt7902Net80211KeyCommand &command);
