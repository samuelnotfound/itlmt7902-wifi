// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "../codec/mt7902_join_codec.h"
#include "../codec/mt7902_tx_codec.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>
static void test_peer_commands()
{
    const uint8_t peer[]={2,0x11,0x22,0x33,0x44,0x55};
    // Independent STA BASIC wire fixtures. NEW is absent on subsequent updates;
    // CONNECT and PORT_SECURE are not the separate NONE/AUTH/ASSOC state TLV.
    const uint8_t connecting[]={0,0,20,0,2,0,1,0,1,0,0,0,2,0x11,0x22,0x33,0x44,0x55,3,0};
    const uint8_t associated[]={0,0,20,0,2,0,1,0,2,1,5,0,2,0x11,0x22,0x33,0x44,0x55,1,0};
    uint8_t actual[46]; memset(actual,0xa5,sizeof(actual));
    assert(mt7902_encode_sta_basic(actual+1,20,peer,1,true,false,0)==20);
    assert(!memcmp(actual+1,connecting,20) && actual[0]==0xa5 && actual[21]==0xa5);
    assert(mt7902_encode_sta_basic(actual+1,20,peer,2,false,true,5)==20);
    assert(!memcmp(actual+1,associated,20));
    for (size_t n=0;n<20;n++) {
        memset(actual,0xa5,sizeof(actual));
        assert(!mt7902_encode_sta_basic(actual+1,n,peer,1,true,false,0));
        for (uint8_t byte:actual) assert(byte==0xa5);
    }
    assert(!mt7902_encode_sta_basic(nullptr,20,peer,1,true,false,0));
    assert(!mt7902_encode_sta_basic(actual,20,nullptr,1,true,false,0));
    assert(!mt7902_encode_sta_basic(actual,20,peer,3,true,false,0));
    // Default non-QoS DCF, even when a beacon advertises WMM. A hostile beacon
    // cannot affect pre-association contention or set the firmware's QoS byte.
    const uint8_t wmm[]={221,24,0,0x50,0xf2,2,1,1,0x80,0,
        3,0xa4,0,0,0x27,0xa4,0,0,0x42,0x43,0x5e,0,0x62,0x32,0x2f,0};
    const uint8_t dcf[44]={15,0,255,3,0,0,2,0,0,0,
        15,0,255,3,0,0,2,0,0,0,15,0,255,3,0,0,2,0,0,0,
        15,0,255,3,0,0,2,0,0,0,0,0,0,0};
    memset(actual,0xa5,sizeof(actual));
    assert(mt7902_encode_edca(actual+1,44,0,false,wmm,sizeof(wmm))==44);
    assert(!memcmp(actual+1,dcf,44) && actual[0]==0xa5 && actual[45]==0xa5);
    // WMM AC_BE/AC_BK/AC_VI/AC_VO -> firmware BK/BE/VI/VO record ordering.
    const uint8_t negotiated[44]={15,0,255,3,0,0,7,0,0,0,
        15,0,255,3,0,0,3,0,0,0,7,0,15,0,94,0,2,0,0,0,
        3,0,7,0,47,0,2,0,0,0,0,1,0,0};
    assert(mt7902_encode_edca(actual+1,44,0,true,wmm,sizeof(wmm))==44);
    assert(!memcmp(actual+1,negotiated,44));
    for (size_t n=0;n<44;n++) {
        memset(actual,0xa5,sizeof(actual));
        assert(!mt7902_encode_edca(actual+1,n,0,true,wmm,sizeof(wmm)));
        for (uint8_t byte:actual) assert(byte==0xa5);
    }
    for (size_t n=0;n<sizeof(wmm);n++) {
        memset(actual,0xa5,sizeof(actual));
        assert(!mt7902_encode_edca(actual+1,44,0,true,wmm,n));
        for (uint8_t byte:actual) assert(byte==0xa5);
    }
    uint8_t bad[sizeof(wmm)]; memcpy(bad,wmm,sizeof(bad));
    bad[14]=3; // duplicate ACI0 instead of ACI1
    assert(!mt7902_encode_edca(actual,44,0,true,bad,sizeof(bad)));
    memcpy(bad,wmm,sizeof(bad)); bad[11]=0x34; // CWmin exponent exceeds max
    assert(!mt7902_encode_edca(actual,44,0,true,bad,sizeof(bad)));
    assert(!mt7902_encode_edca(nullptr,44,0,false,nullptr,0));
    assert(!mt7902_encode_edca(actual,44,0,false,nullptr,1));
    assert(mt7902_encode_edca(actual,44,0,false,nullptr,0)==44);
}
static void test_legacy_rates()
{
    // Observed failure profile: all OFDM rates, no CCK and no basic markers.
    const uint8_t ofdm[]={1,8,12,18,24,36,48,72,96,108};
    Mt7902LegacyRates r={};
    for (uint8_t channel:{1,44}) {
        assert(mt7902_parse_legacy_rates(ofdm,sizeof(ofdm),channel,&r));
        assert(r.supported_cck==0 && r.supported_ofdm==255 && r.basic_cck==0);
        assert(r.basic_ofdm==1 && r.basic_fallback);
        uint8_t encoded[12]; memset(encoded,0xa5,sizeof(encoded));
        assert(mt7902_encode_legacy_rates(encoded+1,10,r)==10);
        assert(encoded[0]==0xa5 && encoded[11]==0xa5 && encoded[3]==0x8c);
        assert(!memcmp(encoded+4,ofdm+3,7));
    }
    // Hardware-working hotspot profile must retain the same association rates.
    const uint8_t mixed[]={1,8,0x82,0x84,0x8b,0x96,12,18,24,36,50,4,48,72,96,108};
    assert(mt7902_parse_legacy_rates(mixed,sizeof(mixed),1,&r));
    assert(r.supported_cck==15 && r.basic_cck==15 && !r.basic_ofdm && !r.basic_fallback);
    uint8_t encoded[18]; memset(encoded,0xa5,sizeof(encoded));
    assert(mt7902_encode_legacy_rates(encoded+1,16,r)==16);
    assert(!memcmp(encoded+1,mixed,16) && encoded[0]==0xa5 && encoded[17]==0xa5);
    for (size_t cap=0;cap<16;cap++) {
        memset(encoded,0xa5,sizeof(encoded));
        assert(!mt7902_encode_legacy_rates(encoded+1,cap,r));
        for (uint8_t b:encoded) assert(b==0xa5);
    }
    // Lowest actual bitrate, not first array entry: 6 Mbps beats CCK 11 Mbps.
    const uint8_t unsorted[]={1,2,22,12};
    assert(mt7902_parse_legacy_rates(unsorted,sizeof(unsorted),1,&r));
    assert(r.basic_cck==0 && r.basic_ofdm==1 && r.basic_fallback);
    const uint8_t marked[]={1,3,12,0x98,48};
    assert(mt7902_parse_legacy_rates(marked,sizeof(marked),44,&r));
    assert(r.basic_ofdm==4 && !r.basic_fallback);
    const uint8_t cckOnly[]={1,2,0x82,0x84}, selectorOnly[]={1,1,255}, truncated[]={1,8,12};
    assert(!mt7902_parse_legacy_rates(cckOnly,sizeof(cckOnly),44,&r));
    assert(!mt7902_parse_legacy_rates(selectorOnly,sizeof(selectorOnly),1,&r));
    assert(!mt7902_parse_legacy_rates(truncated,sizeof(truncated),1,&r));
    assert(!mt7902_parse_legacy_rates(ofdm,1,1,&r));
    assert(!mt7902_parse_legacy_rates(nullptr,0,1,&r));
    assert(!mt7902_parse_legacy_rates(ofdm,sizeof(ofdm),0,&r));
    assert(!mt7902_parse_legacy_rates(ofdm,sizeof(ofdm),1,nullptr));
    const uint8_t values[]={2,4,11,22,12,18,24,36,48,72,96,108};
    // Every nonempty legacy mask: inferred basic rate belongs to the offered set
    // and is its slowest bitrate. Encoded association advertises no extra rates.
    for (unsigned mask=1;mask<4096;mask++) {
        uint8_t ies[14]={1,0}; unsigned minimum=255;
        for (unsigned i=0;i<12;i++) if (mask&(1u<<i)) {
            ies[2+ies[1]++]=values[i];
            if (values[i]<minimum) minimum=values[i];
        }
        assert(mt7902_parse_legacy_rates(ies,(size_t)ies[1]+2,1,&r));
        assert(r.supported_cck==(mask&15) && r.supported_ofdm==(mask>>4));
        assert(r.basic_fallback);
        unsigned basics=0;
        for (unsigned i=0;i<12;i++) if ((i<4?r.basic_cck:r.basic_ofdm)&(1u<<(i<4?i:i-4))) {
            assert(values[i]==minimum); basics++;
        }
        assert(basics==1);
        uint8_t wire[16]; const size_t bytes=mt7902_encode_legacy_rates(wire,sizeof(wire),r);
        Mt7902LegacyRates roundtrip={};
        assert(bytes && mt7902_parse_legacy_rates(wire,bytes,1,&roundtrip));
        assert(roundtrip.supported_cck==r.supported_cck && roundtrip.supported_ofdm==r.supported_ofdm);
        assert(roundtrip.basic_cck==r.basic_cck && roundtrip.basic_ofdm==r.basic_ofdm && !roundtrip.basic_fallback);
    }
}
static void test_assoc_negotiation()
{
    // Independent association response: fixed body, AID with reserved bits,
    // Supported Rates, then an AP's WMM Parameter element.
    uint8_t response[60]={0x10}; response[28]=5; response[29]=0xc0;
    response[30]=1; response[31]=2; response[32]=0x8c; response[33]=0x98;
    const uint8_t wmm[26]={221,24,0,0x50,0xf2,2,1,1,0x80,0,
        3,0xa4,0,0,0x27,0xa4,0,0,0x42,0x43,0x5e,0,0x62,0x32,0x2f,0};
    memcpy(response+34,wmm,sizeof(wmm));
    Mt7902AssocResponse parsed={};
    assert(mt7902_decode_assoc_response(response,sizeof(response),&parsed));
    assert(!parsed.status && parsed.aid==5 && parsed.wmm);
    // A legacy success has no WMM; HT requested by us must not invent it.
    assert(mt7902_decode_assoc_response(response,34,&parsed) && !parsed.wmm && parsed.aid==5);
    // An AP rejection remains a rejection, irrespective of optional IEs.
    response[26]=17;
    assert(mt7902_decode_assoc_response(response,30,&parsed) && parsed.status==17 && !parsed.wmm);
    response[26]=0;
    // Every partial WMM element is rejected atomically, not treated as WMM acceptance.
    for (size_t n=35;n<sizeof(response);n++) {
        parsed={0x1234,0x5678,true};
        assert(!mt7902_decode_assoc_response(response,n,&parsed));
        assert(parsed.status==0x1234 && parsed.aid==0x5678 && parsed.wmm);
    }
    for (size_t n=0;n<30;n++) assert(!mt7902_decode_assoc_response(response,n,&parsed));
    assert(!mt7902_decode_assoc_response(nullptr,sizeof(response),&parsed));
    assert(!mt7902_decode_assoc_response(response,sizeof(response),nullptr));
    response[0]=0xb0;
    assert(!mt7902_decode_assoc_response(response,sizeof(response),&parsed));
    response[0]=0x10;
    // A WMM Information IE is not the AP's negotiated WMM Parameter IE.
    uint8_t info[39]={0x10};
    const uint8_t information[]={221,7,0,0x50,0xf2,2,0,1,0};
    memcpy(info+30,information,sizeof(information));
    assert(mt7902_decode_assoc_response(info,sizeof(info),&parsed) && !parsed.wmm);
    // Unrelated vendor OUI/type, unknown WMM version: no negotiated WMM.
    response[36]=1;
    assert(mt7902_decode_assoc_response(response,sizeof(response),&parsed) && !parsed.wmm);
    response[36]=0; response[41]=2;
    assert(mt7902_decode_assoc_response(response,sizeof(response),&parsed) && !parsed.wmm);
    response[41]=1;
    response[35]=23;
    assert(!mt7902_decode_assoc_response(response,59,&parsed));
    // Deterministic adversarial IE bytes: exercise bounds independently of flags.
    uint32_t seed=0x790214;
    for (unsigned n=30;n<256;n++) {
        uint8_t fuzz[256]={0x10};
        for (unsigned i=30;i<n;i++) { seed=seed*1664525u+1013904223u; fuzz[i]=(uint8_t)(seed>>24); }
        (void)mt7902_decode_assoc_response(fuzz,n,&parsed);
    }
}
static void test_roc_grant()
{
    // mt7921_roc_grant_tlv is 20 bytes; the ACQUIRE request is a different
    // 24-byte layout. Independent packed grant fixture after rxd36 + hdr4.
    uint8_t event[60]={60}; event[3]=0x38; event[28]=0x27; event[30]=4;
    const uint8_t grantWire[]={0,0,20,0,0,7,0,44,0,2,0,44,0,0,255,0,0x88,0x13,0,0};
    memcpy(event+40,grantWire,sizeof(grantWire));
    Mt7902RocGrant grant={};
    assert(mt7902_decode_roc_grant(event,sizeof(event),0,7,&grant));
    assert(!grant.bss && grant.token==7 && !grant.status && grant.channel==44);
    assert(grant.band==2 && !grant.width && grant.center==44 && !grant.center2);
    assert(!grant.request_type && grant.interval_ms==5000);
    event[56]=0xe8; event[57]=3; event[47]=1; event[49]=1; event[51]=1;
    assert(mt7902_decode_roc_grant(event,sizeof(event),0,7,&grant));
    assert(grant.interval_ms==1000 && grant.channel==1 && grant.band==1);
    // Status and interval are observations, including rejection/zero lifetime.
    event[46]=1; event[56]=event[57]=0;
    assert(mt7902_decode_roc_grant(event,sizeof(event),0,7,&grant));
    assert(grant.status==1 && !grant.interval_ms);
    for (size_t n=0;n<60;n++) {
        grant.interval_ms=0xa5a5a5a5;
        assert(!mt7902_decode_roc_grant(event,n,0,7,&grant));
        assert(grant.interval_ms==0xa5a5a5a5);
    }
    assert(!mt7902_decode_roc_grant(nullptr,60,0,7,&grant));
    assert(!mt7902_decode_roc_grant(event,60,0,7,nullptr));
    assert(!mt7902_decode_roc_grant(event,60,1,7,&grant));
    assert(!mt7902_decode_roc_grant(event,60,0,8,&grant));
    for (unsigned offset:{3u,28u,40u,42u,43u}) {
        uint8_t bad[60]; memcpy(bad,event,60); bad[offset]^=0x80;
        assert(!mt7902_decode_roc_grant(bad,60,0,7,&grant));
    }
    event[30]=0; assert(!mt7902_decode_roc_grant(event,60,0,7,&grant)); event[30]=4;
    event[42]=19; assert(!mt7902_decode_roc_grant(event,60,0,7,&grant));
    event[42]=21; assert(!mt7902_decode_roc_grant(event,60,0,7,&grant));
}
int main()
{
    test_peer_commands();
    test_legacy_rates();
    test_assoc_negotiation();
    test_roc_grant();
    const uint8_t rsn[]={48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0};
    assert(mt7902_join_rsn(rsn,sizeof(rsn)));
    assert(!mt7902_join_rsn(nullptr,sizeof(rsn)));
    for (size_t n=0;n<sizeof(rsn);n++) assert(!mt7902_join_rsn(rsn,n));
    uint8_t bad[sizeof(rsn)];
    for (unsigned offset: {2u,7u,13u,19u}) {
        memcpy(bad,rsn,sizeof(bad)); bad[offset]=0xff;
        assert(!mt7902_join_rsn(bad,sizeof(bad)));
    }
    memcpy(bad,rsn,sizeof(bad)); bad[20]=64; // PMF required.
    assert(!mt7902_join_rsn(bad,sizeof(bad)));
    memcpy(bad,rsn,sizeof(bad)); bad[8]=255; bad[9]=255;
    assert(!mt7902_join_rsn(bad,sizeof(bad)));
    uint8_t minimal[20]; memcpy(minimal,rsn,20); minimal[1]=18;
    assert(mt7902_join_rsn(minimal,sizeof(minimal)));
    uint8_t transition[26]={}; memcpy(transition,rsn,20);
    transition[1]=24; transition[14]=2; transition[20]=0; transition[21]=15;
    transition[22]=172; transition[23]=8; transition[24]=128; // PMF capable, PSK+SAE.
    assert(mt7902_join_rsn(transition,sizeof(transition)));
    uint8_t guarded[22];
    for (unsigned ch=0;ch<256;ch++) {
        const bool allowed=(ch>=1 && ch<=11) || ch==36 || ch==40 || ch==44 || ch==48 ||
            ch==149 || ch==153 || ch==157 || ch==161 || ch==165;
        assert(mt7902_join_channel((uint8_t)ch)==allowed);
        memset(guarded,0xa5,sizeof(guarded));
        size_t n=mt7902_encode_join_domain(guarded+1,20,(uint8_t)ch);
        assert(n==(allowed?20u:0u));
        assert(guarded[0]==0xa5 && guarded[21]==0xa5);
        if (n) {
            assert(guarded[13]==ch && guarded[5]==1 && guarded[6]==1 && guarded[7]==1);
            assert(guarded[ch<=14?9:10]==1);
            for (unsigned i=17;i<21;i++) assert(guarded[i]==0);
        }
    }
    assert(!mt7902_encode_join_domain(guarded,19,1));
    assert(!mt7902_encode_join_domain(nullptr,20,1));
    assert(mt7902_fixed_spe(1)==0 && mt7902_fixed_spe(2)==1 && mt7902_fixed_spe(3)==24);
    uint8_t frame[30]={0xb0},tx[32]; Mt7902TxOptions opts={};
    opts.sequence=-1; opts.spe_index=mt7902_fixed_spe(1);
    assert(mt7902_encode_txwi(tx,frame,sizeof(frame),opts));
    assert(!(tx[29]&0xf8)); // Fixed-rate auth uses WF0, not duplicate WF0+WF1.
    // Firmware disconnect fixture: mt76_connac_mcu_sta_cmd(enable=false).
    const uint8_t peer[]={2,0x11,0x22,0x33,0x44,0x55};
    const uint8_t removed[]={
        0,1,2,0,1,0,0,0,
        0,0,20,0,2,0,1,0,0,0,0,0,2,0x11,0x22,0x33,0x44,0x55,1,0,
        13,0,12,0,1,1,0,0,0,0,0,0
    };
    uint8_t removal[42]; memset(removal,0xa5,sizeof(removal));
    assert(mt7902_encode_sta_remove(removal+1,40,0,1,peer)==40);
    assert(!memcmp(removal+1,removed,sizeof(removed)));
    assert(removal[0]==0xa5 && removal[41]==0xa5);
    for (size_t n=0;n<40;n++) {
        memset(removal,0xa5,sizeof(removal));
        assert(!mt7902_encode_sta_remove(removal+1,n,0,1,peer));
        for (uint8_t v:removal) assert(v==0xa5);
    }
    assert(!mt7902_encode_sta_remove(nullptr,40,0,1,peer));
    assert(!mt7902_encode_sta_remove(removal,40,0,1,nullptr));
    // CE CHIP_CONFIG fixture from mt76_connac_mcu_set_deep_sleep(false).
    uint8_t awake[330]; memset(awake,0xa5,sizeof(awake));
    assert(mt7902_encode_keep_awake(awake+1,328)==328);
    assert(awake[0]==0xa5 && awake[329]==0xa5);
    const char command[]="KeepFullPwr 1";
    assert(!memcmp(awake+9,command,sizeof(command)));
    for (unsigned i=1;i<9;i++) assert(awake[i]==0);
    for (unsigned i=9+sizeof(command);i<329;i++) assert(awake[i]==0);
    for (size_t n=0;n<328;n++) {
        memset(awake,0xa5,sizeof(awake));
        assert(!mt7902_encode_keep_awake(awake+1,n));
        for (uint8_t v:awake) assert(v==0xa5);
    }
    assert(!mt7902_encode_keep_awake(nullptr,328));
    // Linux UNI DEV_INFO_ACTIVE fixture; a local unicast MAC is allowed.
    const uint8_t ownMac[]={2,0x11,0x22,0x33,0x44,0x55};
    const uint8_t deviceExpected[]={0,0,0,0,0,0,12,0,1,0,2,0x11,0x22,0x33,0x44,0x55};
    uint8_t device[18]; memset(device,0xa5,sizeof(device));
    assert(mt7902_encode_device_info(device+1,16,ownMac)==16);
    assert(!memcmp(device+1,deviceExpected,sizeof(deviceExpected)));
    assert(device[0]==0xa5 && device[17]==0xa5);
    for (size_t n=0;n<16;n++) {
        memset(device,0xa5,sizeof(device));
        assert(!mt7902_encode_device_info(device+1,n,ownMac));
        for (uint8_t v:device) assert(v==0xa5);
    }
    const uint8_t zeroMac[6]={},groupMac[]={1,0,0,0,0,1};
    const uint8_t broadcastMac[]={255,255,255,255,255,255};
    assert(!mt7902_encode_device_info(nullptr,16,ownMac));
    assert(!mt7902_encode_device_info(device,16,nullptr));
    assert(!mt7902_encode_device_info(device,16,zeroMac));
    assert(!mt7902_encode_device_info(device,16,groupMac));
    assert(!mt7902_encode_device_info(device,16,broadcastMac));
    uint8_t ht[30]; memset(ht,0xa5,sizeof(ht));
    const uint8_t htExpected[28]={45,26,12,0,20,255,0,0,0,0,0,0,0,0,0,0,0,1};
    assert(mt7902_encode_ht20(ht+1,28,255)==28);
    assert(!memcmp(ht+1,htExpected,28) && ht[0]==0xa5 && ht[29]==0xa5);
    assert(mt7902_ht20_rate(255)==65 && mt7902_ht20_rate(1)==6 && mt7902_ht20_rate(0)==0);
    for (unsigned mask=1;mask<256;mask++) {
        assert(mt7902_encode_ht20(ht+1,28,(uint8_t)mask)==28);
        assert(ht[6]==mask && ht[7]==0 && ht[18]==1);
        assert((ht[3]&0x63)==0 && ht[4]==0);
    }
    for (size_t n=0;n<28;n++) {
        memset(ht,0xa5,sizeof(ht));
        assert(!mt7902_encode_ht20(ht+1,n,255));
        for (uint8_t byte:ht) assert(byte==0xa5);
    }
    assert(!mt7902_encode_ht20(nullptr,28,255));
    assert(!mt7902_encode_ht20(ht,28,0));
    puts("join policy/RSN/domain/SPE/legacy rates/HT20 tests passed");
    uint8_t reply[44]={}; reply[3]=0x38; reply[28]=5;
    reply[36]=0x90; reply[37]=0x40; reply[38]=0x0e; reply[39]=0x82;
    reply[40]=0x78; reply[41]=0x56; reply[42]=0x34; reply[43]=0x12;
    uint32_t registerValue=0;
    assert(mt7902_decode_register_reply(reply,44,0x820e4090,&registerValue));
    assert(registerValue==0x12345678);
    for (size_t n=0;n<44;n++) {
        registerValue=0xa5a5a5a5;
        assert(!mt7902_decode_register_reply(reply,n,0x820e4090,&registerValue));
        assert(registerValue==0xa5a5a5a5);
    }
    assert(!mt7902_decode_register_reply(nullptr,44,0x820e4090,&registerValue));
    assert(!mt7902_decode_register_reply(reply,44,0x820e4094,&registerValue));
    assert(!mt7902_decode_register_reply(reply,44,0x820e4090,nullptr));
    reply[28]=1; assert(!mt7902_decode_register_reply(reply,44,0x820e4090,&registerValue));
    reply[28]=5; reply[30]=4;
    assert(!mt7902_decode_register_reply(reply,44,0x820e4090,&registerValue));
    reply[30]=0; reply[3]=0;
    assert(!mt7902_decode_register_reply(reply,44,0x820e4090,&registerValue));
    // Fixed upstream CHANNEL_SWITCH layout; 20 MHz, normal reason, band0.
    for (uint8_t channel: {uint8_t(1),uint8_t(44)}) {
        uint8_t actual[78],expected[76]={}; memset(actual,0xa5,sizeof(actual));
        expected[0]=expected[1]=channel; expected[3]=expected[4]=1;
        expected[10]=channel>14?1:0;
        assert(mt7902_encode_channel_switch(actual+1,76,channel)==76);
        assert(!memcmp(actual+1,expected,76));
        assert(actual[0]==0xa5 && actual[77]==0xa5);
        for (size_t n=0;n<76;n++) {
            memset(actual,0xa5,sizeof(actual));
            assert(!mt7902_encode_channel_switch(actual+1,n,channel));
            for (uint8_t v:actual) assert(v==0xa5);
        }
    }
    uint8_t channelRequest[76];
    assert(!mt7902_encode_channel_switch(nullptr,76,1));
    for (uint8_t channel: {uint8_t(0),uint8_t(14),uint8_t(52),uint8_t(255)})
        assert(!mt7902_encode_channel_switch(channelRequest,76,channel));
}
