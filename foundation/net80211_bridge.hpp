// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/network/IOEthernetController.h>
#include <IOKit/network/IOEthernetInterface.h>
#include <net80211/ieee80211_var.h>
#include "mt7902_net80211_session.h"

class Mt7902Net80211;
// All upstream link notifications pass this mandatory controlled-port guard.
class Mt7902Net80211Controller : public IOEthernetController {
    OSDeclareAbstractStructors(Mt7902Net80211Controller)
public:
    bool setLinkStatus(UInt32,const IONetworkMedium * = nullptr,
                       UInt64 = 0,OSData * = nullptr) override;
    void free() override;
private:
    friend class Mt7902Net80211;
    bool bind(Mt7902Net80211 *,IOWorkLoop *);
    void unbind(Mt7902Net80211 *);
    Mt7902Net80211 *bridge_ = nullptr;
    IOWorkLoop *stack_workloop_ = nullptr;
};

// No pointers into net80211 escape through the backend command interface.
// Callbacks run under the stack gate and MUST copy/enqueue without waiting for
// firmware or calling completion inline. Completions re-enter the same gate.
struct Mt7902Net80211State {
    ieee80211_state state;
    int argument;
    uint8_t mac[6], bssid[6], ssid[32], ssid_length, channel;
    uint16_t capability, beacon_interval, association_id;
    ieee80211_rateset rates;
    bool privacy, short_preamble, short_slot;
};
struct Mt7902Net80211Key {
    uint8_t peer[6], id, length, bytes[16];
    bool group, remove;
    uint64_t receive_sequence[IEEE80211_NUM_TID];
};
struct Mt7902Net80211Tx {
    uint8_t peer[6], channel;
    uint16_t sequence;
    bool management, protected_frame;
};
struct Mt7902Net80211Backend {
    void *context;
    int (*request_state)(void *, uint64_t token, const Mt7902Net80211State &);
    int (*update_key)(void *, uint64_t token, const Mt7902Net80211Key &);
    int (*submit_tx)(void *, uint64_t token, const uint8_t *, size_t,
                     const Mt7902Net80211Tx &);
    // Failure notification is diagnostic; the bridge independently keeps link down.
    void (*failed)(void *, int error);
};

class Mt7902Net80211 {
public:
    // All methods, including initialize/shutdown, require workloop->inGate().
    // Caller owns and must keep controller/interface/stats/workloop/gate alive.
    int initialize(IOEthernetController *, IOEthernetInterface *, IONetworkStats *,
                   IOWorkLoop *, IOCommandGate *, const uint8_t mac[6],
                   const Mt7902Net80211Backend &);
    // Caller has stopped/quiesced hardware and can prove no DMA references remain.
    int shutdown(bool hardware_quiesced);
    // Backend has removed the peer/keys and quiesced all old DMA/callback work.
    int acknowledgeQuiesced(bool hardware_quiesced);
    int configureJoin(const char *ssid, const uint8_t *pmk, size_t pmk_length);
    int beginScan();
    int finishScan();
    int disconnect();
    void pause(); // Gate-only: stop callbacks before the worker quiesces DMA.
    uint32_t generation() const { return session_.generation(); }
    // Raw frame has no FCS/RXD. HWDEC requires the actual reconstructed CCMP IV.
    // Caller retains input storage; this method copies it into a stack-owned mbuf.
    int receive(const uint8_t *,size_t,uint8_t channel,int rssi_dbm,bool rssi_valid,
                 uint32_t rx_flags,uint32_t stamp);
    // Copies Ethernet input; on success the queue owns that copy.
    int enqueueEthernet(const uint8_t *, size_t);
    int completeState(uint64_t token, int error);
    int completeKey(uint64_t token, int error);
    int completeTx(uint64_t token, int error);
    int poll(); // Caller schedules at 1 Hz under the stack gate.
    ieee80211com *controller() { return attached_ ? &ic_ : nullptr; }
    bool authorized() const;

private:
    friend void mt7902_net80211_deferred_port_valid(ieee80211com *,ieee80211_node *);
    ieee80211com ic_ = {};
    IOWorkLoop *workloop_ = nullptr;
    IOCommandGate *gate_ = nullptr;
    Mt7902Net80211Controller *network_controller_ = nullptr;
    Mt7902Net80211Backend backend_ = {};
    int (*original_state_)(ieee80211com *, ieee80211_state, int) = nullptr;
    bool attached_ = false, stopping_ = false, draining_ = false;
    bool state_pending_ = false;
    bool join_requested_ = false;
    bool secure_intent_ = false;
    uint64_t next_token_ = 1, state_token_ = 0;
    ieee80211_state pending_state_ = IEEE80211_S_INIT;
    int pending_argument_ = -1;
    struct TxSlot { uint64_t token; mbuf_t packet; ieee80211_node *node; } tx_[64] = {};
    struct KeySlot { uint64_t token; bool group, remove; } keys_[8] = {};
    Mt7902NetSession session_;
    mbuf_t held_packet_ = nullptr;
    ieee80211_node *held_node_ = nullptr;
    bool held_management_ = false;
    uint8_t tx_copy_[4096] = {};
    bool inGate() const;
    int queueState(ieee80211_state, int);
    int queueKey(ieee80211_node *, ieee80211_key *, bool remove);
    void drain();
    void purgeQueued();
    void releaseTx(TxSlot &);
    void authorizeIfReady();
    void fail(int);
    uint64_t token();
    static Mt7902Net80211 *from(ieee80211com *);
    static int newState(ieee80211com *, ieee80211_state, int);
    static int setKey(ieee80211com *, ieee80211_node *, ieee80211_key *);
    static void deleteKey(ieee80211com *, ieee80211_node *, ieee80211_key *);
    static void start(_ifnet *);
};
