// SPDX-License-Identifier: GPL-2.0-or-later
#include "../foundation/net80211_mailbox.h"
#include <cassert>
#include <cstdio>
#include <cstring>
struct Command {uint32_t id;char key[32];};
int main()
{
    Mt7902NetMailbox<Command,32,4> queue;
    Command value={123,"fixture-password-not-real"},out={999,{}};
    assert(!queue.pop(out) && out.id==999);
    for(unsigned i=0;i<28;i++){value.id=i;assert(queue.push(value,false));}
    assert(!queue.push(value,false));
    for(unsigned i=28;i<32;i++){value.id=i;assert(queue.push(value,true));}
    assert(!queue.push(value,true) && queue.size()==32);
    for(unsigned i=0;i<32;i++){assert(queue.pop(out) && out.id==i);assert(!strcmp(out.key,value.key));}
    assert(queue.size()==0 && !queue.pop(out));
    for(unsigned cycle=0;cycle<10000;cycle++) {
        for(unsigned i=0;i<32;i++){value.id=cycle*32+i;assert(queue.push(value));}
        for(unsigned i=0;i<32;i++){assert(queue.pop(out) && out.id==cycle*32+i);}
    }
    assert(queue.push(value));queue.clear();assert(!queue.pop(out));
    const auto *memory=reinterpret_cast<const unsigned char *>(&queue);
    bool found=false;
    for(size_t i=0;i+sizeof(value.key)<=sizeof(queue);i++)
        if(!memcmp(memory+i,value.key,sizeof(value.key)))found=true;
    assert(!found); // Queued credentials are scrubbed on cancellation.
    puts("Passed net80211 FIFO bounds, control reservations, wrap/reuse and cancellation scrubbing");
}
