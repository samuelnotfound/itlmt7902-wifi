// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>

// Failed bootstrap is different from a failed association. Do not let UI polls
// repeatedly reload a device which failed to return to an operational state.
class Mt7902NetRecovery {
public:
    bool canAttempt(uint32_t now) const {
        return !terminal() && (!failures_ || (int32_t)(now-retryAt_)>=0);
    }
    void failed(uint32_t now) {
        if(failures_<2)++failures_;
        retryAt_=now+5000;
    }
    void succeeded() { failures_=0;retryAt_=0; }
    bool terminal() const { return failures_>=2; }
    unsigned failures() const { return failures_; }
private:
    uint32_t retryAt_=0;
    unsigned failures_=0;
};
