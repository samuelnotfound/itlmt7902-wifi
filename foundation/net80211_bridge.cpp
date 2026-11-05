// SPDX-License-Identifier: GPL-2.0-or-later
#include "net80211_bridge.hpp"
#include <sys/errno.h>
#include <sys/_mbuf.h>
#include <sys/_ifq.h>
#include <libkern/libkern.h>

extern IOWorkLoop *_fWorkloop;
extern IOCommandGate *_fCommandGate;

OSDefineMetaClassAndAbstractStructors(Mt7902Net80211Controller,IOEthernetController)

bool Mt7902Net80211Controller::bind(Mt7902Net80211 *bridge,IOWorkLoop *workloop)
{
    if (!bridge || !workloop || !workloop->inGate() || bridge_) return false;
    if (stack_workloop_ && stack_workloop_!=workloop) return false;
    if (!stack_workloop_) { stack_workloop_=workloop; stack_workloop_->retain(); }
    __atomic_store_n(&bridge_,bridge,__ATOMIC_RELEASE);
    return true;
}
void Mt7902Net80211Controller::unbind(Mt7902Net80211 *bridge)
{
    if (stack_workloop_ && stack_workloop_->inGate() && bridge_==bridge)
        __atomic_store_n(&bridge_,nullptr,__ATOMIC_RELEASE);
}
bool Mt7902Net80211Controller::setLinkStatus(UInt32 status,const IONetworkMedium *medium,
    UInt64 speed,OSData *data)
{
    if (status&kIONetworkLinkActive) {
        // A retained workloop keeps this check valid until controller teardown.
        // Never dereference a bridge from outside its serialized executor.
        Mt7902Net80211 *bridge=nullptr;
        if (stack_workloop_ && stack_workloop_->inGate())
            bridge=__atomic_load_n(&bridge_,__ATOMIC_ACQUIRE);
        if (!bridge || !bridge->authorized()) status&=~kIONetworkLinkActive;
    }
    return IOEthernetController::setLinkStatus(status,medium,speed,data);
}
void Mt7902Net80211Controller::free()
{
    __atomic_store_n(&bridge_,nullptr,__ATOMIC_RELEASE);
    if (stack_workloop_) { stack_workloop_->release(); stack_workloop_=nullptr; }
    IOEthernetController::free();
}

static_assert(int(IEEE80211_S_INIT)==int(Mt7902NetSession::Init) &&
    int(IEEE80211_S_SCAN)==int(Mt7902NetSession::Scan) &&
    int(IEEE80211_S_AUTH)==int(Mt7902NetSession::Auth) &&
    int(IEEE80211_S_ASSOC)==int(Mt7902NetSession::Assoc) &&
    int(IEEE80211_S_RUN)==int(Mt7902NetSession::Run),"stack/session state mapping");

namespace {
mbuf_t copyPacket(const uint8_t *bytes,size_t length)
{
    mbuf_t packet=nullptr;
    if (mbuf_allocpacket(MBUF_DONTWAIT,length,nullptr,&packet)) return nullptr;
    if (mbuf_copyback(packet,0,length,bytes,MBUF_DONTWAIT) ||
        mbuf_pullup(&packet,length)) {
        if (packet) mbuf_freem(packet);
        return nullptr;
    }
    mbuf_pkthdr_setlen(packet,length);
    return packet;
}
}

bool Mt7902Net80211::inGate() const
{
    return workloop_ && gate_ && workloop_->inGate();
}
Mt7902Net80211 *Mt7902Net80211::from(ieee80211com *ic)
{
    return static_cast<Mt7902Net80211 *>(ic->ic_if.if_softc);
}
uint64_t Mt7902Net80211::token()
{
    // TX tickets have an independent namespace from state/key completions.
    if (!next_token_ || next_token_==UINT64_MAX) return 0;
    return next_token_++;
}

int Mt7902Net80211::initialize(IOEthernetController *controller,
    IOEthernetInterface *interface,IONetworkStats *stats,IOWorkLoop *workloop,
    IOCommandGate *gate,const uint8_t mac[6],const Mt7902Net80211Backend &backend)
{
    if (attached_ || !controller || !interface || !stats || !workloop || !gate ||
        !workloop->inGate() || _fWorkloop!=workloop || _fCommandGate!=gate ||
        gate->getWorkLoop()!=workloop || !mac || (mac[0]&1) ||
        !backend.request_state || !backend.update_key || !backend.submit_tx) return EINVAL;
    auto guarded=OSDynamicCast(Mt7902Net80211Controller,controller);
    if (!guarded || !guarded->bind(this,workloop)) return EOPNOTSUPP;
    network_controller_=guarded;
    uint8_t nonzero=0;
    for (unsigned i=0;i<6;i++) nonzero|=mac[i];
    if (!nonzero) { guarded->unbind(this); network_controller_=nullptr; return EINVAL; }
    workloop_=workloop; gate_=gate; backend_=backend; stopping_=false;
    bzero(&ic_,sizeof(ic_));
    ic_.ic_phytype=IEEE80211_T_OFDM;
    ic_.ic_opmode=IEEE80211_M_STA;
    ic_.ic_state=IEEE80211_S_INIT;
    // No HT/QoS/aggregation, MFP, off-channel roaming or software crypto claims.
    ic_.ic_caps=IEEE80211_C_RSN | IEEE80211_C_SCANALL | IEEE80211_C_SCANALLBAND |
        IEEE80211_C_SHSLOT | IEEE80211_C_SHPREAMBLE;
    ic_.ic_max_rssi=100;
    memcpy(ic_.ic_myaddr,mac,6);
    ic_.ic_sup_rates[IEEE80211_MODE_11A]=ieee80211_std_rateset_11a;
    ic_.ic_sup_rates[IEEE80211_MODE_11B]=ieee80211_std_rateset_11b;
    ic_.ic_sup_rates[IEEE80211_MODE_11G]=ieee80211_std_rateset_11g;
    for (unsigned channel=1;channel<=11;channel++) {
        ic_.ic_channels[channel].ic_freq=(uint16_t)(2407+5*channel);
        ic_.ic_channels[channel].ic_flags=IEEE80211_CHAN_G | IEEE80211_CHAN_B;
    }
    const uint8_t channels5[]={36,40,44,48,149,153,157,161,165};
    for (uint8_t channel:channels5) {
        ic_.ic_channels[channel].ic_freq=(uint16_t)(5000+5*channel);
        ic_.ic_channels[channel].ic_flags=IEEE80211_CHAN_A;
    }
    ic_.ic_ibss_chan=&ic_.ic_channels[1];
    _ifnet *ifp=&ic_.ic_if;
    ifp->if_softc=this;
    ifp->if_flags=IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
    ifp->if_start=start;
    ifp->netStat=stats;
    strlcpy(ifp->if_xname,"mt7902",sizeof(ifp->if_xname));
    if_attach(ifp);
    ieee80211_ifattach(ifp,controller);
    // These are overwritten by ifattach's node/crypto/proto initialization.
    original_state_=ic_.ic_newstate;
    ic_.ic_newstate=newState;
    ic_.ic_set_key=setKey;
    ic_.ic_delete_key=deleteKey;
    ether_ifattach(ifp,interface);
    ieee80211_media_init(ifp);
    attached_=true;
    return 0;
}

int Mt7902Net80211::configureJoin(const char *ssid,const uint8_t *pmk,size_t length)
{
    if (!inGate() || !attached_ || stopping_ || !ssid ||
        (length!=0 && length!=32) || (length && !pmk)) return EINVAL;
    const size_t ssidLength=strnlen(ssid,33);
    if (!ssidLength || ssidLength>32) return EINVAL;
    if (!session_.begin(length==32)) return EBUSY;
    secure_intent_=false;
    // Always clear both old credentials and mode; open cannot inherit RSN/PSK.
    ieee80211_disable_rsn(&ic_);
    ieee80211_disable_wep(&ic_);
    bzero(ic_.ic_psk,sizeof(ic_.ic_psk));
    ic_.ic_flags&=~(IEEE80211_F_AUTO_JOIN | IEEE80211_F_PSK | IEEE80211_F_RSNON);
    bzero(ic_.ic_des_essid,sizeof(ic_.ic_des_essid));
    memcpy(ic_.ic_des_essid,ssid,ssidLength);
    ic_.ic_des_esslen=(uint8_t)ssidLength;
    if (length) {
        memcpy(ic_.ic_psk,pmk,32);
        ic_.ic_flags|=IEEE80211_F_PSK | IEEE80211_F_RSNON;
        ic_.ic_rsnprotos=IEEE80211_PROTO_RSN;
        ic_.ic_rsnakms=IEEE80211_AKM_PSK;
        ic_.ic_rsnciphers=IEEE80211_CIPHER_CCMP;
        ic_.ic_rsngroupcipher=IEEE80211_CIPHER_CCMP;
    }
    join_requested_=true;
    return 0;
}

int Mt7902Net80211::beginScan()
{
    if (!inGate() || !attached_ || stopping_) return EINVAL;
    if (!session_.active() && !session_.begin(false)) return EBUSY;
    ic_.ic_if.if_flags|=IFF_UP | IFF_RUNNING;
    ic_.ic_flags&=~IEEE80211_F_ASCAN; // Backend supplies one passive scan of all advertised channels.
    return queueState(IEEE80211_S_SCAN,-1);
}
int Mt7902Net80211::finishScan()
{
    if (!inGate() || !attached_ || stopping_ || state_pending_ ||
        ic_.ic_state!=IEEE80211_S_SCAN) return EINVAL;
    // A list-only scan must not automatically join an arbitrary open SSID.
    if (join_requested_) ieee80211_end_scan(&ic_.ic_if);
    return 0;
}
int Mt7902Net80211::disconnect()
{
    if (!inGate() || !attached_ || stopping_) return EINVAL;
    join_requested_=false;
    return queueState(IEEE80211_S_INIT,-1);
}
void Mt7902Net80211::pause()
{
    if (!inGate() || !attached_) return;
    stopping_=true; session_.cancel(); secure_intent_=false;
    ic_.ic_if.if_flags&=~IFF_RUNNING;
    if(ic_.ic_bss)ic_.ic_bss->ni_port_valid=0;
    ieee80211_set_link_state(&ic_,LINK_STATE_DOWN);
}

int Mt7902Net80211::queueState(ieee80211_state state,int argument)
{
    if (!inGate() || !attached_ || stopping_ || state>IEEE80211_S_RUN) return EINVAL;
    const uint64_t ticket=session_.requestState((Mt7902NetSession::State)state);
    if (!ticket) { fail(EBUSY); return EBUSY; }
    state_token_=ticket; pending_state_=state; pending_argument_=argument;
    state_pending_=true;
    if (state<IEEE80211_S_ASSOC) secure_intent_=false;
    authorizeIfReady(); // Publish down immediately while hardware state is pending.
    Mt7902Net80211State snapshot={};
    snapshot.state=state; snapshot.argument=argument;
    snapshot.privacy=(ic_.ic_flags&IEEE80211_F_RSNON)!=0;
    snapshot.short_preamble=(ic_.ic_flags&IEEE80211_F_SHPREAMBLE)!=0;
    snapshot.short_slot=(ic_.ic_flags&IEEE80211_F_SHSLOT)!=0;
    memcpy(snapshot.mac,ic_.ic_myaddr,6);
    ieee80211_node *ni=ic_.ic_bss;
    if (ni) {
        memcpy(snapshot.bssid,ni->ni_bssid,6);
        snapshot.ssid_length=ni->ni_esslen;
        if (snapshot.ssid_length>sizeof(snapshot.ssid)) { fail(EINVAL); return EINVAL; }
        memcpy(snapshot.ssid,ni->ni_essid,snapshot.ssid_length);
        snapshot.capability=ni->ni_capinfo;
        snapshot.beacon_interval=ni->ni_intval;
        snapshot.association_id=IEEE80211_AID(ni->ni_associd);
        snapshot.rates=ni->ni_rates;
        if (ni->ni_chan && ni->ni_chan!=IEEE80211_CHAN_ANYC)
            snapshot.channel=(uint8_t)ieee80211_chan2ieee(&ic_,ni->ni_chan);
    }
    const int result=backend_.request_state(backend_.context,ticket,snapshot);
    if (result) { session_.completeState(ticket,false); state_pending_=false; fail(result); }
    return result;
}
int Mt7902Net80211::newState(ieee80211com *ic,ieee80211_state state,int argument)
{
    return from(ic)->queueState(state,argument);
}

int Mt7902Net80211::completeState(uint64_t ticket,int error)
{
    if (!inGate() || !attached_ || stopping_) return EINVAL;
    if (!state_pending_ || ticket!=state_token_ || !session_.completeState(ticket,error==0)) return ESTALE;
    const ieee80211_state state=pending_state_;
    const int argument=pending_argument_;
    state_pending_=false;
    if (error) { fail(error); return error; }
    if (state==IEEE80211_S_SCAN) {
        // The firmware scans all channels. Avoid INIT->SCAN recursively starting
        // another software channel scan; follow upstream firmware-scan drivers.
        ic_.ic_state=IEEE80211_S_SCAN;
        ic_.ic_flags&=~IEEE80211_F_ASCAN;
        return original_state_(&ic_,IEEE80211_S_SCAN,-1);
    }
    if (state==IEEE80211_S_INIT) purgeQueued();
    // INIT success certifies backend peer/key removal. Do not queue duplicate
    // key deletes while net80211 clears its local key records.
    const bool oldStopping=stopping_;
    if (state==IEEE80211_S_INIT) stopping_=true;
    if (state==IEEE80211_S_RUN && !(ic_.ic_flags&IEEE80211_F_RSNON) && ic_.ic_bss) {
        session_.setPortValid(true);
        ic_.ic_bss->ni_port_valid=1;
    }
    const int result=original_state_(&ic_,state,argument);
    stopping_=oldStopping;
    if (result) { fail(result); return result; }
    if (state==IEEE80211_S_INIT) session_.cancel();
    authorizeIfReady();
    drain();
    return 0;
}

int Mt7902Net80211::queueKey(ieee80211_node *ni,ieee80211_key *key,bool remove)
{
    if (!inGate() || !attached_ || stopping_ || !key ||
        key->k_cipher!=IEEE80211_CIPHER_CCMP || key->k_len!=16 ||
        (key->k_flags&IEEE80211_KEY_IGTK)) return EOPNOTSUPP;
    KeySlot *slot=nullptr;
    for (auto &candidate:keys_) if (!candidate.token) { slot=&candidate; break; }
    if (!slot) return ENOBUFS;
    const bool group=(key->k_flags&IEEE80211_KEY_GROUP)!=0;
    const uint64_t ticket=session_.requestKey(!group,key->k_id,remove);
    if (!ticket) return ENOBUFS;
    Mt7902Net80211Key snapshot={};
    memcpy(snapshot.peer,ni?ni->ni_macaddr:ic_.ic_bss->ni_macaddr,6);
    snapshot.id=key->k_id; snapshot.length=16; snapshot.group=group; snapshot.remove=remove;
    // Removal identity is needed to reject deletion of an older GTK after a
    // replacement has occupied the hardware's shared broadcast key slot.
    memcpy(snapshot.bytes,key->k_key,16);
    memcpy(snapshot.receive_sequence,key->k_rsc,sizeof(snapshot.receive_sequence));
    *slot={ticket,group,remove};
    authorizeIfReady(); // Pending rekey also closes the controlled port.
    const int result=backend_.update_key(backend_.context,ticket,snapshot);
    bzero(snapshot.bytes,sizeof(snapshot.bytes));
    if (result) {
        session_.completeKey(ticket,false); *slot={}; fail(result);
        // EBUSY is reserved for an accepted asynchronous install in net80211.
        return result==EBUSY?EAGAIN:result;
    }
    return 0;
}
int Mt7902Net80211::setKey(ieee80211com *ic,ieee80211_node *ni,ieee80211_key *key)
{
    const int result=from(ic)->queueKey(ni,key,false);
    // EBUSY is net80211's documented asynchronous-install result: defer link.
    return result?result:EBUSY;
}
void Mt7902Net80211::deleteKey(ieee80211com *ic,ieee80211_node *ni,ieee80211_key *key)
{
    Mt7902Net80211 *bridge=from(ic);
    if (!bridge->stopping_ && key->k_cipher!=IEEE80211_CIPHER_NONE) {
        const int result=bridge->queueKey(ni,key,true);
        if (result) bridge->fail(result);
    }
    bzero(key->k_key,sizeof(key->k_key));
}
int Mt7902Net80211::completeKey(uint64_t ticket,int error)
{
    if (!inGate() || !attached_ || stopping_) return EINVAL;
    KeySlot *slot=nullptr;
    for (auto &candidate:keys_) if (candidate.token==ticket) { slot=&candidate; break; }
    if (!slot || !ticket || !session_.completeKey(ticket,error==0)) return ESTALE;
    *slot={};
    if (error) { fail(error); return error; }
    authorizeIfReady();
    drain();
    return 0;
}

void Mt7902Net80211::authorizeIfReady()
{
    ieee80211_node *ni=ic_.ic_bss;
    const bool rsn=(ic_.ic_flags&IEEE80211_F_RSNON)!=0;
    // A synchronous upstream SECURE transition may already have authorized the
    // port (e.g. retransmitted M3 with no new keys). INSTALL/TXRXPROT alone is
    // insufficient. Async SECURE intent is recorded by the narrowly staged hook.
    if (rsn && ni && ni->ni_port_valid && !state_pending_ && ic_.ic_state==IEEE80211_S_RUN)
        secure_intent_=true;
    const bool handshake=ni && (!rsn || secure_intent_);
    session_.setPortValid(handshake);
    const bool ready=session_.linkReady();
    if (ni) ni->ni_port_valid=ready?1:0;
    ieee80211_set_link_state(&ic_,ready?LINK_STATE_UP:LINK_STATE_DOWN);
}
bool Mt7902Net80211::authorized() const
{
    return inGate() && attached_ && !stopping_ && session_.linkReady() &&
        ic_.ic_state==IEEE80211_S_RUN && ic_.ic_bss && ic_.ic_bss->ni_port_valid &&
        ic_.ic_if.if_link_state==LINK_STATE_UP;
}
void Mt7902Net80211::fail(int error)
{
    if (!attached_) return;
    secure_intent_=false;
    session_.cancel(); // No later completion may reopen a failed/canceled port.
    if (ic_.ic_bss) ic_.ic_bss->ni_port_valid=0;
    ieee80211_set_link_state(&ic_,LINK_STATE_DOWN);
    ic_.ic_if.if_flags&=~IFF_RUNNING;
    if (backend_.failed) backend_.failed(backend_.context,error);
}

int Mt7902Net80211::receive(const uint8_t *bytes,size_t length,uint8_t channel,
    int rssi,bool rssiValid,uint32_t flags,uint32_t stamp)
{
    if (!inGate() || !attached_ || stopping_ || !session_.active() || !bytes || length<24 || length>4096 ||
        (flags&~uint32_t(IEEE80211_RXI_HWDEC)) || !channel ||
        !ic_.ic_channels[channel].ic_flags) return EINVAL;
    // During firmware scan, beacons lacking a DS/HT primary-channel IE use the
    // actual descriptor channel, never the last arbitrary software scan slot.
    if (ic_.ic_state==IEEE80211_S_SCAN && ic_.ic_bss)
        ic_.ic_bss->ni_chan=&ic_.ic_channels[channel];
    mbuf_t packet=copyPacket(bytes,length);
    if (!packet) return ENOBUFS;
    ieee80211_rxinfo info={};
    info.rxi_flags=flags;
    info.rxi_rssi=!rssiValid || rssi<-110 || rssi>0?0:rssi>=-10?100:rssi+110;
    info.rxi_tstamp=stamp;
    ieee80211_node *node=ieee80211_find_rxnode(&ic_,mtod(packet,ieee80211_frame *));
    if (!node) { mbuf_freem(packet); return ENOENT; }
    ieee80211_input(&ic_.ic_if,packet,node,&info); // Consumes packet, not our node reference.
    ieee80211_release_node(&ic_,node);
    authorizeIfReady();
    drain();
    return 0;
}

int Mt7902Net80211::enqueueEthernet(const uint8_t *bytes,size_t length)
{
    if (!inGate() || !attached_ || stopping_ || !bytes || length<14 || length>1600) return EINVAL;
    // EAPOL is generated internally; the external stack cannot bypass this gate.
    if (!authorized()) return ENETDOWN;
    mbuf_t packet=copyPacket(bytes,length);
    if (!packet) return ENOBUFS;
    ifq_enqueue(&ic_.ic_if.if_snd,packet);
    drain();
    return 0;
}
void Mt7902Net80211::start(_ifnet *ifp)
{
    static_cast<Mt7902Net80211 *>(ifp->if_softc)->drain();
}
void Mt7902Net80211::drain()
{
    if (!inGate() || !attached_ || stopping_ || !session_.active() || draining_ ||
        !(ic_.ic_if.if_flags&IFF_RUNNING)) return;
    draining_=true;
    for (unsigned budget=0;budget<64;budget++) {
        TxSlot *slot=nullptr;
        for (auto &candidate:tx_) if (!candidate.token) { slot=&candidate; break; }
        if (!slot) break;
        if (!held_packet_) {
            held_packet_=mq_dequeue(&ic_.ic_mgtq);
            held_management_=held_packet_!=nullptr;
            if (held_packet_) held_node_=(ieee80211_node *)mbuf_pkthdr_rcvif(held_packet_);
            else {
                if (ic_.ic_state!=IEEE80211_S_RUN || (ic_.ic_xflags&IEEE80211_F_TX_MGMT_ONLY)) break;
                mbuf_t ethernet=ifq_dequeue(&ic_.ic_if.if_snd);
                if (!ethernet) break;
                held_packet_=ieee80211_encap(&ic_.ic_if,ethernet,&held_node_);
                if (!held_packet_) { held_node_=nullptr; continue; }
            }
        }
        const size_t length=mbuf_pkthdr_len(held_packet_);
        if (!held_node_ || length<24 || length>sizeof(tx_copy_) ||
            mbuf_copydata(held_packet_,0,length,tx_copy_)) {
            mbuf_freem(held_packet_);
            if (held_node_) ieee80211_release_node(&ic_,held_node_);
            held_packet_=nullptr; held_node_=nullptr;
            continue;
        }
        Mt7902Net80211Tx snapshot={};
        memcpy(snapshot.peer,held_node_->ni_macaddr,6);
        if (held_node_->ni_chan && held_node_->ni_chan!=IEEE80211_CHAN_ANYC)
            snapshot.channel=(uint8_t)ieee80211_chan2ieee(&ic_,held_node_->ni_chan);
        snapshot.sequence=(uint16_t)((tx_copy_[22]|(uint16_t(tx_copy_[23])<<8))>>4);
        snapshot.management=held_management_;
        snapshot.protected_frame=(tx_copy_[1]&IEEE80211_FC1_PROTECTED)!=0;
        const uint64_t value=token();
        if (!value) { fail(ENOSPC); break; }
        const int result=backend_.submit_tx(backend_.context,value,tx_copy_,length,snapshot);
        if (result==EAGAIN || result==ENOBUFS || result==EBUSY) break;
        if (result) {
            mbuf_freem(held_packet_); ieee80211_release_node(&ic_,held_node_);
            held_packet_=nullptr; held_node_=nullptr;
            ic_.ic_if.netStat->outputErrors++;
            continue;
        }
        *slot={value,held_packet_,held_node_};
        held_packet_=nullptr; held_node_=nullptr;
        ic_.ic_if.netStat->outputPackets++;
    }
    draining_=false;
}
void Mt7902Net80211::releaseTx(TxSlot &slot)
{
    if (slot.packet) mbuf_freem(slot.packet);
    if (slot.node) ieee80211_release_node(&ic_,slot.node);
    slot={};
}
int Mt7902Net80211::completeTx(uint64_t value,int error)
{
    if (!inGate() || !attached_ || stopping_ || !value) return EINVAL;
    for (auto &slot:tx_) if (slot.token==value) {
        releaseTx(slot);
        if (error) ic_.ic_if.netStat->outputErrors++;
        drain(); return 0;
    }
    return ESTALE;
}
void Mt7902Net80211::purgeQueued()
{
    if (held_packet_) mbuf_freem(held_packet_);
    if (held_node_) ieee80211_release_node(&ic_,held_node_);
    held_packet_=nullptr; held_node_=nullptr;
    while (mbuf_t packet=mq_dequeue(&ic_.ic_mgtq)) {
        auto node=(ieee80211_node *)mbuf_pkthdr_rcvif(packet);
        mbuf_freem(packet);
        if (node) ieee80211_release_node(&ic_,node);
    }
    ifq_flush(&ic_.ic_if.if_snd);
}
int Mt7902Net80211::acknowledgeQuiesced(bool quiesced)
{
    if (!inGate() || !attached_ || !quiesced) return EINVAL;
    session_.cancel();
    secure_intent_=false;
    purgeQueued();
    for (auto &slot:tx_) releaseTx(slot);
    for (auto &slot:keys_) slot={};
    state_pending_=false;
    const bool oldStopping=stopping_;
    stopping_=true;
    original_state_(&ic_,IEEE80211_S_INIT,-1);
    stopping_=oldStopping;
    ic_.ic_if.if_flags&=~(IFF_RUNNING | IFF_UP);
    // A list refresh after disconnect must not rejoin the previous SSID with
    // a scrubbed PMK or inherit its protected-network mode in a new session.
    join_requested_=false;
    ieee80211_disable_rsn(&ic_);
    ieee80211_disable_wep(&ic_);
    ic_.ic_flags&=~(IEEE80211_F_AUTO_JOIN | IEEE80211_F_PSK | IEEE80211_F_RSNON);
    bzero(ic_.ic_des_essid,sizeof(ic_.ic_des_essid));
    ic_.ic_des_esslen=0;
    bzero(ic_.ic_psk,sizeof(ic_.ic_psk));
    stopping_=false;
    return session_.quiesced()?0:EBUSY;
}
int Mt7902Net80211::shutdown(bool quiesced)
{
    if (!inGate() || !attached_ || !quiesced) return EINVAL;
    stopping_=true;
    acknowledgeQuiesced(true);
    stopping_=true;
    ieee80211_ifdetach(&ic_.ic_if);
    if_detach(&ic_.ic_if);
    if (network_controller_) network_controller_->unbind(this);
    network_controller_=nullptr;
    attached_=false; workloop_=nullptr; gate_=nullptr; backend_={};
    return 0;
}
int Mt7902Net80211::poll()
{
    if (!inGate() || !attached_ || stopping_ || !session_.active()) return EINVAL;
    ieee80211_watchdog(&ic_.ic_if);
    authorizeIfReady(); drain(); return 0;
}

// Called only by the staged PAE unit after MIC/replay/RSN checks and SECURE.
// This records protocol intent, not successful hardware installation.
void mt7902_net80211_deferred_port_valid(ieee80211com *ic,ieee80211_node *node)
{
    if (!ic) return;
    auto bridge=Mt7902Net80211::from(ic);
    if (bridge && bridge->inGate() && bridge->attached_ && !bridge->stopping_ &&
        bridge->session_.active() && node==bridge->ic_.ic_bss &&
        bridge->ic_.ic_state==IEEE80211_S_RUN)
        bridge->secure_intent_=true;
}
