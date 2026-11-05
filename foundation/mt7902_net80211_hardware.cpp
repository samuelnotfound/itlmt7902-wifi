// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/itlmt7902.hpp"
#include "mt7902_net80211_hardware.hpp"
#include "mt7902_net80211_key.h"
#include "../codec/mt7902_join_codec.h"
#include <sys/errno.h>
#include <string.h>

namespace {
bool validAddress(const uint8_t *address)
{
    uint8_t any=0;
    for (unsigned i=0;i<6;i++) any|=address[i];
    return any && !(address[0]&1);
}
bool legacyRate(uint8_t rate)
{
    static const uint8_t values[]={2,4,11,22,12,18,24,36,48,72,96,108};
    for (uint8_t value:values)
        if ((rate&0x7f)==value) return true;
    return false;
}
void put16(uint8_t *out,uint16_t value)
{
    out[0]=(uint8_t)value; out[1]=(uint8_t)(value>>8);
}
void put32(uint8_t *out,uint32_t value)
{
    for (unsigned i=0;i<4;i++) out[i]=(uint8_t)(value>>(8*i));
}
}

Mt7902Net80211Hardware::Mt7902Net80211Hardware(itlmt7902 &driver)
    :driver_(driver),owner_(current_thread()) {}

int Mt7902Net80211Hardware::check(uint32_t epoch) const
{
    if (current_thread()!=owner_) return EPERM;
    if (!epoch || epoch!=epoch_) return ESTALE;
    if (driver_.shouldStop() || driver_.firmwareFault || !driver_.busMasterOn)
        return ENXIO;
    return 0;
}
int Mt7902Net80211Hardware::bindSession(uint32_t epoch)
{
    if (current_thread()!=owner_) return EPERM;
    if (!epoch) return EINVAL;
    if (epoch_) return EBUSY;
    if (!driver_.workerRunning || driver_.shouldStop() || driver_.firmwareFault ||
        !driver_.busMasterOn || !driver_.regs || !driver_.txData.n ||
        driver_.nativeMode || driver_.diagnosticStage<3 || !driver_.m3On) return ENXIO;
    // The future coordinator must have taken the old supervisor out of service.
    // Never take over an association/handshake or old queued join in flight.
    if (driver_.m3prepared || driver_.m3linkUp || driver_.m3dataOn ||
        driver_.m3wantFc || driver_.m3wantEapol ||
        driver_.cmdPending(itlmt7902::CMD_JOIN|itlmt7902::CMD_DISCONNECT)) return EBUSY;
    epoch_=epoch; state_=IEEE80211_S_INIT;
    prepared_=associated_=privacy_=touched_=false; channel_=0;
    bzero(bssid_,sizeof(bssid_)); forgetKeys();
    return 0;
}
void Mt7902Net80211Hardware::forgetKeys()
{
    bzero(&pair_,sizeof(pair_)); bzero(&group_,sizeof(group_));
}
int Mt7902Net80211Hardware::failed(int error)
{
    driver_.m3linkUp=false; driver_.m3dataOn=false;
    driver_.m3wantEapol=false; driver_.ethLink(false);
    // Uncertain peer/key state cannot be reused by a subsequent association.
    driver_.recoverWanted=true; driver_.firmwareFault=true;
    return error?error:EIO;
}
bool Mt7902Net80211Hardware::grantUsable() const
{
    const Mt7902RocGrant &g=driver_.lastRocGrant;
    const uint32_t elapsed=driver_.nowMs()-driver_.lastRocGrantMs;
    return driver_.lastRocGrantValid && g.bss==0 && g.token==driver_.rocTok &&
        g.status==0 && g.channel==channel_ && g.band==(channel_>14?2:1) &&
        g.width==0 && g.center==channel_ && !g.center2 && !g.request_type &&
        g.interval_ms>=50 && elapsed<g.interval_ms && g.interval_ms-elapsed>=25;
}
int Mt7902Net80211Hardware::prepare(const Mt7902Net80211State &s)
{
    if (!validAddress(s.mac) || memcmp(s.mac,driver_.mac,6) ||
        !validAddress(s.bssid) || !s.ssid_length || s.ssid_length>32 ||
        memchr(s.ssid,0,s.ssid_length) || !mt7902_join_channel(s.channel) ||
        !(s.capability&1) || (s.capability&2) ||
        s.privacy!=!!(s.capability&0x10) || !s.beacon_interval ||
        !s.rates.rs_nrates || s.rates.rs_nrates>sizeof(s.rates.rs_rates)) return EINVAL;
    if (driver_.nicCaps.nss!=1 || driver_.nicCaps.chain_mask!=1 ||
        !(driver_.nicCaps.bands&(s.channel>14?2:1))) return EOPNOTSUPP;
    // Existing Connac2 RLM programming uses short slot. Do not silently claim
    // long-slot/HT support until the backend can program that negotiated state.
    // AUTH precedes negotiation of the stack's slot flag; use the AP capability.
    if (s.channel<=14 && !(s.capability&0x400)) return EOPNOTSUPP;
    uint8_t ies[2+IEEE80211_RATE_MAXSIZE]={1,s.rates.rs_nrates};
    for (unsigned i=0;i<s.rates.rs_nrates;i++) {
        if (!legacyRate(s.rates.rs_rates[i])) return EOPNOTSUPP;
        ies[2+i]=s.rates.rs_rates[i];
    }
    Mt7902LegacyRates rates={};
    if (!mt7902_parse_legacy_rates(ies,(size_t)s.rates.rs_nrates+2,s.channel,&rates)) return EINVAL;
    // Keep this latched even after a peer is removed: old TX DMA may still exist.
    touched_=true;
    memcpy(bssid_,s.bssid,6); channel_=s.channel; privacy_=s.privacy;
    memcpy(driver_.tgtBssid,s.bssid,6); driver_.tgtCh=s.channel;
    driver_.tgtCap=s.capability; driver_.tgtBcnInt=s.beacon_interval;
    memcpy(driver_.m3ssid,s.ssid,s.ssid_length); driver_.m3ssid[s.ssid_length]=0;
    driver_.apSupp=rates.supported_ofdm; driver_.apBasic=rates.basic_ofdm;
    driver_.apCck=rates.supported_cck; driver_.apBasicCck=rates.basic_cck;
    driver_.fastActive=false; driver_.peerQos=false; driver_.authConnect=false;
    bzero(driver_.peerWmm,sizeof(driver_.peerWmm));
    driver_.m3Open=!privacy_; driver_.m3cipher=0;
    driver_.m3wantFc=0; driver_.m3wantEapol=false; driver_.m3dataOn=false;
    driver_.m3linkUp=false; driver_.m3reachedAssoc=false;
    int rc=driver_.m3HwPrep();
    if (rc) return failed(EIO);
    if (!driver_.m3devReady) {
        uint8_t request[16];
        const size_t length=mt7902_encode_device_info(request,sizeof(request),driver_.mac);
        if (!length || driver_.m3Cmd(0x01,request,length,"net80211-device")) return failed(EIO);
        driver_.m3devReady=true;
    }
    if (driver_.m3SetChannel()) return failed(EIO);
    // From here cleanup must run even if the next firmware operation fails.
    prepared_=driver_.m3prepared=true;
    if (driver_.m3Bss(false) || driver_.m3Sta(0,true,0) ||
        driver_.m3AcquireRoc() || !grantUsable()) return failed(EIO);
    return 0;
}
int Mt7902Net80211Hardware::disableBss()
{
    // BSS_INFO basic with active=0 and disconnected=1. m3Bss(false) is an
    // active add and must not be used to disable a departing stack-owned BSS.
    uint8_t request[36]={}; uint8_t *b=request+4;
    put16(b,0); put16(b+2,32); b[4]=0; put32(b+8,0x10001); b[12]=1;
    memcpy(b+14,bssid_,6); put16(b+20,19); put16(b+26,19);
    return driver_.m3Cmd(0x02,request,sizeof(request),"net80211-bss-disable")?EIO:0;
}
int Mt7902Net80211Hardware::clearKey(bool group)
{
    Mt7902Net80211KeyCommand command={};
    command.operation=MT7902_NET80211_KEY_CLEAR_WCID;
    command.wcid=group?19:1; command.pairwise=!group;
    uint8_t request[16];
    const size_t length=mt7902_net80211_encode_key(request,sizeof(request),command);
    return !length || driver_.m3Cmd(0x03,request,length,"net80211-key-clear")?EIO:0;
}
int Mt7902Net80211Hardware::cleanup()
{
    driver_.m3linkUp=false; driver_.m3dataOn=false; driver_.m3wantEapol=false;
    driver_.m3wantFc=0; driver_.ethLink(false);
    if (prepared_) {
        if (driver_.m3Roc(false) || driver_.m3StaRemove()) return failed(EIO);
        // STA remove reset the pairwise WTBL. Clear the independent group WCID.
        if (group_.valid && clearKey(true)) return failed(EIO);
        if (disableBss()) return failed(EIO);
    }
    prepared_=associated_=false; driver_.m3prepared=false;
    driver_.m3reachedAssoc=false; driver_.m3cipher=0; driver_.m3Open=false;
    driver_.peerQos=false; driver_.authConnect=false;
    forgetKeys();
    return 0;
}
int Mt7902Net80211Hardware::changeState(uint32_t epoch,const Mt7902Net80211State &s)
{
    const int valid=check(epoch); if (valid) return valid;
    if (s.state==IEEE80211_S_INIT || s.state==IEEE80211_S_SCAN) {
        const int result=cleanup();
        if (!result) state_=s.state;
        // SCAN acknowledgement precedes asynchronous all-channel scan/RX.
        // The coordinator must schedule that scan after entering stack SCAN.
        return result;
    }
    if (s.state==IEEE80211_S_AUTH) {
        if (prepared_) { const int result=cleanup(); if (result) return result; }
        const int result=prepare(s);
        if (!result) state_=IEEE80211_S_AUTH;
        return result;
    }
    if (!prepared_ || s.channel!=channel_ || memcmp(s.bssid,bssid_,6) ||
        memcmp(s.mac,driver_.mac,6) || s.privacy!=privacy_) return EINVAL;
    if (s.state==IEEE80211_S_ASSOC) {
        if (state_!=IEEE80211_S_AUTH) return EINVAL;
        // AUTH and ASSOC are one managed exchange. Keep its live JOIN grant;
        // aborting/reacquiring here tears down the channel context mid-exchange.
        // Like mt7921_mac_sta_event, update firmware to ASSOC only after the
        // association response (net80211 RUN), not on a local ASSOC request.
        if (!grantUsable()) return ETIMEDOUT;
        state_=IEEE80211_S_ASSOC; return 0;
    }
    if (s.state==IEEE80211_S_RUN) {
        if(channel_<=14 && !s.short_slot)return EOPNOTSUPP;
        const uint16_t aid=s.association_id&0x3fff;
        if (state_!=IEEE80211_S_ASSOC || !aid || aid>2007 ||
            ((s.association_id&0xc000)!=0 && (s.association_id&0xc000)!=0xc000)) return EINVAL;
        driver_.m3aid=aid;
        if (driver_.m3Bss(true) || driver_.m3Sta(2,false,aid))
            return failed(EIO);
        associated_=true; driver_.m3reachedAssoc=true; state_=IEEE80211_S_RUN;
        return 0;
    }
    return EOPNOTSUPP;
}
int Mt7902Net80211Hardware::updateKey(uint32_t epoch,const Mt7902Net80211Key &key)
{
    const int valid=check(epoch); if (valid) return valid;
    if (!associated_ || !privacy_ || state_!=IEEE80211_S_RUN || key.length!=16 ||
        memcmp(key.peer,bssid_,6) || key.id>(key.group?3:0)) return EINVAL;
    InstalledKey &installed=key.group?group_:pair_;
    if (key.remove) {
        // Firmware clear is WCID-wide. An old GTK delete must not erase a
        // newer key sharing BMC19, including re-use of the same key ID.
        if (!installed.valid || installed.id!=key.id ||
            memcmp(installed.bytes,key.bytes,sizeof(installed.bytes))) return 0;
        driver_.m3linkUp=false; driver_.ethLink(false);
        if (clearKey(key.group)) return failed(EIO);
        bzero(&installed,sizeof(installed));
        return 0;
    }
    driver_.m3linkUp=false; driver_.ethLink(false);
    Mt7902Net80211KeyCommand command={};
    command.operation=MT7902_NET80211_KEY_INSTALL_CCMP;
    command.wcid=key.group?19:1; command.pairwise=!key.group;
    command.key_id=key.id; command.key_length=16; memcpy(command.key,key.bytes,16);
    uint8_t request[52];
    const size_t length=mt7902_net80211_encode_key(request,sizeof(request),command);
    const int result=!length || driver_.m3Cmd(0x03,request,length,"net80211-key-install")?EIO:0;
    bzero(command.key,sizeof(command.key)); bzero(request,sizeof(request));
    if (result) return failed(result);
    installed.valid=true; installed.id=key.id; memcpy(installed.bytes,key.bytes,16);
    driver_.m3cipher=4;
    return 0;
}
int Mt7902Net80211Hardware::setAuthorized(uint32_t epoch,bool authorized)
{
    const int valid=check(epoch); if (valid) return valid;
    if (authorized && (!associated_ || state_!=IEEE80211_S_RUN ||
        (privacy_ && (!pair_.valid || !group_.valid)))) return EACCES;
    // The MT7902 reference holds the JOIN grant across the 4-way handshake.
    // Release once authorization is complete, rather than on entering RUN.
    if (authorized && driver_.lastRocGrantValid && driver_.m3Roc(false)) return failed(EIO);
    driver_.m3linkUp=authorized;
    // The coordinator feeds raw RX exclusively to net80211. Old decapsulation
    // and old EAPOL queues must never observe frames in the new stack session.
    driver_.m3dataOn=false; driver_.m3wantEapol=false; driver_.m3wantFc=0;
    if (!authorized) driver_.ethLink(false);
    return 0;
}
int Mt7902Net80211Hardware::transmit(uint32_t epoch,const uint8_t *frame,size_t length,
                                   const Mt7902Net80211Tx &tx)
{
    const int valid=check(epoch); if (valid) return valid;
    if (!prepared_ || !frame || length<24 || length>1900 ||
        tx.channel!=channel_ || memcmp(tx.peer,bssid_,6) ||
        memcmp(frame+4,bssid_,6) || memcmp(frame+10,driver_.mac,6) ||
        (frame[0]&3) || (frame[1]&4) || (frame[22]&15) ||
        tx.sequence!=(uint16_t)((frame[22]|uint16_t(frame[23])<<8)>>4) ||
        tx.protected_frame!=!!(frame[1]&0x40)) return EINVAL;
    const uint8_t type=(frame[0]>>2)&3,subtype=frame[0]>>4;
    if (tx.management) {
        if (type!=0 || (frame[1]&0x43) || memcmp(frame+16,bssid_,6)) return EINVAL;
        if ((subtype==11 && state_!=IEEE80211_S_AUTH) ||
            (subtype==0 && state_!=IEEE80211_S_ASSOC) ||
            (subtype!=11 && subtype!=0 && subtype!=10 && subtype!=12)) return EOPNOTSUPP;
    } else {
        if (type!=2 || (frame[1]&3)!=1 || (subtype!=0 && subtype!=4) ||
            state_!=IEEE80211_S_RUN || (tx.protected_frame && (!privacy_ || !pair_.valid))) return EINVAL;
    }
    if (state_!=IEEE80211_S_RUN && !grantUsable()) return ETIMEDOUT;
    M3TxOpt options;
    options.protect=tx.protected_frame; options.sn=tx.sequence;
    options.rateAdapt=false; options.aggr=false; options.qidx=0x10;
    // Do not set options.full: it is the old Ethernet callback gate and would
    // reject pre-authorization EAPOL. m3Tx itself still permits only EAPOL there.
    const uint32_t noSlot=driver_.stTxNoSlot;
    const bool accepted=driver_.m3Tx(frame,(uint32_t)length,type,subtype,options);
    if (accepted) return 0; // The complete payload is now copied into owned DMA.
    return driver_.stTxNoSlot!=noSlot?EAGAIN:EIO;
}
int Mt7902Net80211Hardware::releaseUnusedScanSession()
{
    const int valid=check(epoch_); if(valid)return valid;
    if(touched_ || prepared_ || associated_ || state_>IEEE80211_S_SCAN ||
        pair_.valid || group_.valid) return EBUSY;
    epoch_=0;state_=IEEE80211_S_INIT;
    return 0;
}
int Mt7902Net80211Hardware::releaseAfterQuiesce()
{
    if (current_thread()!=owner_) return EPERM;
    if (!epoch_) return EINVAL;
    // Never turn a software declaration into a DMA ownership barrier.
    if (driver_.busMasterOn || driver_.txData.n || driver_.txData.buf.va ||
        driver_.txData.desc.va) return EBUSY;
    driver_.m3linkUp=false; driver_.m3dataOn=false; driver_.m3wantEapol=false;
    driver_.m3prepared=false; driver_.m3wantFc=0;
    prepared_=associated_=privacy_=touched_=false; epoch_=0; channel_=0;
    bzero(bssid_,sizeof(bssid_)); forgetKeys(); state_=IEEE80211_S_INIT;
    return 0;
}
