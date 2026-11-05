// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>

// Caller supplies one lock around every access. Entries contain copied values,
// never stack/node pointers. Reserved capacity keeps TX from starving controls.
template<class T,unsigned Capacity,unsigned Reserved=0> class Mt7902NetMailbox {
    static_assert(Capacity && !(Capacity&(Capacity-1)) && Reserved<Capacity,"power-of-two mailbox capacity");
    T values_[Capacity] = {};
    uint32_t head_=0,tail_=0;
    static void erase(T &value) {
        volatile uint8_t *bytes=reinterpret_cast<volatile uint8_t *>(&value);
        for(size_t i=0;i<sizeof(T);i++)bytes[i]=0;
    }
public:
    bool push(const T &value,bool control=true) {
        if(head_-tail_>=Capacity-(control?0:Reserved))return false;
        values_[head_++&(Capacity-1)]=value;return true;
    }
    bool pop(T &out) {
        if(head_==tail_)return false;
        auto &value=values_[tail_++&(Capacity-1)];out=value;erase(value);return true;
    }
    void clear() {
        for(auto &value:values_)erase(value);
        tail_=head_;
    }
    unsigned size() const {return head_-tail_;}
};
