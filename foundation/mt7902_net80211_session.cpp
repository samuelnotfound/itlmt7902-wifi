// SPDX-License-Identifier: GPL-2.0-or-later
#include "mt7902_net80211_session.h"

bool Mt7902NetSession::begin(bool protected_network)
{
    if (active_ || !quiesced_ || generation_==UINT32_MAX) return false;
    ++generation_; serial_=0;
    active_=true; quiesced_=false; protected_=protected_network;
    portValid_=failed_=false; state_=requested_=Init; pendingState_=0;
    for (auto &key:keys_) key=Key{};
    return true;
}
uint64_t Mt7902NetSession::ticket()
{
    if (!active_ || failed_ || serial_==UINT32_MAX) return 0;
    return (uint64_t(generation_)<<32) | ++serial_;
}
uint64_t Mt7902NetSession::requestState(State state)
{
    if (state>Run || !active_ || failed_) return 0;
    if (state<Assoc) for (const auto &key:keys_) if (key.pending) return 0;
    // No successful hardware association may be inferred from a RUN request.
    if ((state==Assoc && state_!=Auth) || (state==Run && state_!=Assoc && state_!=Run)) return 0;
    const uint64_t value=ticket();
    if (!value) return 0;
    pendingState_=value; requested_=state;
    if (state<Assoc) {
        portValid_=false;
        for (auto &key:keys_) key.installed=false;
    }
    return value;
}
bool Mt7902NetSession::completeState(uint64_t value,bool success)
{
    if (!active_ || !value || value!=pendingState_) return false;
    pendingState_=0;
    if (!success) { failed_=true; portValid_=false; return true; }
    state_=requested_;
    return true;
}
uint64_t Mt7902NetSession::requestKey(bool pairwise,uint8_t key_id,bool remove)
{
    if (!protected_ || !active_ || failed_ || key_id>3 ||
        (pairwise && key_id!=0) || state_<Assoc) return 0;
    Key &key=keys_[pairwise?0:key_id+1];
    // Do not overwrite an uncompleted firmware operation on the same key slot.
    if (key.pending) return 0;
    const uint64_t value=ticket();
    if (!value) return 0;
    key.pending=value; key.removing=remove;
    if (remove) key.installed=false;
    return value;
}
bool Mt7902NetSession::completeKey(uint64_t value,bool success)
{
    if (!active_ || !value || uint32_t(value>>32)!=generation_) return false;
    for (auto &key:keys_) if (key.pending==value) {
        key.pending=0;
        if (!success) { failed_=true; portValid_=false; key.installed=false; }
        else key.installed=!key.removing;
        return true;
    }
    return false;
}
void Mt7902NetSession::cancel()
{
    active_=false; portValid_=false;
    // Do not claim that queued firmware work or DMA ownership has ended here.
}
bool Mt7902NetSession::quiesced()
{
    if (active_) return false;
    quiesced_=true; pendingState_=0; state_=Init;
    for (auto &key:keys_) key=Key{};
    return true;
}
bool Mt7902NetSession::linkReady() const
{
    if (!active_ || failed_ || pendingState_ || state_!=Run || !portValid_) return false;
    if (!protected_) return true;
    bool group=false;
    for (unsigned i=0;i<5;i++) {
        if (keys_[i].pending) return false;
        if (i && keys_[i].installed) group=true;
    }
    return keys_[0].installed && group;
}
