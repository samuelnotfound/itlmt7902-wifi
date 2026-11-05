// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>

// Serialized by the stack gate. Hardware callbacks may be queued elsewhere,
// but results must be marshalled back through the same gate before using this.
// This guard owns no DMA or mbufs: quiesced() requires an actual backend barrier.
class Mt7902NetSession {
public:
    enum State : uint8_t { Init, Scan, Auth, Assoc, Run };
    bool begin(bool protected_network);
    uint64_t requestState(State state);
    bool completeState(uint64_t ticket, bool success);
    uint64_t requestKey(bool pairwise, uint8_t key_id, bool remove);
    bool completeKey(uint64_t ticket, bool success);
    void setPortValid(bool valid) { portValid_=valid; }
    void cancel();
    bool quiesced();
    bool linkReady() const;
    bool active() const { return active_; }
    uint32_t generation() const { return generation_; }
    State state() const { return state_; }
private:
    uint64_t ticket();
    struct Key {
        uint64_t pending=0;
        bool installed=false, removing=false;
    } keys_[5] = {};
    uint32_t generation_=0, serial_=0;
    bool active_=false, quiesced_=true, protected_=false;
    bool portValid_=false, failed_=false;
    State state_=Init, requested_=Init;
    uint64_t pendingState_=0;
};
