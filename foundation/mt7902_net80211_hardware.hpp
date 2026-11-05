// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "net80211_bridge.hpp"
#include <kern/thread.h>

class itlmt7902;

// Construct and call only on the radio worker. This executor never calls back
// into net80211/the stack gate; the separate coordinator owns those completions.
// It is compiled/linked for validation but is not activated by current runtime.
class Mt7902Net80211Hardware {
public:
    explicit Mt7902Net80211Hardware(itlmt7902 &);
    int bindSession(uint32_t epoch);
    int changeState(uint32_t epoch,const Mt7902Net80211State &);
    int updateKey(uint32_t epoch,const Mt7902Net80211Key &);
    int transmit(uint32_t epoch,const uint8_t *,size_t,const Mt7902Net80211Tx &);
    int setAuthorized(uint32_t epoch,bool authorized);
    // Does not stop DMA itself. Requires the owner already proved bus mastering
    // disabled and released its DMA rings through quiesceHardware().
    int releaseAfterQuiesce();
private:
    itlmt7902 &driver_;
    thread_t owner_;
    uint32_t epoch_=0;
    ieee80211_state state_=IEEE80211_S_INIT;
    bool prepared_=false, associated_=false, privacy_=false;
    uint8_t bssid_[6]={};
    uint8_t channel_=0;
    struct InstalledKey { bool valid=false; uint8_t id=0, bytes[16]={}; } pair_,group_;
    int check(uint32_t epoch) const;
    bool grantUsable() const;
    int prepare(const Mt7902Net80211State &);
    int cleanup();
    int disableBss();
    int clearKey(bool group);
    int failed(int error);
    void forgetKeys();
};
