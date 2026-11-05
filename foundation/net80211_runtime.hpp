// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "net80211_bridge.hpp"
#include "net80211_mailbox.h"
#include "net80211_recovery.h"
#include "mt7902_net80211_hardware.hpp"
#include "mt7902_net80211_rx.h"
#include <IOKit/IOLocks.h>
#include <IOKit/network/IOGatedOutputQueue.h>

class itlmt7902;
// One radio worker executes firmware operations. The Ethernet workloop owns
// net80211; callbacks copy into bounded mailboxes and never wait for firmware.
class Mt7902Net80211Runtime : public Mt7902Net80211Controller {
    OSDeclareDefaultStructors(Mt7902Net80211Runtime)
public:
    bool setup(itlmt7902 *);
    bool start(IOService *) override;
    void stop(IOService *) override;
    void free() override;
    bool createWorkLoop() override;
    IOWorkLoop *getWorkLoop() const override { return loop_; }
    IOOutputQueue *createOutputQueue() override;
    bool configureInterface(IONetworkInterface *) override;
    IOReturn getHardwareAddress(IOEthernetAddress *) override;
    IOReturn enable(IONetworkInterface *) override;
    IOReturn disable(IONetworkInterface *) override;
    IOReturn setMulticastMode(bool) override;
    IOReturn setMulticastList(IOEthernetAddress *,UInt32) override;
    IOReturn getPacketFilters(const OSSymbol *,UInt32 *) const override;
    UInt32 outputPacket(mbuf_t,void *) override;
    void receiveDMA(const uint8_t *,uint32_t); // hardware worker only, including MCU waits
    void serve(); // hardware worker: consumes the existing HeliPort mailbox
private:
    friend class itlmt7902;
    enum Kind : uint8_t { State, Key, Tx };
    struct Command {
        Kind kind; uint64_t token; uint32_t epoch; uint16_t length;
        Mt7902Net80211State state; Mt7902Net80211Key key; Mt7902Net80211Tx tx;
        uint8_t frame[1900];
    };
    Mt7902NetMailbox<Command,32,4> commands_;
    Command current_ = {};
    struct Rx { Mt7902Net80211Rx metadata; uint32_t epoch; uint8_t frame[4096]; }
        incoming_ = {}, receive_ = {};
    Mt7902NetMailbox<Rx,64> receives_;
    IOLock *mailbox_=nullptr;
    itlmt7902 *driver_=nullptr;
    Mt7902Net80211Hardware *hardware_=nullptr;
    Mt7902Net80211 bridge_;
    IOEthernetInterface *interface_=nullptr;
    IONetworkStats *statistics_=nullptr;
    IOWorkLoop *loop_=nullptr;
    IOCommandGate *gate_=nullptr;
    IONetworkMedium *medium_=nullptr;
    bool baseStarted_=false,attached_=false,used_=false,radioReady_=true,authorized_=false,joining_=false,quiesced_=true;
    uint32_t epoch_=0,lastPoll_=0,joinStart_=0;
    uint32_t rxRejected_[9]={};
    Mt7902NetRecovery recovery_;
    int failure_=0;
    bool fatal_=false;
    uint8_t mac_[6] = {}, ethernet_[1600] = {};
    bool push(const Command &);
    bool pop();
    void purge();
    int gated(int action,void *arg=nullptr);
    int reset(bool recover);
    int scan();
    int join(const char *,const uint8_t *,size_t);
    void process();
    void serviceStack();
    void failConnection(int);
    void managementDiagnostic(const uint8_t *,size_t,int,const char *);
    static int state(void *,uint64_t,const Mt7902Net80211State &);
    static int key(void *,uint64_t,const Mt7902Net80211Key &);
    static int tx(void *,uint64_t,const uint8_t *,size_t,const Mt7902Net80211Tx &);
    static void failed(void *,int);
};
