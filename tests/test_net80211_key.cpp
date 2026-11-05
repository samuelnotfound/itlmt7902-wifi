// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "../foundation/mt7902_net80211_key.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>

namespace {
void invalid(const Mt7902Net80211KeyCommand &c,size_t capacity=52)
{
    uint8_t out[54]; memset(out,0xa5,sizeof(out));
    assert(!mt7902_net80211_encode_key(out+1,capacity,c));
    for (uint8_t b:out) assert(b==0xa5);
}
}
int main()
{
    Mt7902Net80211KeyCommand c={};
    c.operation=MT7902_NET80211_KEY_INSTALL_CCMP; c.wcid=1; c.pairwise=true; c.key_length=16;
    for (unsigned i=0;i<16;i++) c.key[i]=(uint8_t)i;
    // Independent full-wire fixtures: known key bytes0..15 plus32-byte key field padding.
    const uint8_t pairwise[52]={0,1,1,0,1,0,0,0,17,0,44,0,0,1,0,0,5,36,0,16,
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    const uint8_t group[52]={0,19,1,0,1,14,0,0,17,0,44,0,0,1,0,0,5,36,3,16,
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    uint8_t out[54]; memset(out,0xa5,sizeof(out));
    assert(mt7902_net80211_encode_key(out+1,52,c)==52);
    assert(!memcmp(out+1,pairwise,52) && out[0]==0xa5 && out[53]==0xa5);
    c.pairwise=false; c.wcid=19; c.key_id=3;
    assert(mt7902_net80211_encode_key(out+1,52,c)==52);
    assert(!memcmp(out+1,group,52));
    for (size_t n=0;n<52;n++) invalid(c,n);
    for (uint8_t id:{uint8_t(0),uint8_t(1),uint8_t(2),uint8_t(3)}) {
        c.key_id=id; assert(mt7902_net80211_encode_key(out,52,c)==52 && out[18]==id);
    }
    for (uint8_t id:{uint8_t(4),uint8_t(255)}) { c.key_id=id; invalid(c); }
    c.key_id=0;
    for (uint16_t wcid:{uint16_t(0),uint16_t(1),uint16_t(18),uint16_t(20),uint16_t(255),uint16_t(256),uint16_t(65535)}) {
        c.wcid=wcid; invalid(c);
    }
    c.wcid=19;
    for (uint8_t len:{uint8_t(0),uint8_t(15),uint8_t(17),uint8_t(255)}) { c.key_length=len; invalid(c); }
    c.key_length=16; c.bss=1; invalid(c); c.bss=255; invalid(c); c.bss=0;
    c.pairwise=true; c.wcid=1; c.key_id=1; invalid(c); c.key_id=0;
    c.operation=(Mt7902Net80211KeyOperation)99; invalid(c);
    c.operation=MT7902_NET80211_KEY_CLEAR_WCID;
    invalid(c); // A clear cannot accidentally serialize a still-populated key snapshot.
    c.key_length=0; memset(c.key,0,sizeof(c.key));
    const uint8_t clearPairwise[16]={0,1,1,0,1,0,0,0,17,0,8,0,1,0,0,0};
    const uint8_t clearGroup[16]={0,19,1,0,1,14,0,0,17,0,8,0,1,0,0,0};
    memset(out,0xa5,sizeof(out));
    assert(mt7902_net80211_encode_key(out+1,16,c)==16);
    assert(!memcmp(out+1,clearPairwise,16) && out[0]==0xa5 && out[17]==0xa5);
    c.pairwise=false; c.wcid=19;
    assert(mt7902_net80211_encode_key(out+1,16,c)==16 && !memcmp(out+1,clearGroup,16));
    for (size_t n=0;n<16;n++) invalid(c,n);
    c.key_id=1; invalid(c); c.key_id=0; c.key[15]=1; invalid(c);
    assert(!mt7902_net80211_encode_key(nullptr,52,c));
    puts("Passed Connac2 full key-install and WCID-wide clear fixtures, bounds and role validation");
}
