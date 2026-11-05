// SPDX-License-Identifier: BSD-3-Clause-Clear
// Hardware fields: mt76 mt7921/mac.c:mt7921_mac_fill_rx and
// mac80211.c:mt76_insert_ccmp_hdr. Stack contract: pinned OpenIntelWireless
// ieee80211_input.c:ieee80211_input_hwdecrypt. This copies no upstream code.
#include "mt7902_net80211_rx.h"
#include "../codec/mt7902_rx_codec.h"
#include <string.h>

namespace {
uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
        ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
}

Mt7902Net80211RxStatus mt7902_net80211_normalize_rx(
    const uint8_t *packet,uint32_t packet_length,Mt7902Net80211RxCrypto policy,
    uint8_t *output,size_t capacity,Mt7902Net80211Rx *result)
{
    if (!packet || !output || !result ||
        (policy!=MT7902_NET80211_HW_CCMP && policy!=MT7902_NET80211_SOFTWARE_CRYPTO))
        return MT7902_NET80211_RX_ARGUMENT;
    Mt7902RxView rx={};
    if (mt7902_parse_rx_normal(packet,packet_length,&rx)!=MT7902_RX_OK)
        return MT7902_NET80211_RX_DESCRIPTOR;
    const uint8_t *frame=packet+rx.frame_offset;
    const uint32_t bytes=rx.frame_length;
    if (bytes<24 || (frame[0]&3)) return MT7902_NET80211_RX_FRAME;
    const uint8_t type=(frame[0]>>2)&3;
    if (type!=0 && type!=2) return MT7902_NET80211_RX_FRAME;
    if (rx.fragmented || (frame[1]&4) || (frame[22]&15))
        return MT7902_NET80211_RX_FRAGMENT;
    // Split subframes require reassembly/SAME_PN ownership, not invented flags.
    if (rx.payload_format) return MT7902_NET80211_RX_SPLIT_AMSDU;
    size_t header=24;
    if (type==2) {
        if ((frame[1]&3)==3) header+=6;
        if (frame[0]&0x80) {
            header+=2;
            if (frame[1]&0x80) header+=4;
        }
    }
    if (header>bytes) return MT7902_NET80211_RX_FRAME;
    const bool protected_frame=frame[1]&0x40;
    // Linux's MT7902-family decrypted test deliberately does not require
    // SEC_DONE. CM/CLM mean the payload has not been hardware decrypted.
    const bool decrypted=rx.security_mode && !rx.cipher_mismatch;
    const bool insert_iv=decrypted && protected_frame;
    if (decrypted && (!protected_frame || rx.security_mode!=4 ||
        policy!=MT7902_NET80211_HW_CCMP)) return MT7902_NET80211_RX_CRYPTO;
    if (protected_frame && !decrypted) {
        if (policy!=MT7902_NET80211_SOFTWARE_CRYPTO ||
            (rx.security_mode && rx.security_mode!=4) || bytes-header<16)
            return MT7902_NET80211_RX_CRYPTO;
        // Preserve the real wire IV/MIC and let the stack verify/decrypt it.
        // Never mark ciphertext as HWDEC or search its contents for plaintext.
    }
    Mt7902Net80211Rx value={};
    value.channel=rx.channel; value.band=rx.band;
    value.hardware_decrypted=insert_iv;
    for (unsigned i=0;i<4;i++) if ((rx.chain_valid_mask&(1u<<i)) &&
        rx.chain_rssi_dbm[i]<=0 && rx.chain_rssi_dbm[i]>=-110 &&
        (!value.rssi_valid || rx.chain_rssi_dbm[i]>value.rssi_dbm)) {
        value.rssi_dbm=rx.chain_rssi_dbm[i]; value.rssi_valid=true;
    }
    const uint32_t d1=le32(packet+4);
    uint32_t offset=24;
    if (d1&(1u<<14)) offset+=16; // Group4 precedes Group1.
    uint8_t iv[8]={};
    if (d1&(1u<<11)) {
        // The shared RX decoder has already checked each complete group.
        if (offset>rx.frame_offset || rx.frame_offset-offset<16)
            return MT7902_NET80211_RX_DESCRIPTOR;
        if (insert_iv) {
            const uint8_t *pn=packet+offset;
            value.key_id=(uint8_t)((d1>>21)&3);
            for (unsigned i=0;i<6;i++) value.packet_number|=(uint64_t)pn[i]<<(8*i);
            value.pn_valid=true;
            iv[0]=pn[0]; iv[1]=pn[1]; iv[3]=(uint8_t)(0x20|(value.key_id<<6));
            memcpy(iv+4,pn+2,4);
        }
        offset+=16;
    } else if (insert_iv) return MT7902_NET80211_RX_PN_MISSING;
    if (d1&(1u<<12)) {
        if (offset>rx.frame_offset || rx.frame_offset-offset<8)
            return MT7902_NET80211_RX_DESCRIPTOR;
        value.timestamp=le32(packet+offset); value.timestamp_valid=true;
    }
    const size_t length=(size_t)bytes+(insert_iv?8u:0u);
    if (capacity<length) return MT7902_NET80211_RX_CAPACITY;
    // Copy first, then insert in the destination, so aliases never corrupt the
    // source header/body or PN metadata before it has been consumed.
    memmove(output,frame,bytes);
    if (insert_iv) {
        memmove(output+header+8,output+header,bytes-header);
        memcpy(output+header,iv,sizeof(iv));
    }
    value.length=(uint32_t)length;
    *result=value;
    return MT7902_NET80211_RX_OK;
}
