// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "../foundation/mt7902_net80211_rx.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
const uint32_t G1=1u<<11,G2=1u<<12,G3=1u<<13,G4=1u<<14,G5=1u<<15;
void put32(uint8_t *p,uint32_t v)
{
    for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(i*8));
}
std::vector<uint8_t> frame(uint8_t fc0=0x08,uint8_t fc1=0x42)
{
    size_t hdr=24;
    if ((fc0&12)==8) {
        if ((fc1&3)==3) hdr+=6;
        if (fc0&0x80) { hdr+=2; if (fc1&0x80) hdr+=4; }
    }
    std::vector<uint8_t> f(hdr+24,0);
    f[0]=fc0; f[1]=fc1;
    for (size_t i=4;i<22;i++) f[i]=(uint8_t)i;
    f[22]=0x20; f[23]=1;
    // Deliberately opaque body: parsing/decrypting LLC belongs to net80211.
    for (size_t i=hdr;i<f.size();i++) f[i]=(uint8_t)(0x70+i-hdr);
    return f;
}
std::vector<uint8_t> packet(const std::vector<uint8_t> &f,uint32_t groups=G1,
                           uint8_t security=4,uint8_t mismatch=0,uint8_t key=2,
                           uint8_t format=0)
{
    size_t off=24;
    if (groups&G4) off+=16;
    if (groups&G1) off+=16;
    if (groups&G2) off+=8;
    if (groups&G3) off+=8;
    if (groups&G5) off+=72;
    std::vector<uint8_t> p(off+f.size(),0);
    put32(p.data(),(uint32_t)p.size()|(2u<<27));
    put32(p.data()+4,groups|((uint32_t)security<<16)|((uint32_t)mismatch<<23)|((uint32_t)key<<21));
    put32(p.data()+12,44u<<8);
    put32(p.data()+16,format);
    off=24;
    if (groups&G4) off+=16;
    if (groups&G1) {
        const uint8_t pn[]={0x11,0x22,0x33,0x44,0x55,0x66};
        memcpy(p.data()+off,pn,sizeof(pn)); off+=16;
    }
    if (groups&G2) { put32(p.data()+off,0x89abcdefu); off+=8; }
    if (groups&G3) {
        put32(p.data()+off,(1u<<24)|11); off+=8;
        if (groups&G5) { off+=24; put32(p.data()+off,0xff00788c); off+=48; }
        else put32(p.data()+off-4,0xff00788c);
    }
    assert(off+f.size()==p.size());
    memcpy(p.data()+off,f.data(),f.size());
    return p;
}
void unchangedFailure(const std::vector<uint8_t> &p,Mt7902Net80211RxCrypto policy,
                      Mt7902Net80211RxStatus expected,size_t capacity=512)
{
    uint8_t out[512]; memset(out,0xa5,sizeof(out));
    Mt7902Net80211Rx result,copy; memset(&result,0xa5,sizeof(result)); memcpy(&copy,&result,sizeof(copy));
    assert(mt7902_net80211_normalize_rx(p.data(),(uint32_t)p.size(),policy,out,capacity,&result)==expected);
    for (uint8_t b:out) assert(b==0xa5);
    assert(!memcmp(&result,&copy,sizeof(result)));
}
}

int main()
{
    const uint8_t iv[]={0x11,0x22,0,0xa0,0x33,0x44,0x55,0x66};
    const std::vector<uint8_t> raw=frame();
    uint8_t output[512]; Mt7902Net80211Rx result={};
    // Independent PN/IV fixture: descriptor group order is 4,1,2,3,5.
    for (uint32_t groups:{G1,G1|G4,G1|G2|G3,G1|G2|G3|G4|G5}) {
        const auto p=packet(raw,groups);
        assert(mt7902_net80211_normalize_rx(p.data(),(uint32_t)p.size(),MT7902_NET80211_HW_CCMP,
                                          output,sizeof(output),&result)==MT7902_NET80211_RX_OK);
        assert(result.length==raw.size()+8 && result.hardware_decrypted && result.pn_valid);
        assert(result.packet_number==0x665544332211ull && result.key_id==2);
        assert(output[1]&0x40); // Preserve Protected: stack must perform its real PN/replay check.
        assert(!memcmp(output,raw.data(),24));
        assert(!memcmp(output+24,iv,8));
        assert(!memcmp(output+32,raw.data()+24,raw.size()-24));
        assert(result.channel==44 && result.band==5);
        assert(result.timestamp_valid==!!(groups&G2));
        if (groups&G2) assert(result.timestamp==0x89abcdefu);
        assert(result.rssi_valid==!!(groups&G3));
        if (groups&G3) assert(result.rssi_dbm==-40);
        // Packet truncated anywhere must not produce data or partial output.
        for (size_t n=0;n<p.size();n++) {
            std::vector<uint8_t> cut(p.begin(),p.begin()+n);
            if (!n) continue; // nullptr is an argument error rather than a descriptor error.
            unchangedFailure(cut,MT7902_NET80211_HW_CCMP,MT7902_NET80211_RX_DESCRIPTOR);
        }
        for (size_t n=0;n<raw.size()+8;n++)
            unchangedFailure(p,MT7902_NET80211_HW_CCMP,MT7902_NET80211_RX_CAPACITY,n);
        unchangedFailure(p,MT7902_NET80211_SOFTWARE_CRYPTO,MT7902_NET80211_RX_CRYPTO);
    }
    // The IV is inserted after the actual MAC header (including addr4/QoS/HTC).
    for (const auto &f:{frame(0x88,0x42),frame(0x88,0xc2),frame(0x88,0xc3),frame(0xb0,0x40)}) {
        const auto p=packet(f);
        const size_t header=f.size()-24;
        assert(mt7902_net80211_normalize_rx(p.data(),(uint32_t)p.size(),MT7902_NET80211_HW_CCMP,
            output,sizeof(output),&result)==MT7902_NET80211_RX_OK);
        assert(!memcmp(output+header,iv,8));
        assert(!memcmp(output+header+8,f.data()+header,24));
    }
    // Software crypto receives exact ciphertext/real wire IV+MIC, never HWDEC.
    for (uint8_t mode:{uint8_t(0),uint8_t(4)}) {
        const auto p=packet(raw,G1,mode,mode?1:0);
        assert(mt7902_net80211_normalize_rx(p.data(),(uint32_t)p.size(),MT7902_NET80211_SOFTWARE_CRYPTO,
            output,sizeof(output),&result)==MT7902_NET80211_RX_OK);
        assert(result.length==raw.size() && !memcmp(output,raw.data(),raw.size()));
        assert(!result.hardware_decrypted && !result.pn_valid);
        unchangedFailure(p,MT7902_NET80211_HW_CCMP,MT7902_NET80211_RX_CRYPTO);
    }
    // Clear beacons/EAPOL/open data must remain clear; protection policy is the stack's job.
    for (const auto &f:{frame(0x80,0),frame(0x08,2)}) for (auto policy:
        {MT7902_NET80211_HW_CCMP,MT7902_NET80211_SOFTWARE_CRYPTO}) {
        const auto p=packet(f,0,0);
        assert(mt7902_net80211_normalize_rx(p.data(),(uint32_t)p.size(),policy,
            output,sizeof(output),&result)==MT7902_NET80211_RX_OK);
        assert(result.length==f.size() && !memcmp(output,f.data(),f.size()));
        assert(!result.hardware_decrypted && !result.pn_valid && !result.rssi_valid && !result.timestamp_valid);
    }
    unchangedFailure(packet(raw,0),MT7902_NET80211_HW_CCMP,MT7902_NET80211_RX_PN_MISSING);
    // Synthetic successful auth/association wire replies must survive the
    // normalizer byte-for-byte, including fixed status/AID and rate elements.
    for(const auto &f:std::vector<std::vector<uint8_t>>{
        {0xb0,0,0,0, 2,1,2,3,4,5, 2,6,7,8,9,10, 2,6,7,8,9,10, 0x10,0,
         0,0,2,0,0,0},
        {0x10,0,0,0, 2,1,2,3,4,5, 2,6,7,8,9,10, 2,6,7,8,9,10, 0x20,0,
         0x31,0x04,0,0,1,0xc0,1,4,0x82,0x84,0x8b,0x96}}) {
        const auto p=packet(f,G2|G3,0);
        assert(mt7902_net80211_normalize_rx(p.data(),(uint32_t)p.size(),MT7902_NET80211_HW_CCMP,
            output,sizeof(output),&result)==MT7902_NET80211_RX_OK);
        assert(result.length==f.size() && !memcmp(output,f.data(),f.size()));
        assert(!result.hardware_decrypted && result.timestamp_valid);
        for(size_t n=1;n<p.size();n++) {
            std::vector<uint8_t> truncated(p.begin(),p.begin()+n);
            unchangedFailure(truncated,MT7902_NET80211_HW_CCMP,MT7902_NET80211_RX_DESCRIPTOR);
        }
    }
    unchangedFailure(packet(raw,G1,2),MT7902_NET80211_HW_CCMP,MT7902_NET80211_RX_CRYPTO);
    unchangedFailure(packet(frame(0x08,2),G1),MT7902_NET80211_HW_CCMP,MT7902_NET80211_RX_CRYPTO);
    unchangedFailure(packet(raw),(Mt7902Net80211RxCrypto)99,MT7902_NET80211_RX_ARGUMENT);
    // SEC_DONE is neither required nor sufficient to label a packet decrypted.
    auto secDone=packet(raw); secDone[7]|=0x80;
    assert(mt7902_net80211_normalize_rx(secDone.data(),(uint32_t)secDone.size(),MT7902_NET80211_HW_CCMP,
        output,sizeof(output),&result)==MT7902_NET80211_RX_OK && result.hardware_decrypted);
    auto clear=frame(0x08,2); const auto clearMismatch=packet(clear,0,4,1);
    assert(mt7902_net80211_normalize_rx(clearMismatch.data(),(uint32_t)clearMismatch.size(),MT7902_NET80211_HW_CCMP,
        output,sizeof(output),&result)==MT7902_NET80211_RX_OK && !result.hardware_decrypted);
    for (uint8_t format:{uint8_t(1),uint8_t(2),uint8_t(3)})
        unchangedFailure(packet(raw,G1,4,0,2,format),MT7902_NET80211_HW_CCMP,MT7902_NET80211_RX_SPLIT_AMSDU);
    auto fragmented=raw; fragmented[1]|=4;
    unchangedFailure(packet(fragmented),MT7902_NET80211_HW_CCMP,MT7902_NET80211_RX_FRAGMENT);
    fragmented=raw; fragmented[22]|=1;
    unchangedFailure(packet(fragmented),MT7902_NET80211_HW_CCMP,MT7902_NET80211_RX_FRAGMENT);
    // Shifted aliases must preserve the header, inserted IV and body.
    auto alias=packet(raw,G1|G4); alias.resize(alias.size()+64);
    assert(mt7902_net80211_normalize_rx(alias.data(),(uint32_t)alias.size(),MT7902_NET80211_HW_CCMP,
        alias.data()+44,alias.size()-44,&result)==MT7902_NET80211_RX_OK);
    assert(!memcmp(alias.data()+44,raw.data(),24));
    assert(!memcmp(alias.data()+68,iv,8));
    assert(!memcmp(alias.data()+76,raw.data()+24,24));
    assert(mt7902_net80211_normalize_rx(nullptr,0,MT7902_NET80211_HW_CCMP,
        output,sizeof(output),&result)==MT7902_NET80211_RX_ARGUMENT);
    puts("Passed raw net80211 RX, real CCMP PN/IV, cipher ownership, bounds and alias fixtures");
}
