// SPDX-License-Identifier: GPL-2.0-or-later
#include "net80211_runtime.hpp"
#include "../src/itlmt7902.hpp"
#include "../src/itl_wpa.h"
#include <IOKit/network/IONetworkMedium.h>
#include <sys/_task.h>
#include <sys/errno.h>
#include <libkern/libkern.h>

IOWorkLoop *_fWorkloop=nullptr;
IOCommandGate *_fCommandGate=nullptr;
static void *runtimeOwner=nullptr;
OSDefineMetaClassAndStructors(Mt7902Net80211Runtime,Mt7902Net80211Controller)
namespace {
enum { Attach, Pause, Reset, Shutdown, Join, Scan, FinishScan, Receive, Complete, Poll, Authorized, Generation };
struct JoinArgs { const char *ssid; const uint8_t *pmk; size_t length; };
}
bool Mt7902Net80211Runtime::setup(itlmt7902 *driver)
{
    driver_=driver; memcpy(mac_,driver->mac,6);
    mailbox_=IOLockAlloc();
    hardware_=new Mt7902Net80211Hardware(*driver);
    return mailbox_ && hardware_;
}
bool Mt7902Net80211Runtime::createWorkLoop()
{
    if (!loop_) loop_=IOWorkLoop::workLoop();
    return loop_!=nullptr;
}
IOOutputQueue *Mt7902Net80211Runtime::createOutputQueue()
{
    return createWorkLoop()?IOGatedOutputQueue::withTarget(this,loop_,256):nullptr;
}
bool Mt7902Net80211Runtime::configureInterface(IONetworkInterface *interface)
{
    if (!IOEthernetController::configureInterface(interface)) return false;
    auto data=interface->getParameter(kIONetworkStatsKey);
    statistics_=data?static_cast<IONetworkStats *>(data->getBuffer()):nullptr;
    return statistics_!=nullptr;
}
bool Mt7902Net80211Runtime::start(IOService *provider)
{
    void *expected=nullptr;
    if (!__atomic_compare_exchange_n(&runtimeOwner,&expected,this,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) return false;
    if (!IOEthernetController::start(provider)) return false;
    baseStarted_=true;
    if(!createWorkLoop())return false;
    gate_=IOCommandGate::commandGate(this);
    if (!gate_ || loop_->addEventSource(gate_)!=kIOReturnSuccess) return false;
    _fWorkloop=loop_; _fCommandGate=gate_;
    medium_=IONetworkMedium::medium(kIOMediumEthernetAuto,54000000);
    auto media=OSDictionary::withCapacity(1);
    if (!medium_ || !media) { if(media)media->release(); return false; }
    IONetworkMedium::addMedium(media,medium_);
    const bool ok=publishMediumDictionary(media) && setCurrentMedium(medium_) && setSelectedMedium(medium_);
    media->release();
    if (!ok || !attachInterface(reinterpret_cast<IONetworkInterface **>(&interface_),true) || !statistics_) return false;
    if (gated(Attach)) return false;
    attached_=true;
    if (getOutputQueue()) getOutputQueue()->start();
    registerService();
    return true;
}
IOReturn Mt7902Net80211Runtime::getHardwareAddress(IOEthernetAddress *address)
{ memcpy(address->bytes,mac_,6); return kIOReturnSuccess; }
IOReturn Mt7902Net80211Runtime::enable(IONetworkInterface *)
{ if(getOutputQueue())getOutputQueue()->start(); return kIOReturnSuccess; }
IOReturn Mt7902Net80211Runtime::disable(IONetworkInterface *)
{ if(getOutputQueue()){getOutputQueue()->stop();getOutputQueue()->flush();} return kIOReturnSuccess; }
UInt32 Mt7902Net80211Runtime::outputPacket(mbuf_t packet,void *)
{
    if (!packet) return kIOReturnOutputDropped;
    const size_t length=mbuf_pkthdr_len(packet);
    int error=ENETDOWN;
    if (attached_ && length>=14 && length<=sizeof(ethernet_) &&
        !mbuf_copydata(packet,0,length,ethernet_)) error=bridge_.enqueueEthernet(ethernet_,length);
    freePacket(packet);
    return error?kIOReturnOutputDropped:kIOReturnOutputSuccess;
}
bool Mt7902Net80211Runtime::push(const Command &command)
{
    IOLockLock(mailbox_);
    // Reserve control slots so bulk TX cannot starve association/key operations.
    const bool room=commandHead_-commandTail_<(command.kind==Tx?28u:32u);
    if(room) commands_[commandHead_++%32]=command;
    IOLockUnlock(mailbox_); return room;
}
bool Mt7902Net80211Runtime::pop()
{
    IOLockLock(mailbox_);
    const bool available=commandHead_!=commandTail_;
    if (available) {
        current_=commands_[commandTail_%32];
        bzero(&commands_[commandTail_++%32],sizeof(Command));
    }
    IOLockUnlock(mailbox_); return available;
}
void Mt7902Net80211Runtime::purge()
{
    IOLockLock(mailbox_);
    bzero(commands_,sizeof(commands_)); bzero(receives_,sizeof(receives_));
    commandTail_=commandHead_; rxTail_=rxHead_;
    IOLockUnlock(mailbox_);
    bzero(&current_,sizeof(current_));
}
int Mt7902Net80211Runtime::state(void *context,uint64_t token,const Mt7902Net80211State &snapshot)
{
    auto self=static_cast<Mt7902Net80211Runtime *>(context);
    Command command={}; command.kind=State; command.token=token;
    command.epoch=uint32_t(token>>32); command.state=snapshot;
    return self->push(command)?0:ENOBUFS;
}
int Mt7902Net80211Runtime::key(void *context,uint64_t token,const Mt7902Net80211Key &snapshot)
{
    auto self=static_cast<Mt7902Net80211Runtime *>(context);
    Command command={}; command.kind=Key; command.token=token;
    command.epoch=uint32_t(token>>32); command.key=snapshot;
    const int result=self->push(command)?0:ENOBUFS;
    bzero(&command,sizeof(command)); return result;
}
int Mt7902Net80211Runtime::tx(void *context,uint64_t token,const uint8_t *frame,size_t length,
                             const Mt7902Net80211Tx &snapshot)
{
    auto self=static_cast<Mt7902Net80211Runtime *>(context);
    if (!frame || length>1900) return EMSGSIZE;
    Command command={}; command.kind=Tx; command.token=token;
    command.epoch=self->bridge_.generation(); command.tx=snapshot;
    command.length=(uint16_t)length; memcpy(command.frame,frame,length);
    return self->push(command)?0:EAGAIN;
}
void Mt7902Net80211Runtime::failed(void *context,int error)
{
    auto self=static_cast<Mt7902Net80211Runtime *>(context);
    __atomic_store_n(&self->failure_,error?error:EIO,__ATOMIC_RELEASE);
}
int Mt7902Net80211Runtime::gated(int action,void *argument)
{
    if (!gate_) return ENXIO;
    return (int)gate_->runAction([](OSObject *owner,void *actionArg,void *argument,void *,void *)->IOReturn {
        auto self=static_cast<Mt7902Net80211Runtime *>(owner);
        const int action=(int)(uintptr_t)actionArg;
        switch(action) {
        case Attach: {
            taskq_init();
            Mt7902Net80211Backend backend={self,state,key,tx,failed};
            return self->bridge_.initialize(self,self->interface_,self->statistics_,self->loop_,self->gate_,self->mac_,backend);
        }
        case Pause: self->bridge_.pause(); return 0;
        case Reset: return self->bridge_.acknowledgeQuiesced(true);
        case Shutdown: return self->bridge_.shutdown(true);
        case Join: {
            auto args=static_cast<JoinArgs *>(argument);
            return self->bridge_.configureJoin(args->ssid,args->pmk,args->length);
        }
        case Scan: return self->bridge_.beginScan();
        case FinishScan: return self->bridge_.finishScan();
        case Receive: {
            auto &rx=self->receive_;
            return self->bridge_.receive(rx.frame,rx.metadata.length,rx.metadata.channel,
                rx.metadata.rssi_dbm,rx.metadata.rssi_valid,
                rx.metadata.hardware_decrypted?IEEE80211_RXI_HWDEC:0,rx.metadata.timestamp);
        }
        case Complete: {
            const int result=*static_cast<int *>(argument);
            auto &command=self->current_;
            if(command.kind==State) return self->bridge_.completeState(command.token,result);
            if(command.kind==Key) return self->bridge_.completeKey(command.token,result);
            return self->bridge_.completeTx(command.token,result);
        }
        case Poll: return self->bridge_.poll();
        case Authorized: return self->bridge_.authorized()?1:0;
        case Generation: return self->bridge_.generation();
        }
        return EINVAL;
    },reinterpret_cast<void *>((uintptr_t)action),argument);
}
void Mt7902Net80211Runtime::receiveDMA(const uint8_t *packet,uint32_t length)
{
    if (!attached_ || !used_ || !radioReady_ || !epoch_) return;
    Mt7902Net80211Rx rx={};
    if(mt7902_net80211_normalize_rx(packet,length,MT7902_NET80211_HW_CCMP,
        normalized_,sizeof(normalized_),&rx)!=MT7902_NET80211_RX_OK) return;
    IOLockLock(mailbox_);
    if(rxHead_-rxTail_<64) {
        auto &slot=receives_[rxHead_++%64]; slot.metadata=rx; slot.epoch=epoch_;
        memcpy(slot.frame,normalized_,rx.length);
    }
    IOLockUnlock(mailbox_);
}
void Mt7902Net80211Runtime::serviceStack()
{
    for(unsigned budget=0;budget<64;budget++) {
        IOLockLock(mailbox_);
        const bool available=rxHead_!=rxTail_;
        if(available) receive_=receives_[rxTail_++%64];
        IOLockUnlock(mailbox_);
        if(!available) break;
        if(receive_.epoch==epoch_) gated(Receive);
    }
    const uint32_t now=driver_->nowMs();
    if(used_ && now-lastPoll_>=1000) { lastPoll_=now; gated(Poll); }
}
int Mt7902Net80211Runtime::reset(bool recover)
{
    if(attached_) gated(Pause);
    authorized_=false; joining_=false;
    if(used_ || (recover && driver_->firmwareFault)) {
        // Do not release a stack reference while the old DMA session can run.
        if(!driver_->quiesceHardware()) return EBUSY;
        radioReady_=false;
        if(hardware_->bound() && hardware_->releaseAfterQuiesce()) return EBUSY;
        if(used_ && gated(Reset)) return EBUSY;
        quiesced_=true;
    }
    purge(); epoch_=0; used_=false;
    __atomic_store_n(&failure_,0,__ATOMIC_RELEASE);
    if(recover && !radioReady_) {
        if(!driver_->chipRecover()) return EIO;
        driver_->firmwareFault=false; driver_->recoverWanted=false; radioReady_=true;
    }
    return 0;
}
int Mt7902Net80211Runtime::scan()
{
    if((!radioReady_ || driver_->firmwareFault) && reset(true)) return EIO;
    const int result=gated(Scan);
    if(!result){used_=true;quiesced_=false;epoch_=(uint32_t)gated(Generation);}
    return result;
}
int Mt7902Net80211Runtime::join(const char *ssid,const uint8_t *pmk,size_t length)
{
    if((used_ || !radioReady_ || driver_->firmwareFault) && reset(true)) return EIO;
    JoinArgs arguments={ssid,pmk,length};
    int result=gated(Join,&arguments);
    if(!result) {
        used_=true; quiesced_=false; epoch_=(uint32_t)gated(Generation);
        result=gated(Scan); joining_=!result; joinStart_=driver_->nowMs();
    }
    return result;
}
void Mt7902Net80211Runtime::process()
{
    for(unsigned budget=0;budget<32 && pop();budget++) {
        if(current_.epoch!=epoch_) { bzero(&current_,sizeof(current_));continue; }
        int error=0;
        if(!hardware_->bound()) error=hardware_->bindSession(epoch_);
        if(!error && current_.kind==State) error=hardware_->changeState(epoch_,current_.state);
        if(!error && current_.kind==Key) error=hardware_->updateKey(epoch_,current_.key);
        if(!error && current_.kind==Tx) {
            const uint32_t started=driver_->nowMs();
            do {
                error=hardware_->transmit(epoch_,current_.frame,current_.length,current_.tx);
                if(error!=EAGAIN) break;
                driver_->rxDataPoll(); IOSleep(1);
            }while(!driver_->shouldStop() && !driver_->cmdPending(itlmt7902::CMD_JOIN|itlmt7902::CMD_DISCONNECT) &&
                driver_->nowMs()-started<500);
        }
        const bool scanning=current_.kind==State && current_.state.state==IEEE80211_S_SCAN && !error;
        gated(Complete,&error);
        bzero(&current_,sizeof(current_));
        if(error) { failed(this,error); break; }
        if(scanning) {
            // SCAN has been committed before firmware starts delivering beacons.
            error=driver_->m2Scan(nullptr);
            serviceStack();
            if(!error) gated(FinishScan); else failed(this,error);
            if(joining_) driver_->wifiState("connecting",""); else driver_->wifiState("idle","");
        }
    }
}
void Mt7902Net80211Runtime::failConnection(int error)
{
    driver_->setProperty("net80211-error",(uint32_t)error,32);
    const bool wasJoining=joining_;
    reset(false);
    driver_->wifiState("failed",wasJoining?"net80211 connection failed; collect driver diagnostics":"net80211 session failed; collect driver diagnostics");
}
void Mt7902Net80211Runtime::serve()
{
    driver_->setProperty("connection-engine","net80211");
    driver_->wifiState("idle","");
    while(!driver_->shouldStop()) {
        itlmt7902::Net request={};
        const uint32_t command=driver_->cmdTake(itlmt7902::CMD_SCAN|itlmt7902::CMD_JOIN|itlmt7902::CMD_DISCONNECT,&request);
        if(command&itlmt7902::CMD_DISCONNECT) {
            const int result=reset(false);
            driver_->wifiState(result?"failed":"idle",result?"Radio quiescence failed":"");
        }
        if(command&itlmt7902::CMD_JOIN) {
            uint8_t pmk[32]={}; const size_t length=strlen(request.psk);
            if(request.hasPmk) memcpy(pmk,request.pmk,32);
            else if(length) itl_wpa_pmk(request.psk,(const uint8_t *)request.ssid,strlen(request.ssid),pmk);
            driver_->setProperty("wifi-ssid",request.ssid);
            driver_->wifiState("connecting","");
            const int error=join(request.ssid,pmk,(length||request.hasPmk)?32:0);
            bzero(pmk,sizeof(pmk)); if(error)failConnection(error);
        } else if((command&itlmt7902::CMD_SCAN) && !authorized_) {
            const int error=scan(); if(error)failConnection(error);
        }
        bzero(&request,sizeof(request));
        if(radioReady_) {process();driver_->rxDataPoll();serviceStack();}
        int error=__atomic_exchange_n(&failure_,0,__ATOMIC_ACQ_REL);
        if(driver_->firmwareFault)error=EIO;
        if(joining_ && driver_->nowMs()-joinStart_>45000)error=ETIMEDOUT;
        if(error && used_) {failConnection(error);continue;}
        const bool authorized=used_ && gated(Authorized)==1;
        if(authorized!=authorized_) {
            const int result=hardware_->setAuthorized(epoch_,authorized);
            if(result) {failConnection(result);continue;}
            authorized_=authorized;
            if(authorized) {joining_=false;driver_->wifiState("connected","");driver_->wifiLink();}
            else if(used_){joining_=true;joinStart_=driver_->nowMs();driver_->wifiState("connecting","");}
        }
        IOSleep(1);
    }
    reset(false);
}
void Mt7902Net80211Runtime::stop(IOService *provider)
{
    if(!quiesced_)return; // Retain every reference if the physical DMA barrier failed.
    if(attached_) {gated(Shutdown);attached_=false;}
    if(interface_){detachInterface(interface_,true);interface_=nullptr;}
    if(gate_ && loop_)loop_->removeEventSource(gate_);
    if(runtimeOwner==this){_fCommandGate=nullptr;_fWorkloop=nullptr;__atomic_store_n(&runtimeOwner,nullptr,__ATOMIC_RELEASE);}
    if(baseStarted_){IOEthernetController::stop(provider);baseStarted_=false;}
}
void Mt7902Net80211Runtime::free()
{
    if(hardware_){delete hardware_;hardware_=nullptr;}
    if(mailbox_){IOLockFree(mailbox_);mailbox_=nullptr;}
    if(gate_){gate_->release();gate_=nullptr;}
    if(medium_){medium_->release();medium_=nullptr;}
    if(loop_){loop_->release();loop_=nullptr;}
    if(runtimeOwner==this){_fCommandGate=nullptr;_fWorkloop=nullptr;__atomic_store_n(&runtimeOwner,nullptr,__ATOMIC_RELEASE);}
    Mt7902Net80211Controller::free();
}
void itlmt7902::net80211Serve()
{
    auto runtime=OSTypeAlloc(Mt7902Net80211Runtime);
    if(!runtime || !runtime->init() || !runtime->setup(this)) {
        if(runtime)runtime->release();wifiState("failed","net80211 allocation failed");return;
    }
    if(!runtime->attach(this)){runtime->release();wifiState("failed","net80211 attach failed");return;}
    netRuntime=runtime;
    if(runtime->start(this))runtime->serve();
    else wifiState("failed","net80211 Ethernet initialization failed");
    if(!runtime->quiesced_) {
        wifiState("failed","DMA did not quiesce; retaining net80211 resources");
        return;
    }
    runtime->stop(this);netRuntime=nullptr;runtime->detach(this);runtime->release();
}
void itlmt7902::net80211Receive(const uint8_t *packet,uint32_t length)
{if(netRuntime)netRuntime->receiveDMA(packet,length);}
void itlmt7902::net80211ServiceRx()
{if(netRuntime)netRuntime->serviceStack();}
