// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <stddef.h>
#include <stdint.h>
// Connection checkpoint: legacy/HT20, non-DFS, open or WPA2-PSK/CCMP.
enum Mt7902JoinSecurity { MT7902_JOIN_UNSUPPORTED, MT7902_JOIN_OPEN, MT7902_JOIN_WPA2 };
// Full beacon IE validation: privacy-clear alone must not downgrade RSN/WPA/OWE.
Mt7902JoinSecurity mt7902_join_security(uint16_t capability, const uint8_t *ies, size_t length);
bool mt7902_join_credentials(Mt7902JoinSecurity security, size_t password_length, bool has_pmk);
uint16_t mt7902_assoc_capability(bool open, bool short_preamble);
// Called after the data decoder has rejected invalid ciphertext.
bool mt7902_rx_security_allowed(bool open, bool linked, bool protected_frame, bool amsdu, uint16_t ethertype);
bool mt7902_join_channel(uint8_t channel);
bool mt7902_join_rsn(const uint8_t *ie, size_t length);
size_t mt7902_encode_join_domain(uint8_t *out, size_t capacity, uint8_t channel);
uint8_t mt7902_fixed_spe(uint8_t chain_mask);
struct Mt7902LegacyRates {
    uint8_t supported_cck, supported_ofdm, basic_cck, basic_ofdm;
    bool basic_fallback;
};
// Parse Supported/Extended Supported Rates; never invent unsupported rates.
// Missing basic flags fall back to the slowest supported rate, as mac80211 does.
bool mt7902_parse_legacy_rates(const uint8_t *ies, size_t length, uint8_t channel,
                               Mt7902LegacyRates *out);
// Association rate elements, limited to the AP/driver intersection.
size_t mt7902_encode_legacy_rates(uint8_t *out, size_t capacity,
                                  const Mt7902LegacyRates &rates);
// One stream, HT20, long GI; no LDPC/STBC/40 MHz or two-stream MCS claims.
size_t mt7902_encode_ht20(uint8_t *out, size_t capacity, uint8_t mcs_mask);
uint32_t mt7902_ht20_rate(uint8_t mcs_mask);
struct Mt7902AssocResponse {
    uint16_t status, aid;
    bool wmm;
};
// Fixed association response fields plus bounded IE parsing. Peer WMM comes
// from the response, not from our requested HT plan or the AP's beacon alone.
bool mt7902_decode_assoc_response(const uint8_t *frame, size_t length,
                                   Mt7902AssocResponse *out);
struct Mt7902RocGrant {
    uint8_t bss, token, status, channel, band, width, center, center2, request_type;
    uint32_t interval_ms;
};
// Full Connac2 unsolicited ROC grant (RX header36 + event header4 + TLV20).
// Decoding is diagnostic; it does not silently change channel/timing policy.
bool mt7902_decode_roc_grant(const uint8_t *event, size_t length, uint8_t bss,
                             uint8_t token, Mt7902RocGrant *out);
// mt76_connac_mcu_set_deep_sleep(false): CE CHIP_CONFIG, 328-byte payload.
size_t mt7902_encode_keep_awake(uint8_t *out, size_t capacity);
// UNI DEV_INFO_UPDATE: activate OMAC0/band0 with the firmware-reported MAC.
size_t mt7902_encode_device_info(uint8_t *out, size_t capacity, const uint8_t mac[6]);
// Legacy 20 MHz home-channel switch, one TX/RX stream, normal switch reason.
size_t mt7902_encode_channel_switch(uint8_t *out, size_t capacity, uint8_t channel);
bool mt7902_decode_register_reply(const uint8_t *event, size_t length,
                                  uint32_t address, uint32_t *value);
// Connac2 disconnect: BASIC(disconnected) + empty RESET_AND_SET WTBL.
size_t mt7902_encode_sta_remove(uint8_t *out, size_t capacity, uint8_t bss,
                                uint8_t wcid, const uint8_t peer[6]);
// STA_REC_BASIC: connection state is distinct from STA_REC_STATE (NONE/AUTH/ASSOC).
size_t mt7902_encode_sta_basic(uint8_t *out, size_t capacity, const uint8_t peer[6],
                               uint8_t connection_state, bool newly, bool qos, uint16_t aid);
// CE SET_EDCA_PARMS. Before WMM negotiation use non-QoS defaults, not beacon EDCA.
size_t mt7902_encode_edca(uint8_t *out, size_t capacity, uint8_t bss, bool qos,
                          const uint8_t *ies, size_t length);
