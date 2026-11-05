// SPDX-License-Identifier: BSD-3-Clause-Clear
// Connac2 wire layout from mt76_connac_mcu_sta_key_tlv/sta_req_hdr. Constants:
// STA_REC_KEY_V2=0x11, SET_KEY=0, DISABLE_KEY=1, MCU_CIPHER_AES_CCMP=5.
// See foundation/NET80211-RX-KEY-CONTRACT.md for pinned primary references.
#include "mt7902_net80211_key.h"
#include <string.h>

size_t mt7902_net80211_encode_key(uint8_t *out,size_t capacity,
                                 const Mt7902Net80211KeyCommand &c)
{
    const bool install=c.operation==MT7902_NET80211_KEY_INSTALL_CCMP;
    if (!out || (c.operation!=MT7902_NET80211_KEY_INSTALL_CCMP &&
        c.operation!=MT7902_NET80211_KEY_CLEAR_WCID) || c.bss ||
        c.wcid!=(c.pairwise?1u:19u) || c.key_id>(c.pairwise?0u:3u)) return 0;
    if (install) {
        if (c.key_length!=16) return 0;
    } else {
        if (c.key_length || c.key_id) return 0;
        for (uint8_t byte:c.key) if (byte) return 0;
    }
    const size_t bytes=install?52u:16u;
    if (capacity<bytes) return 0;
    uint8_t request[52]={};
    request[0]=c.bss; request[1]=(uint8_t)c.wcid;
    request[2]=1; // One TLV, little endian.
    request[4]=1; request[5]=c.pairwise?0:0x0e;
    request[8]=0x11; request[10]=install?44:8;
    request[12]=install?0:1; request[13]=install?1:0;
    if (install) {
        request[16]=5; request[17]=36; request[18]=c.key_id; request[19]=16;
        memcpy(request+20,c.key,sizeof(c.key));
    }
    memcpy(out,request,bytes);
    // The caller owns the snapshot/output. Do not leave our extra stack copy.
    volatile uint8_t *wipe=request;
    for (size_t i=0;i<sizeof(request);i++) wipe[i]=0;
    return bytes;
}
