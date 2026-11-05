// SPDX-License-Identifier: GPL-2.0-or-later
#include "../foundation/mt7902_net80211_session.h"
#include <cassert>
#include <cstdio>
using Session=Mt7902NetSession;
static void advance(Session &s,Session::State state)
{
    const auto ticket=s.requestState(state);
    assert(ticket && s.completeState(ticket,true));
    assert(!s.completeState(ticket,true));
}
int main()
{
    Session s;
    assert(!s.linkReady() && !s.requestState(Session::Auth));
    assert(s.begin(true) && !s.begin(false));
    assert(!s.requestState(Session::Run));
    advance(s,Session::Scan); advance(s,Session::Auth);
    assert(!s.requestKey(true,0,false));
    advance(s,Session::Assoc); advance(s,Session::Run);
    s.setPortValid(true);
    assert(!s.linkReady()); // Stack RUN/port-valid cannot outrun hardware keys.
    const auto pair=s.requestKey(true,0,false),group=s.requestKey(false,1,false);
    assert(pair && group && pair!=group);
    assert(!s.requestState(Session::Auth) && !s.requestState(Session::Scan));
    assert(!s.requestState(Session::Init)); // Cancel+quiesce is required here.
    assert(!s.requestKey(true,0,false));
    assert(!s.requestKey(false,4,false) && !s.requestKey(true,1,false));
    assert(s.completeKey(group,true) && !s.linkReady());
    assert(s.completeKey(pair,true) && s.linkReady());
    assert(!s.completeKey(pair,true));
    const auto replacement=s.requestKey(false,2,false);
    assert(!s.linkReady());
    assert(s.completeKey(replacement,true) && s.linkReady());
    const auto remove=s.requestKey(false,1,true);
    assert(!s.linkReady() && s.completeKey(remove,true) && s.linkReady());
    const auto failed=s.requestKey(true,0,false);
    assert(s.completeKey(failed,false) && !s.linkReady());
    s.cancel();
    assert(!s.begin(false)); // A disconnect request is not a hardware barrier.
    assert(!s.completeKey(pair,true) && !s.linkReady());
    assert(s.quiesced() && s.begin(false));
    assert(!s.completeKey(group,true)); // Old epoch cannot authorize a new link.
    advance(s,Session::Scan); advance(s,Session::Auth); advance(s,Session::Assoc);
    s.setPortValid(true);
    assert(!s.linkReady() && !s.requestKey(true,0,false));
    advance(s,Session::Run); assert(s.linkReady());
    const auto old=s.requestState(Session::Auth);
    const auto current=s.requestState(Session::Init);
    assert(old && current && !s.completeState(old,true));
    assert(s.completeState(current,true) && !s.linkReady());
    s.cancel(); assert(s.quiesced() && s.begin(true));
    const auto cancelled=s.requestState(Session::Scan);
    s.cancel(); assert(!s.completeState(cancelled,true));
    assert(s.quiesced() && s.begin(true));
    assert(!s.completeState(cancelled,true));
    // Exercise many generations/reordered completions without token reuse.
    uint64_t previous=cancelled;
    for (unsigned i=0;i<10000;i++) {
        const auto next=s.requestState(Session::Scan);
        assert(next>previous && !s.completeState(previous,true));
        s.cancel(); assert(!s.completeState(next,true));
        assert(s.quiesced() && s.begin(true)); previous=next;
    }
    puts("Passed net80211 session epochs, cancellation, association and controlled-port/key completion gates");
}
