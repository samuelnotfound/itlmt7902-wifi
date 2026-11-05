// SPDX-License-Identifier: GPL-2.0-or-later
#include "../foundation/net80211_recovery.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main()
{
    // Replay a UI polling burst after a failed bootstrap: 100 polls cannot
    // trigger another reset during the mandatory five-second cooldown.
    Mt7902NetRecovery radio;
    assert(radio.canAttempt(1000));
    radio.failed(1000);
    for(unsigned poll=0;poll<100;poll++)assert(!radio.canAttempt(1000+poll*49));
    assert(!radio.canAttempt(5999) && radio.canAttempt(6000));
    radio.failed(6000);
    for(uint32_t now:{6000u,11000u,1000000u,0xffffffffu})assert(!radio.canAttempt(now));
    assert(radio.terminal() && radio.failures()==2);
    // An actual successful reload clears the consecutive-failure budget.
    radio.succeeded(); assert(!radio.terminal() && radio.canAttempt(1));
    radio.failed(100);radio.succeeded();radio.failed(200);
    assert(radio.failures()==1 && radio.canAttempt(5200));
    // Uptime rollover must neither skip cooldown nor permanently block retry.
    radio.succeeded();radio.failed(0xfffffff0u);
    assert(!radio.canAttempt(0xfffffff0u) && !radio.canAttempt(4983));
    assert(radio.canAttempt(4984));
    puts("Passed bootstrap-failure polling bursts, terminal budget, successful reload and clock wrap");
}
