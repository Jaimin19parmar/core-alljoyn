/**
 * @file
 * BusAttachment is the top-level object responsible for connecting to and optionally managing a message bus.
 */

/******************************************************************************
 *    Copyright (c) Open Connectivity Foundation (OCF), AllJoyn Open Source
 *    Project (AJOSP) Contributors and others.
 *
 *    SPDX-License-Identifier: Apache-2.0
 *
 *    All rights reserved. This program and the accompanying materials are
 *    made available under the terms of the Apache License, Version 2.0
 *    which accompanies this distribution, and is available at
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Copyright (c) Open Connectivity Foundation and Contributors to AllSeen
 *    Alliance. All rights reserved.
 *
 *    Permission to use, copy, modify, and/or distribute this software for
 *    any purpose with or without fee is hereby granted, provided that the
 *    above copyright notice and this permission notice appear in all
 *    copies.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 *    WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 *    WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 *    AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 *    DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 *    PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 *    TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 *    PERFORMANCE OF THIS SOFTWARE.
 ******************************************************************************/
#include <qcc/platform.h>
#include <qcc/Debug.h>
#include <qcc/Util.h>
#include <qcc/Event.h>
#include <qcc/String.h>
#include <qcc/Timer.h>
#include <qcc/atomic.h>
#include <qcc/XmlElement.h>
#include <qcc/StringSource.h>
#include <qcc/FileStream.h>
#include <qcc/String.h>
#include <qcc/StringUtil.h>
#include <qcc/MutexInternal.h>

#if defined(QCC_OS_GROUP_WINDOWS)
#include <qcc/windows/NamedPipeWrapper.h>
#endif

#include <algorithm>
#include <set>

#include <alljoyn/BusAttachment.h>
#include <alljoyn/BusListener.h>
#include <alljoyn/DBusStd.h>
#include <alljoyn/AllJoynStd.h>
#include <alljoyn/InterfaceDescription.h>
#include <alljoyn/AutoPinger.h>

#include "AuthMechanism.h"
#include "AuthMechAnonymous.h"
#include "AuthMechExternal.h"
#include "AuthMechSRP.h"
#include "AuthMechLogon.h"
#include "SessionInternal.h"
#include "Transport.h"
#include "TransportList.h"
#include "BusUtil.h"
#include "BusEndpoint.h"
#include "LocalTransport.h"
#include "PeerState.h"
#include "KeyStore.h"
#include "BusInternal.h"
#include "AllJoynPeerObj.h"
#include "XmlHelper.h"
#include "ClientTransport.h"
#include "NullTransport.h"
#include "NamedPipeClientTransport.h"
#include "KeyInfoHelper.h"

#define QCC_MODULE "ALLJOYN"

using namespace std;
using namespace qcc;

// declare these in the anonymous namespace so that the symbols will not be
// visible outside this translation unit
namespace {
using namespace ajn;

struct JoinSessionAsyncCBContext {
    BusAttachment::JoinSessionAsyncCB* callback;
    SessionListener* sessionListener;
    std::string host;
    void* context;

    JoinSessionAsyncCBContext(BusAttachment::JoinSessionAsyncCB* callback, SessionListener* sessionListener, std::string host, void* context) :
        callback(callback),
        sessionListener(sessionListener),
        host(host),
        context(context)
    { }
};

struct LeaveSessionAsyncCBContext {
    BusAttachment::LeaveSessionAsyncCB* callback;
    void* context;

    LeaveSessionAsyncCBContext(BusAttachment::LeaveSessionAsyncCB* callback, void* context) :
        callback(callback),
        context(context)
    { }
};

struct SetLinkTimeoutAsyncCBContext {
    BusAttachment::SetLinkTimeoutAsyncCB* callback;
    void* context;

    SetLinkTimeoutAsyncCBContext(BusAttachment::SetLinkTimeoutAsyncCB* callback, void* context) :
        callback(callback),
        context(context)
    { }
};

struct PingAsyncCBContext {
    BusAttachment::PingAsyncCB* callback;
    void* context;

    PingAsyncCBContext(BusAttachment::PingAsyncCB* callback, void* context) :
        callback(callback),
        context(context)
    { }
};

struct GetNameOwnerCBContext {
    BusAttachment::GetNameOwnerAsyncCB* callback;
    void* context;

    GetNameOwnerCBContext(BusAttachment::GetNameOwnerAsyncCB* callback, void* context) :
        callback(callback),
        context(context)
    { }
};

struct AddMatchCBContext {
    BusAttachment::AddMatchAsyncCB* callback;
    void* context;

    AddMatchCBContext(BusAttachment::AddMatchAsyncCB* callback, void* context) :
        callback(callback),
        context(context)
    { }
};

struct RemoveMatchCBContext {
    BusAttachment::RemoveMatchAsyncCB* callback;
    void* context;

    RemoveMatchCBContext(BusAttachment::RemoveMatchAsyncCB* callback, void* context) :
        callback(callback),
        context(context)
    { }
};

}

namespace ajn {

// Maintain a list of all BusAttachment objects that can be found easily in a debugger
struct BusAttachmentSet {
    std::set<BusAttachment::Internal*> busInternalSet;
    qcc::Mutex lock;

    BusAttachmentSet() : lock(qcc::LOCK_LEVEL_BUSATTACHMENT_INTERNAL_BUSATTACHMENTSETLOCK) { }

    void Add(BusAttachment::Internal* busInternal)
    {
        lock.Lock(MUTEX_CONTEXT);
        busInternalSet.insert(busInternal);
        lock.Unlock(MUTEX_CONTEXT);
    }

    void Delete(BusAttachment::Internal* busInternal)
    {
        lock.Lock(MUTEX_CONTEXT);
        busInternalSet.erase(busInternal);
        lock.Unlock(MUTEX_CONTEXT);
    }
};
static BusAttachmentSet* s_allBusAttachments = nullptr;

AJ_PCSTR BusAttachment::Internal::STATE_MATCH_RULE = "type='signal',interface='org.alljoyn.Bus.Application',member='State',sessionless='t'";
uint32_t BusAttachment::Internal::APPLICATION_STATE_LISTENER_UNREGISTER_WAIT_INTERVAL = 5U;

BusAttachment::Internal::Internal(const char* appName,
                                  BusAttachment& bus,
                                  TransportFactoryContainer& factories,
                                  Router* router,
                                  bool allowRemoteMessages,
                                  const char* listenAddresses,
                                  uint32_t concurrency) :
    application(appName ? appName : "unknown"),
    bus(bus),
    listenersLock(LOCK_LEVEL_BUSATTACHMENT_INTERNAL_LISTENERSLOCK),
    listeners(),
    m_ioDispatch("iodisp", 96),
    transportList(bus, factories, &m_ioDispatch, concurrency),
    keyStore(application),
    authManager(keyStore),
    globalGuid(qcc::GUID128()),
    msgSerial(1),
    router(router ? router : new ClientRouter),
    localEndpoint(transportList.GetLocalTransport()->GetLocalEndpoint()),
    allowRemoteMessages(allowRemoteMessages),
    listenAddresses(listenAddresses ? listenAddresses : ""),
    stopLock(),
    stopCount(0),
    sessionPortListenersLock(LOCK_LEVEL_BUSATTACHMENT_INTERNAL_SESSIONPORTLISTENERSLOCK),
    aboutListenersLock(LOCK_LEVEL_BUSATTACHMENT_INTERNAL_ABOUTLISTENERSLOCK),
    joinLock(LOCK_LEVEL_BUSATTACHMENT_INTERNAL_JOINLOCK),
    permissionManager(),
    permissionConfigurator(bus),
    applicationStateListenersLock(LOCK_LEVEL_BUSATTACHMENT_INTERNAL_APPLICATIONSTATELISTENERSLOCK),
    observerManager(NULL),
    permissionConfigurationListener(NULL),
    permissionConfigurationListenerLock(LOCK_LEVEL_BUSATTACHMENT_INTERNAL_PERMISSIONCONFIGURATIONLISTENERLOCK)
{
#ifndef NDEBUG
    for (uint32_t index = 0; index < ArraySize(sessionsLock); index++) {
        MutexInternal::SetLevel(sessionsLock[index], LOCK_LEVEL_BUSATTACHMENT_INTERNAL_SESSIONSLOCK);
    }
#endif

    /*
     * Bus needs a pointer to this internal object.
     */
    bus.busInternal = this;

    /*
     * Create the standard interfaces
     */
    QStatus status = org::freedesktop::DBus::CreateInterfaces(bus);
    if (ER_OK != status) {
        QCC_LogError(status, ("Cannot create %s interface", org::freedesktop::DBus::InterfaceName));
    }
    status = org::alljoyn::CreateInterfaces(bus);
    if (ER_OK != status) {
        QCC_LogError(status, ("Cannot create %s interface", org::alljoyn::Bus::InterfaceName));
    }
    /* Register bus client authentication mechanisms */
    authManager.RegisterMechanism(AuthMechExternal::Factory, AuthMechExternal::AuthName());
    authManager.RegisterMechanism(AuthMechAnonymous::Factory, AuthMechAnonymous::AuthName());

    if (s_allBusAttachments) {
        s_allBusAttachments->Add(this);
    }
}

BusAttachment::Internal::~Internal()
{
    if (observerManager) {
        observerManager->Stop();
        observerManager->Join();
        delete observerManager;
        observerManager = NULL;
    }
    if (permissionConfigurationListener) {
        delete permissionConfigurationListener;
        permissionConfigurationListener = NULL;
    }
    /*
     * Make sure that all threads that might possibly access this object have been joined.
     */
    transportList.Join();
    delete router;
    router = NULL;

    if (s_allBusAttachments) {
        s_allBusAttachments->Delete(this);
    }
}

/*
 * Transport factory container for transports this bus attachment uses to communicate with the daemon.
 */
class ClientTransportFactoryContainer : public TransportFactoryContainer {
  public:

    ClientTransportFactoryContainer() : isInitialized(false) { }

    void Init()
    {
        lock.Lock();
        if (!isInitialized) {
            /*
             * Registration of transport factories is a one time operation.
             */
            if (NamedPipeClientTransport::IsAvailable()) {
                Add(new TransportFactory<NamedPipeClientTransport>(NamedPipeClientTransport::NamedPipeTransportName, true));
            }
            if (ClientTransport::IsAvailable()) {
                Add(new TransportFactory<ClientTransport>(ClientTransport::TransportName, true));
            }
            if (NullTransport::IsAvailable()) {
                Add(new TransportFactory<NullTransport>(NullTransport::TransportName, true));
            }
            isInitialized = true;
        }
        lock.Unlock();
    }

  private:
    bool isInitialized;
    qcc::Mutex lock;

};

static ClientTransportFactoryContainer* clientTransportsContainer = NULL;

BusAttachment::BusAttachment(const char* applicationName, bool allowRemoteMessages, uint32_t concurrencyLimit) :
    isStarted(false),
    isStopping(false),
    concurrency(concurrencyLimit),
    busInternal(new Internal(applicationName, *this, *clientTransportsContainer, nullptr, allowRemoteMessages, nullptr, concurrency)),
    translator(nullptr),
    joinObj(this)
{
    clientTransportsContainer->Init();
    QCC_DbgTrace(("BusAttachment client constructor (%p)", this));
}

BusAttachment::BusAttachment(Internal* busInternal, uint32_t concurrency) :
    isStarted(false),
    isStopping(false),
    concurrency(concurrency),
    busInternal(busInternal),
    translator(NULL),
    joinObj(this)
{
    clientTransportsContainer->Init();
    QCC_DbgTrace(("BusAttachment daemon constructor"));
}

BusAttachment::~BusAttachment(void)
{
    QCC_DbgTrace(("BusAttachment Destructor (%p)", this));

    StopInternal(true);

    /*
     * Other threads may be attempting to stop the bus. We need to wait for ALL
     * callers of BusAttachment::StopInternal() to exit before deleting the
     * object
     */
    while (busInternal->stopCount) {
        /*
         * We want to allow other calling threads to complete.  This means we
         * need to yield the CPU.  Sleep(0) yields the CPU to all threads of
         * equal or greater priority.  Other callers may be of lesser priority
         * so We need to yield the CPU to them, too.  We need to get ourselves
         * off of the ready queue, so we need to really execute a sleep.  The
         * Sleep(1) will translate into a mimimum sleep of one scheduling quantum
         * which is, for example, one Jiffy in Linux which is 1/250 second or
         * 4 ms.  It's not as arbitrary as it might seem.
         */
        qcc::Sleep(1);
    }

    /*
     * Make sure there is no BusListener callback is in progress.
     * Then remove listener and call ListenerUnregistered callback
     */
    busInternal->listenersLock.Lock(MUTEX_CONTEXT);
    Internal::ListenerSet::iterator it = busInternal->listeners.begin();
    while (it != busInternal->listeners.end()) {
        Internal::ProtectedBusListener l = *it;

        /* Remove listener and wait for any outstanding listener callback(s) to complete */
        busInternal->listeners.erase(it);
        busInternal->listenersLock.Unlock(MUTEX_CONTEXT);
        while (l.GetRefCount() > 1) {
            qcc::Sleep(4);
        }

        /* Call Listener Unregistered */
        (*l)->ListenerUnregistered();

        busInternal->listenersLock.Lock(MUTEX_CONTEXT);
        it = busInternal->listeners.begin();
    }
    busInternal->listenersLock.Unlock(MUTEX_CONTEXT);

    /*
     * Make sure there is no ApplicationStateListener callback is in progress.
     * Then remove listener.
     */
    busInternal->applicationStateListenersLock.Lock(MUTEX_CONTEXT);
    Internal::ApplicationStateListenerSet::iterator aslit = busInternal->applicationStateListeners.begin();
    while (aslit != busInternal->applicationStateListeners.end()) {
        Internal::ProtectedApplicationStateListener l = *aslit;

        /* Remove listener and wait for any outstanding listener callback(s) to complete */
        busInternal->applicationStateListeners.erase(aslit);
        busInternal->applicationStateListenersLock.Unlock(MUTEX_CONTEXT);
        while (l.GetRefCount() > 1) {
            qcc::Sleep(4);
        }

        busInternal->applicationStateListenersLock.Lock(MUTEX_CONTEXT);
        aslit = busInternal->applicationStateListeners.begin();
    }
    busInternal->applicationStateListenersLock.Unlock(MUTEX_CONTEXT);

    /* clear the contents of the session maps and wait for any outstanding callbacks. */
    for (size_t side = 0; side < ArraySize(busInternal->sessions); ++side) {
        busInternal->sessionsLock[side].Lock(MUTEX_CONTEXT);
        Internal::SessionMap::iterator sit = busInternal->sessions[side].begin();
        while (sit != busInternal->sessions[side].end()) {
            Internal::ProtectedSessionListener l = sit->second.listener;

            /* Remove listener and wait for any outstanding listener callback(s) to complete */
            busInternal->sessions[side].erase(sit);
            busInternal->sessionsLock[side].Unlock(MUTEX_CONTEXT);
            while (l.GetRefCount() > 1) {
                qcc::Sleep(4);
            }
            busInternal->sessionsLock[side].Lock(MUTEX_CONTEXT);
            sit = busInternal->sessions[side].begin();
        }
        busInternal->sessionsLock[side].Unlock(MUTEX_CONTEXT);
    }

    busInternal->sessionPortListenersLock.Lock(MUTEX_CONTEXT);
    /* clear the contents of the sessionPortListeners and wait for any outstanding callbacks. */
    Internal::SessionPortListenerMap::iterator split = busInternal->sessionPortListeners.begin();
    while (split != busInternal->sessionPortListeners.end()) {
        Internal::ProtectedSessionPortListener l = split->second;

        /* Remove listener and wait for any outstanding listener callback(s) to complete */
        busInternal->sessionPortListeners.erase(split);
        busInternal->sessionPortListenersLock.Unlock(MUTEX_CONTEXT);
        while (l.GetRefCount() > 1) {
            qcc::Sleep(4);
        }

        busInternal->sessionPortListenersLock.Lock(MUTEX_CONTEXT);
        split = busInternal->sessionPortListeners.begin();
    }
    busInternal->sessionPortListenersLock.Unlock(MUTEX_CONTEXT);

    /* Remove the BusAttachement internals */
    delete busInternal;
    busInternal = NULL;
}

uint32_t BusAttachment::GetConcurrency()
{
    return concurrency;
}

qcc::String BusAttachment::GetConnectSpec()
{
    return connectSpec;
}

QStatus BusAttachment::Start()
{
    QStatus status;

    QCC_DbgTrace(("BusAttachment::Start()"));

    /*
     * The variable isStarted indicates that the bus has been Start()ed, and has
     * not yet been Stop()ed.  As soon as a Join is completed, isStarted is set
     * to false.  We want to prevent the bus attachment from being started
     * multiple times to prevent very hard to debug problems where users try to
     * reuse bus attachments in the mistaken belief that it will somehow be more
     * efficient.  There are three state variables here and we check them all
     * separately (in order to be specific with error messages) before
     * continuing to allow a Start.
     */

    if (isStarted) {
        status = ER_BUS_BUS_ALREADY_STARTED;
        QCC_LogError(status, ("BusAttachment::Start(): Start called, but currently started."));
        return status;
    }

    if (isStopping) {
        status = ER_BUS_STOPPING;
        QCC_LogError(status, ("BusAttachment::Start(): Start called while stopping"));
        return status;
    }

    isStarted = true;

    /* Start the transports */
    status = busInternal->TransportsStart();

    if ((status == ER_OK) && isStopping) {
        status = ER_BUS_STOPPING;
        QCC_LogError(status, ("BusAttachment::Start bus was stopped while starting"));
    }

    if (status != ER_OK) {
        QCC_LogError(status, ("BusAttachment::Start failed to start"));
        busInternal->TransportsStop();
        WaitStopInternal();
    }
    return status;
}

QStatus BusAttachment::Internal::TransportConnect(const char* requestedConnectSpec)
{
    QStatus status;
    Transport* trans = requestedConnectSpec ? transportList.GetTransport(requestedConnectSpec) : nullptr;
    if (trans) {
        SessionOpts emptyOpts;
        BusEndpoint tempEp;
        status = trans->Connect(requestedConnectSpec, emptyOpts, tempEp);

        /* Make sure the remote side (daemon) is at least as new as the client */
        if ((status == ER_OK) && ((tempEp->GetEndpointType() == ENDPOINT_TYPE_REMOTE) ||
                                  (tempEp->GetEndpointType() == ENDPOINT_TYPE_BUS2BUS))) {
            RemoteEndpoint rem = RemoteEndpoint::cast(tempEp);
            /*
             * Reject a daemon whose ALLJOYN_PROTOCOL_VERSION is less than that of this
             * client. This check is complicated by the requirement to successfully connect to a
             * standard (non-AllJoyn) DBUs daemon regardless of version.
             *
             * If we are connected to an older ALLJOYN daemon, then reject the connection. If it
             * is a standard DBUS daemon (that doesn't report an alljoyn version) then ignore
             * the ALLJOYN_PROTOCOL_VERSION check.
             */
            if ((rem->GetRemoteAllJoynVersion() != 0) && (rem->GetRemoteProtocolVersion() < ALLJOYN_PROTOCOL_VERSION)) {
                QCC_DbgPrintf(("Rejecting daemon at %s because its protocol version (%d) is less than ours (%d)",
                               requestedConnectSpec, rem->GetRemoteProtocolVersion(), ALLJOYN_PROTOCOL_VERSION));
                TransportDisconnect(requestedConnectSpec);
                status = ER_BUS_INCOMPATIBLE_DAEMON;
            }
        }
    } else {
        status = ER_BUS_TRANSPORT_NOT_AVAILABLE;
    }
    return status;
}

QStatus BusAttachment::Internal::TransportConnect(const char* requestedConnectSpec, qcc::String& actualConnectSpec)
{
    const char* bundledConnectSpec = "null:";

    if (IsConnected() && !router->IsDaemon()) {
        return ER_BUS_ALREADY_CONNECTED;
    }

    QStatus status = TransportConnect(requestedConnectSpec);


    if (status == ER_OK) {
        actualConnectSpec = requestedConnectSpec;
    } else if (!router->IsDaemon() && (!requestedConnectSpec || strcmp(requestedConnectSpec, bundledConnectSpec))) {
        /*
         * Try using the null transport to connect to a bundled daemon if there is one
         */
        QStatus bundledStatus = TransportConnect(bundledConnectSpec);

        if (bundledStatus == ER_OK) {
            actualConnectSpec = bundledConnectSpec;
            status = bundledStatus;
        }
    }

    return status;
}

QStatus BusAttachment::Connect()
{
    const char* connectArgs;

#if defined(QCC_OS_GROUP_WINDOWS)
    if (qcc::NamedPipeWrapper::AreApisAvailable()) {
        connectArgs = "npipe:";
    } else {
        connectArgs = "tcp:addr=127.0.0.1,port=9955";
    }
#else
    connectArgs = "unix:abstract=alljoyn";
#endif

    return Connect(connectArgs);
}

QStatus BusAttachment::Connect(const char* requestedConnectSpec)
{
    QStatus status;

    if (!isStarted) {
        status = ER_BUS_BUS_NOT_STARTED;
    } else if (isStopping) {
        status = ER_BUS_STOPPING;
        QCC_LogError(status, ("BusAttachment::Connect cannot connect while bus is stopping"));
    } else {
        status = busInternal->TransportConnect(requestedConnectSpec, this->connectSpec);
        if (ER_OK == status) {
            status = RegisterSignalHandlers();
            if (ER_OK != status) {
                /*
                 * We connected but failed to fully realize the connection so disconnect to cleanup.
                 */
                busInternal->TransportDisconnect(this->connectSpec.c_str());
            }
        }
    }
    if (ER_OK != status) {
        QCC_LogError(status, ("BusAttachment::Connect failed"));
    }
    return status;
}

QStatus BusAttachment::RegisterSignalHandlers()
{
    QStatus status = ER_OK;

    /* If this is a client (non-daemon) bus attachment, then register signal handlers for BusListener */
    if (!busInternal->GetRouter().IsDaemon()) {
        /* Register org.freedesktop.DBus signal handler */
        const InterfaceDescription* iface = GetInterface(org::freedesktop::DBus::InterfaceName);
        QCC_ASSERT(iface);
        status = RegisterSignalHandler(busInternal,
                                       static_cast<MessageReceiver::SignalHandler>(&BusAttachment::Internal::AllJoynSignalHandler),
                                       iface->GetMember("NameOwnerChanged"),
                                       NULL);
        if (ER_OK == status) {
            Message reply(*this);
            MsgArg arg("s", "type='signal',interface='org.freedesktop.DBus'");
            const ProxyBusObject& dbusObj = this->GetDBusProxyObj();
            status = dbusObj.MethodCall(org::freedesktop::DBus::InterfaceName, "AddMatch", &arg, 1, reply);
        }
        /* Register org.alljoyn.Bus signal handler */
        const InterfaceDescription* ajIface = GetInterface(org::alljoyn::Bus::InterfaceName);
        if (ER_OK == status) {
            QCC_ASSERT(ajIface);
            status = RegisterSignalHandler(busInternal,
                                           static_cast<MessageReceiver::SignalHandler>(&BusAttachment::Internal::AllJoynSignalHandler),
                                           ajIface->GetMember("FoundAdvertisedName"),
                                           NULL);
        }
        if (ER_OK == status) {
            QCC_ASSERT(ajIface);
            status = RegisterSignalHandler(busInternal,
                                           static_cast<MessageReceiver::SignalHandler>(&BusAttachment::Internal::AllJoynSignalHandler),
                                           ajIface->GetMember("LostAdvertisedName"),
                                           NULL);
        }
        if (ER_OK == status) {
            QCC_ASSERT(ajIface);
            status = RegisterSignalHandler(busInternal,
                                           static_cast<MessageReceiver::SignalHandler>(&BusAttachment::Internal::AllJoynSignalHandler),
                                           ajIface->GetMember("SessionLostWithReasonAndDisposition"),
                                           NULL);
        }
        if (ER_OK == status) {
            QCC_ASSERT(ajIface);
            status = RegisterSignalHandler(busInternal,
                                           static_cast<MessageReceiver::SignalHandler>(&BusAttachment::Internal::AllJoynSignalHandler),
                                           ajIface->GetMember("MPSessionChangedWithReason"),
                                           NULL);
        }
        const InterfaceDescription* aboutIface = GetInterface(org::alljoyn::About::InterfaceName);
        if (ER_OK == status) {
            QCC_ASSERT(aboutIface);
            const ajn::InterfaceDescription::Member* announceSignalMember = aboutIface->GetMember("Announce");
            QCC_ASSERT(announceSignalMember);
            status = RegisterSignalHandler(busInternal,
                                           static_cast<MessageReceiver::SignalHandler>(&BusAttachment::Internal::AllJoynSignalHandler),
                                           announceSignalMember,
                                           NULL);
        }
        const InterfaceDescription* applicationIface = GetInterface(org::alljoyn::Bus::Application::InterfaceName);
        if (ER_OK == status) {
            QCC_ASSERT(applicationIface);
            const ajn::InterfaceDescription::Member* stateSignalMember = applicationIface->GetMember("State");
            QCC_ASSERT(stateSignalMember);
            status = RegisterSignalHandler(busInternal,
                                           static_cast<MessageReceiver::SignalHandler>(&BusAttachment::Internal::AllJoynSignalHandler),
                                           stateSignalMember,
                                           NULL);
        }
        if (ER_OK == status) {
            Message reply(*this);
            MsgArg arg("s", "type='signal',interface='org.alljoyn.Bus'");
            const ProxyBusObject& dbusObj = this->GetDBusProxyObj();
            status = dbusObj.MethodCall(org::freedesktop::DBus::InterfaceName, "AddMatch", &arg, 1, reply);
        }
    }
    return status;
}

QStatus BusAttachment::Internal::TransportDisconnect(const char* disconnectConnectSpec)
{
    QStatus status;

    if (!router->IsDaemon() && !IsConnected()) {
        status = ER_BUS_NOT_CONNECTED;
    } else {
        /* Terminate transport for connection */
        Transport* trans = transportList.GetTransport(disconnectConnectSpec);
        if (trans) {
            status = trans->Disconnect(disconnectConnectSpec);
        } else {
            status = ER_BUS_TRANSPORT_NOT_AVAILABLE;
        }
    }

    return status;
}

QStatus BusAttachment::Disconnect()
{
    QStatus status;

    if (!isStarted) {
        status = ER_BUS_BUS_NOT_STARTED;
    } else if (isStopping) {
        status = ER_BUS_STOPPING;
        QCC_LogError(status, ("BusAttachment::Disconnect cannot disconnect while bus is stopping"));
    } else {
        /*
         * Shut down the ObserverManager
         */
        if (busInternal->observerManager) {
            busInternal->observerManager->Stop();
            busInternal->observerManager->Join();
        }

        status = busInternal->TransportDisconnect(this->connectSpec.c_str());
        if (ER_OK == status) {
            UnregisterSignalHandlers();
        }
    }

    if (ER_OK != status) {
        QCC_LogError(status, ("BusAttachment::Disconnect failed"));
    }
    return status;
}

QStatus BusAttachment::Disconnect(const char* disconnectConnectSpec)
{
    QCC_UNUSED(disconnectConnectSpec);
    return Disconnect();
}

void BusAttachment::UnregisterSignalHandlers()
{
    /* Unregister signal handlers if this is a client-side bus attachment */
    if (!busInternal->GetRouter().IsDaemon()) {
        const InterfaceDescription* dbusIface = GetInterface(org::freedesktop::DBus::InterfaceName);
        if (dbusIface) {
            UnregisterSignalHandler(busInternal,
                                    static_cast<MessageReceiver::SignalHandler>(&BusAttachment::Internal::AllJoynSignalHandler),
                                    dbusIface->GetMember("NameOwnerChanged"),
                                    NULL);
        }
        const InterfaceDescription* alljoynIface = GetInterface(org::alljoyn::Bus::InterfaceName);
        if (alljoynIface) {
            UnregisterSignalHandler(busInternal,
                                    static_cast<MessageReceiver::SignalHandler>(&BusAttachment::Internal::AllJoynSignalHandler),
                                    alljoynIface->GetMember("FoundAdvertisedName"),
                                    NULL);
        }
        if (alljoynIface) {
            UnregisterSignalHandler(busInternal,
                                    static_cast<MessageReceiver::SignalHandler>(&BusAttachment::Internal::AllJoynSignalHandler),
                                    alljoynIface->GetMember("LostAdvertisedName"),
                                    NULL);
        }
        if (alljoynIface) {
            UnregisterSignalHandler(busInternal,
                                    static_cast<MessageReceiver::SignalHandler>(&BusAttachment::Internal::AllJoynSignalHandler),
                                    alljoynIface->GetMember("SessionLostWithReasonAndDisposition"),
                                    NULL);
        }
        if (alljoynIface) {
            UnregisterSignalHandler(busInternal,
                                    static_cast<MessageReceiver::SignalHandler>(&BusAttachment::Internal::AllJoynSignalHandler),
                                    alljoynIface->GetMember("MPSessionChangedWithReason"),
                                    NULL);
        }
        const InterfaceDescription* aboutIface = GetInterface(org::alljoyn::About::InterfaceName);
        if (aboutIface) {
            const ajn::InterfaceDescription::Member* announceSignalMember = aboutIface->GetMember("Announce");
            QCC_ASSERT(announceSignalMember);
            UnregisterSignalHandler(busInternal,
                                    static_cast<MessageReceiver::SignalHandler>(&BusAttachment::Internal::AllJoynSignalHandler),
                                    announceSignalMember,
                                    NULL);
        }
        const InterfaceDescription* applicationIface = GetInterface(org::alljoyn::Bus::Application::InterfaceName);
        if (applicationIface) {
            const ajn::InterfaceDescription::Member* stateSignalMember = applicationIface->GetMember("State");
            QCC_ASSERT(stateSignalMember);
            UnregisterSignalHandler(busInternal,
                                    static_cast<MessageReceiver::SignalHandler>(&BusAttachment::Internal::AllJoynSignalHandler),
                                    stateSignalMember,
                                    NULL);
        }
    }
}

QStatus BusAttachment::Stop(void)
{
    return StopInternal(false);
}

/*
 * Note if called with blockUntilStopped == false this function must not do anything that might block.
 * Because we don't know what kind of cleanup various transports may do on Stop() the transports are
 * stopped on the ThreadExit callback for the dispatch thread.
 */
QStatus BusAttachment::StopInternal(bool blockUntilStopped)
{
    QStatus status = ER_OK;
    if (isStarted) {
        isStopping = true;
        /*
         * Let bus listeners know the bus is stopping.
         */
        busInternal->listenersLock.Lock(MUTEX_CONTEXT);
        Internal::ListenerSet::iterator it = busInternal->listeners.begin();
        while (it != busInternal->listeners.end()) {
            Internal::ProtectedBusListener l = *it;
            busInternal->listenersLock.Unlock(MUTEX_CONTEXT);
            (*l)->BusStopping();
            busInternal->listenersLock.Lock(MUTEX_CONTEXT);
            it = busInternal->listeners.upper_bound(l);
        }
        busInternal->listenersLock.Unlock(MUTEX_CONTEXT);

        /* Stop the transport list */
        status = busInternal->TransportsStop();
        if (ER_OK != status) {
            QCC_LogError(status, ("TransportList::Stop() failed"));
        }

        /* Stop the threads currently waiting for join to complete */
        busInternal->joinLock.Lock();
        map<Thread*, Internal::JoinContext>::iterator jit = busInternal->joinThreads.begin();
        while (jit != busInternal->joinThreads.end()) {
            jit++->first->Alert(1);
        }
        busInternal->joinLock.Unlock();

        if ((status == ER_OK) && blockUntilStopped) {
            WaitStopInternal();
        }
    }
    return status;
}

QStatus BusAttachment::Join()
{
    QCC_DbgTrace(("BusAttachment::Join"));
    WaitStopInternal();
    return ER_OK;
}

void BusAttachment::WaitStopInternal()
{
    QCC_DbgTrace(("BusAttachment::WaitStopInternal"));
    if (isStarted) {
        /*
         * We use a combination of a mutex and a counter to ensure that all threads that are
         * blocked waiting for the bus attachment to stop are actually blocked.
         */
        IncrementAndFetch(&busInternal->stopCount);
        busInternal->stopLock.Lock(MUTEX_CONTEXT);

        /* Wait for any threads stuck in JoinSession to exit */
        busInternal->joinLock.Lock();
        while (!busInternal->joinThreads.empty()) {
            busInternal->joinLock.Unlock();
            qcc::Sleep(2);
            busInternal->joinLock.Lock();
        }
        busInternal->joinLock.Unlock();

        /*
         * In the case where more than one thread has called WaitStopInternal() the first thread in will
         * clear the isStarted flag.
         */
        if (isStarted) {
            busInternal->TransportsJoin();

            /* Clear peer state */
            busInternal->peerStateTable.Clear();

            isStarted = false;
            isStopping = false;
        }

        busInternal->stopLock.Unlock(MUTEX_CONTEXT);
        DecrementAndFetch(&busInternal->stopCount);
    }
}

QStatus BusAttachment::CreateInterface(const char* name, InterfaceDescription*& iface, InterfaceSecurityPolicy secPolicy)
{
    if (!IsLegalInterfaceName(name)) {
        iface = NULL;
        return ER_BAD_ARG_1;
    }

    if (NULL != GetInterface(name)) {
        iface = NULL;
        return ER_BUS_IFACE_ALREADY_EXISTS;
    }
    std::string key = String(name);
    InterfaceDescription intf(name, secPolicy);
    iface = &(busInternal->ifaceDescriptions.insert(pair<std::string, InterfaceDescription>(key, intf)).first->second);
    return ER_OK;
}

QStatus BusAttachment::DeleteInterface(InterfaceDescription& iface)
{
    /* Get the (hopefully) unactivated interface */
    map<std::string, InterfaceDescription>::iterator it = busInternal->ifaceDescriptions.find(std::string(iface.GetName()));
    if ((it != busInternal->ifaceDescriptions.end()) && !it->second.isActivated) {
        busInternal->ifaceDescriptions.erase(it);
        return ER_OK;
    } else {
        return ER_BUS_NO_SUCH_INTERFACE;
    }
}

size_t BusAttachment::GetInterfaces(const InterfaceDescription** ifaces, size_t numIfaces) const
{
    size_t count = 0;
    map<std::string, InterfaceDescription>::const_iterator it;
    for (it = busInternal->ifaceDescriptions.begin(); it != busInternal->ifaceDescriptions.end(); it++) {
        if (it->second.isActivated) {
            if (ifaces && (count < numIfaces)) {
                ifaces[count] = &(it->second);
            }
            ++count;
        }
    }
    return count;
}

const InterfaceDescription* BusAttachment::GetInterface(const char* name) const
{
    if (name == nullptr) {
        return nullptr;
    }

    map<std::string, InterfaceDescription>::const_iterator it = busInternal->ifaceDescriptions.find(std::string(name));
    if ((it != busInternal->ifaceDescriptions.end()) && it->second.isActivated) {
        return &(it->second);
    } else {
        return nullptr;
    }
}

QStatus BusAttachment::RegisterKeyStoreListener(KeyStoreListener& listener)
{
    return busInternal->keyStore.SetListener(listener);
}

QStatus BusAttachment::UnregisterKeyStoreListener()
{
    return busInternal->keyStore.SetDefaultListener();
}

void BusAttachment::ClearKeyStore()
{
    busInternal->keyStore.Clear();
}

QStatus BusAttachment::DeleteDefaultKeyStore(const char* applicationName)
{
    return DeleteDefaultKeyStoreFile(applicationName, nullptr);
}

const qcc::String BusAttachment::GetUniqueName() const
{
    /*
     * Cannot have a valid unique name if not connected to the bus.
     */
    if (!IsConnected()) {
        return "";
    }
    return busInternal->localEndpoint->GetUniqueName();
}

const qcc::String& BusAttachment::GetGlobalGUIDString() const
{
    return busInternal->GetGlobalGUID().ToString();
}

const qcc::String& BusAttachment::GetGlobalGUIDShortString() const
{
    return busInternal->GetGlobalGUID().ToShortString();
}

const ProxyBusObject& BusAttachment::GetDBusProxyObj()
{
    return busInternal->GetDBusProxyObj();
}

const ProxyBusObject& BusAttachment::GetAllJoynProxyObj()
{
    return busInternal->localEndpoint->GetAllJoynProxyObj();
}

const ProxyBusObject& BusAttachment::GetAllJoynDebugObj()
{
    return busInternal->localEndpoint->GetAllJoynDebugObj();
}

QStatus BusAttachment::RegisterSignalHandlerWithRule(MessageReceiver* receiver,
                                                     MessageReceiver::SignalHandler signalHandler,
                                                     const InterfaceDescription::Member* member,
                                                     const char* matchRule)
{
    return busInternal->RegisterSignalHandler(receiver, signalHandler, member, matchRule);
}

QStatus BusAttachment::RegisterSignalHandler(MessageReceiver* receiver,
                                             MessageReceiver::SignalHandler signalHandler,
                                             const InterfaceDescription::Member* member,
                                             const char* srcPath)
{
    if (!member) {
        return ER_BAD_ARG_3;
    }

    qcc::String matchRule("type='signal',member='");
    matchRule += String(member->name) + "',interface='" + member->iface->GetName() + "'";
    if (srcPath && (srcPath[0] != '\0')) {
        matchRule += String(",path='") + srcPath + "'";
    }
    return RegisterSignalHandlerWithRule(receiver, signalHandler, member, matchRule.c_str());
}

QStatus BusAttachment::UnregisterSignalHandler(MessageReceiver* receiver,
                                               MessageReceiver::SignalHandler signalHandler,
                                               const InterfaceDescription::Member* member,
                                               const char* srcPath)
{
    if (!member) {
        return ER_BAD_ARG_3;
    }

    qcc::String matchRule("type='signal',member='");
    matchRule += String(member->name) + "',interface='" + member->iface->GetName() + "'";
    if (srcPath && (srcPath[0] != '\0')) {
        matchRule += String(",path='") + srcPath + "'";
    }
    return UnregisterSignalHandlerWithRule(receiver, signalHandler, member, matchRule.c_str());
}

QStatus BusAttachment::UnregisterSignalHandlerWithRule(MessageReceiver* receiver,
                                                       MessageReceiver::SignalHandler signalHandler,
                                                       const InterfaceDescription::Member* member,
                                                       const char* matchRule)
{
    return busInternal->UnregisterSignalHandler(receiver, signalHandler, member, matchRule);
}

QStatus BusAttachment::UnregisterAllHandlers(MessageReceiver* receiver)
{
    return busInternal->localEndpoint->UnregisterAllHandlers(receiver);
}

bool BusAttachment::Internal::IsConnected() const {
    QCC_ASSERT(router);
    return router->IsBusRunning();
}

QStatus BusAttachment::Internal::AddApplicationStateListener(ApplicationStateListener& applicationStateListener)
{
    QStatus status = ER_OK;
    ApplicationStateListener* pListener = &applicationStateListener;
    ProtectedApplicationStateListener protectedListener(pListener);

    applicationStateListenersLock.Lock(MUTEX_CONTEXT);

    if (applicationStateListeners.end() != applicationStateListeners.find(protectedListener)) {
        status = ER_APPLICATION_STATE_LISTENER_ALREADY_EXISTS;
    } else {
        applicationStateListeners.insert(pListener);
    }

    applicationStateListenersLock.Unlock(MUTEX_CONTEXT);

    return status;
}

QStatus BusAttachment::Internal::RemoveApplicationStateListener(ApplicationStateListener& applicationStateListener)
{
    QStatus status = ER_OK;
    ApplicationStateListener* pListener = &applicationStateListener;
    ProtectedApplicationStateListener protectedListener(pListener);

    applicationStateListenersLock.Lock(MUTEX_CONTEXT);

    ApplicationStateListenerSet::iterator it = applicationStateListeners.find(protectedListener);

    if (it == applicationStateListeners.end()) {
        status = ER_APPLICATION_STATE_LISTENER_NO_SUCH_LISTENER;
    } else {

        /* Wait for all refs to ProtectedApplicationStateListener to exit */
        while ((it != applicationStateListeners.end()) && (it->GetRefCount() > 1)) {
            ProtectedApplicationStateListener l = *it;
            applicationStateListenersLock.Unlock(MUTEX_CONTEXT);
            qcc::Sleep(APPLICATION_STATE_LISTENER_UNREGISTER_WAIT_INTERVAL);
            applicationStateListenersLock.Lock(MUTEX_CONTEXT);
            it = applicationStateListeners.find(l);
        }

        /* Delete the listeners entry */
        if (it != applicationStateListeners.end()) {
            applicationStateListeners.erase(it);
        } else {
            status = ER_APPLICATION_STATE_LISTENER_NO_SUCH_LISTENER;
        }
    }

    applicationStateListenersLock.Unlock(MUTEX_CONTEXT);

    return status;
}

bool BusAttachment::IsConnected() const {
    return busInternal->IsConnected();
}

QStatus BusAttachment::RegisterBusObject(BusObject& obj, bool secure) {
    return busInternal->localEndpoint->RegisterBusObject(obj, secure);
}

void BusAttachment::UnregisterBusObject(BusObject& object)
{
    busInternal->localEndpoint->UnregisterBusObject(object);
}

QStatus BusAttachment::EnablePeerSecurity(const char* authMechanisms,
                                          AuthListener* authListener,
                                          const char* keyStoreFileName,
                                          bool isShared,
                                          PermissionConfigurationListener* permissionConfigurationListener)
{
    QStatus status = ER_OK;

    if (isShared == false) {
        QCC_LogError(ER_WARNING, ("EnablePeerSecurity: Ignoring isShared parameter. KeyStore always works in shared mode."));
    }

    busInternal->SetPermissionConfigurationListener(permissionConfigurationListener);

    /* If there are no auth mechanisms peer security is being disabled. */
    if (authMechanisms) {
        busInternal->keyStore.SetKeyEventListener(&busInternal->ksKeyEventListener);
        status = busInternal->keyStore.Init(keyStoreFileName);
        if (status == ER_KEY_STORE_ALREADY_INITIALIZED) {
            status = ER_OK;
        }
        if (status == ER_OK) {
            /* Register peer-to-peer authentication mechanisms */
            busInternal->authManager.RegisterMechanism(AuthMechSRP::Factory, AuthMechSRP::AuthName());
            busInternal->authManager.RegisterMechanism(AuthMechLogon::Factory, AuthMechLogon::AuthName());
            /* Validate the list of auth mechanisms */
            status =  busInternal->authManager.CheckNames(authMechanisms);
            /* Clear peer state */
            busInternal->peerStateTable.Clear();
        }
    } else {
        status = busInternal->keyStore.Reset();
        busInternal->authManager.UnregisterMechanism(AuthMechSRP::AuthName());
        busInternal->authManager.UnregisterMechanism(AuthMechLogon::AuthName());
    }

    if (status == ER_OK) {
        AllJoynPeerObj* peerObj = busInternal->localEndpoint->GetPeerObj();
        if (peerObj) {
            peerObj->SetupPeerAuthentication(authMechanisms ? String(authMechanisms) : String(), authMechanisms ? authListener : NULL, *this);
        } else {
            return ER_BUS_SECURITY_NOT_ENABLED;
        }
    }
    return status;
}

bool BusAttachment::IsPeerSecurityEnabled()
{
    AllJoynPeerObj* peerObj = busInternal->localEndpoint->GetPeerObj();
    if (peerObj) {
        return peerObj->AuthenticationEnabled();
    } else {
        return false;
    }
}

QStatus BusAttachment::AddLogonEntry(const char* authMechanism, const char* userName, const char* password)
{
    if (!authMechanism) {
        return ER_BAD_ARG_2;
    }
    if (!userName) {
        return ER_BAD_ARG_3;
    }
    if (strcmp(authMechanism, "ALLJOYN_SRP_LOGON") == 0) {
        return AuthMechLogon::AddLogonEntry(busInternal->keyStore, userName, password);
    } else {
        return ER_BUS_INVALID_AUTH_MECHANISM;
    }
}

QStatus BusAttachment::RequestName(const char* requestedName, uint32_t flags)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    Message reply(*this);
    MsgArg args[2];
    size_t numArgs = ArraySize(args);

    MsgArg::Set(args, numArgs, "su", requestedName, flags);

    const ProxyBusObject& dbusObj = this->GetDBusProxyObj();
    QStatus status = dbusObj.MethodCall(org::freedesktop::DBus::InterfaceName, "RequestName", args, numArgs, reply);
    if (ER_OK == status) {
        uint32_t disposition;
        status = reply->GetArgs("u", &disposition);
        if (ER_OK == status) {
            switch (disposition) {
            case DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER:
                break;

            case DBUS_REQUEST_NAME_REPLY_IN_QUEUE:
                status = ER_DBUS_REQUEST_NAME_REPLY_IN_QUEUE;
                break;

            case DBUS_REQUEST_NAME_REPLY_EXISTS:
                status = ER_DBUS_REQUEST_NAME_REPLY_EXISTS;
                break;

            case DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER:
                status = ER_DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER;
                break;

            default:
                status = ER_BUS_UNEXPECTED_DISPOSITION;
                break;
            }
        }
    } else {
        QCC_LogError(status, ("%s.RequestName returned ERROR_MESSAGE (error=%s)", org::freedesktop::DBus::InterfaceName, reply->GetErrorDescription().c_str()));
    }
    return status;
}

QStatus BusAttachment::ReleaseName(const char* name)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    Message reply(*this);
    MsgArg args[1];
    size_t numArgs = ArraySize(args);

    MsgArg::Set(args, numArgs, "s", name);

    const ProxyBusObject& dbusObj = this->GetDBusProxyObj();
    QStatus status = dbusObj.MethodCall(org::freedesktop::DBus::InterfaceName, "ReleaseName", args, numArgs, reply);
    if (ER_OK == status) {
        uint32_t disposition;
        status = reply->GetArgs("u", &disposition);
        if (ER_OK == status) {
            switch (disposition) {
            case DBUS_RELEASE_NAME_REPLY_RELEASED:
                break;

            case DBUS_RELEASE_NAME_REPLY_NON_EXISTENT:
                status = ER_DBUS_RELEASE_NAME_REPLY_NON_EXISTENT;
                break;

            case DBUS_RELEASE_NAME_REPLY_NOT_OWNER:
                status = ER_DBUS_RELEASE_NAME_REPLY_NOT_OWNER;
                break;

            default:
                status = ER_BUS_UNEXPECTED_DISPOSITION;
                break;
            }
        }
    } else {
        QCC_LogError(status, ("%s.ReleaseName returned ERROR_MESSAGE (error=%s)", org::freedesktop::DBus::InterfaceName, reply->GetErrorDescription().c_str()));
    }
    return status;
}

QStatus BusAttachment::AddMatch(const char* rule)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    Message reply(*this);
    MsgArg args[1];
    size_t numArgs = ArraySize(args);

    MsgArg::Set(args, numArgs, "s", rule);

    const ProxyBusObject& dbusObj = this->GetDBusProxyObj();
    QStatus status = dbusObj.MethodCall(org::freedesktop::DBus::InterfaceName, "AddMatch", args, numArgs, reply);
    if (ER_OK != status) {
        QCC_LogError(status, ("%s.AddMatch returned ERROR_MESSAGE (error=%s)", org::freedesktop::DBus::InterfaceName, reply->GetErrorDescription().c_str()));
    }
    return status;
}

QStatus BusAttachment::AddMatchAsync(const char* rule, AddMatchAsyncCB* callback, void* context)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    MsgArg args[1];
    size_t numArgs = ArraySize(args);

    MsgArg::Set(args, numArgs, "s", rule);

    const ProxyBusObject& dbusObj = GetDBusProxyObj();
    AddMatchCBContext* cbCtx = new AddMatchCBContext(callback, context);
    QStatus status = dbusObj.MethodCallAsync(org::freedesktop::DBus::InterfaceName, "AddMatch",
                                             busInternal,
                                             static_cast<MessageReceiver::ReplyHandler>(&BusAttachment::Internal::AddMatchAsyncCB),
                                             args, numArgs, cbCtx);
    if (ER_OK != status) {
        QCC_LogError(status, ("Failed to call %s.AddMatch", org::freedesktop::DBus::InterfaceName));
        delete cbCtx;
    }
    return status;
}

void BusAttachment::Internal::AddMatchAsyncCB(Message& reply, void* context)
{
    AddMatchCBContext* ctx = reinterpret_cast<AddMatchCBContext*>(context);
    QStatus status = ER_OK;

    if (reply->GetType() == MESSAGE_ERROR) {
        status = ER_BUS_REPLY_IS_ERROR_MESSAGE;
    }

    /* Call the callback */
    ctx->callback->AddMatchCB(status, ctx->context);
    delete ctx;
}

QStatus BusAttachment::AddMatchNonBlocking(const char* rule)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    MsgArg args[1];
    size_t numArgs = ArraySize(args);

    MsgArg::Set(args, numArgs, "s", rule);

    const ProxyBusObject& dbusObj = this->GetDBusProxyObj();
    QStatus status = dbusObj.MethodCall(org::freedesktop::DBus::InterfaceName, "AddMatch", args, numArgs);
    return status;
}

QStatus BusAttachment::RemoveMatch(const char* rule)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    Message reply(*this);
    MsgArg args[1];
    size_t numArgs = ArraySize(args);

    MsgArg::Set(args, numArgs, "s", rule);

    const ProxyBusObject& dbusObj = this->GetDBusProxyObj();
    QStatus status = dbusObj.MethodCall(org::freedesktop::DBus::InterfaceName, "RemoveMatch", args, numArgs, reply);
    if (ER_OK != status) {
        if (strcmp(reply->GetErrorName(), "org.freedesktop.DBus.Error.MatchRuleNotFound") == 0) {
            status = ER_BUS_MATCH_RULE_NOT_FOUND;
            QCC_DbgTrace(("%s.RemoveMatch returned org.freedesktop.DBus.Error.MatchRuleNotFound",
                          org::freedesktop::DBus::InterfaceName));
        } else {
            QCC_LogError(status, ("%s.RemoveMatch returned ERROR_MESSAGE (error=%s)",
                                  org::freedesktop::DBus::InterfaceName, reply->GetErrorDescription().c_str()));
        }
    }
    return status;
}

QStatus BusAttachment::RemoveMatchAsync(const char* rule, RemoveMatchAsyncCB* callback, void* context)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    MsgArg args[1];
    size_t numArgs = ArraySize(args);

    MsgArg::Set(args, numArgs, "s", rule);

    const ProxyBusObject& dbusObj = this->GetDBusProxyObj();
    RemoveMatchCBContext* cbCtx = new RemoveMatchCBContext(callback, context);
    QStatus status = dbusObj.MethodCallAsync(org::freedesktop::DBus::InterfaceName, "RemoveMatch",
                                             busInternal,
                                             static_cast<MessageReceiver::ReplyHandler>(&BusAttachment::Internal::RemoveMatchAsyncCB),
                                             args, numArgs, cbCtx);
    if (ER_OK != status) {
        QCC_LogError(status, ("Failed to call %s.RemoveMatch", org::freedesktop::DBus::InterfaceName));
        delete cbCtx;
    }
    return status;
}

void BusAttachment::Internal::RemoveMatchAsyncCB(Message& reply, void* context)
{
    RemoveMatchCBContext* ctx = reinterpret_cast<RemoveMatchCBContext*>(context);
    QStatus status = ER_OK;

    if (reply->GetType() == MESSAGE_ERROR) {
        status = ER_BUS_REPLY_IS_ERROR_MESSAGE;
    }

    /* Call the callback */
    ctx->callback->RemoveMatchCB(status, ctx->context);
    delete ctx;
}

QStatus BusAttachment::RemoveMatchNonBlocking(const char* rule)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    MsgArg args[1];
    size_t numArgs = ArraySize(args);

    MsgArg::Set(args, numArgs, "s", rule);

    const ProxyBusObject& dbusObj = this->GetDBusProxyObj();
    QStatus status = dbusObj.MethodCall(org::freedesktop::DBus::InterfaceName, "RemoveMatch", args, numArgs);
    return status;
}

QStatus BusAttachment::FindAdvertisedName(const char* namePrefix)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    if (!namePrefix) {
        return ER_BAD_ARG_1;
    }

    Message reply(*this);
    MsgArg args[1];
    size_t numArgs = ArraySize(args);

    MsgArg::Set(args, numArgs, "s", namePrefix);

    const ProxyBusObject& alljoynObj = this->GetAllJoynProxyObj();
    QStatus status = alljoynObj.MethodCall(org::alljoyn::Bus::InterfaceName, "FindAdvertisedName", args, numArgs, reply);
    if (ER_OK == status) {
        uint32_t disposition;
        status = reply->GetArgs("u", &disposition);
        if (ER_OK == status) {
            switch (disposition) {
            case ALLJOYN_FINDADVERTISEDNAME_REPLY_SUCCESS:
                break;

            case ALLJOYN_FINDADVERTISEDNAME_REPLY_ALREADY_DISCOVERING:
                status = ER_ALLJOYN_FINDADVERTISEDNAME_REPLY_ALREADY_DISCOVERING;
                break;

            case ALLJOYN_FINDADVERTISEDNAME_REPLY_FAILED:
                status = ER_ALLJOYN_FINDADVERTISEDNAME_REPLY_FAILED;
                break;

            default:
                status = ER_BUS_UNEXPECTED_DISPOSITION;
                break;
            }
        }
    } else {
        QCC_LogError(status, ("%s.FindAdvertisedName returned ERROR_MESSAGE (error=%s)", org::alljoyn::Bus::InterfaceName, reply->GetErrorDescription().c_str()));
    }
    return status;
}

QStatus BusAttachment::FindAdvertisedNameByTransport(const char* namePrefix, TransportMask transports)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    if (!namePrefix) {
        return ER_BAD_ARG_1;
    }

    Message reply(*this);
    MsgArg args[2];
    size_t numArgs = ArraySize(args);

    MsgArg::Set(args, numArgs, "sq", namePrefix, transports);

    const ProxyBusObject& alljoynObj = this->GetAllJoynProxyObj();
    QStatus status = alljoynObj.MethodCall(org::alljoyn::Bus::InterfaceName, "FindAdvertisedNameByTransport", args, numArgs, reply);
    if (ER_OK == status) {
        uint32_t disposition;
        status = reply->GetArgs("u", &disposition);
        if (ER_OK == status) {
            switch (disposition) {
            case ALLJOYN_FINDADVERTISEDNAME_REPLY_SUCCESS:
                break;

            case ALLJOYN_FINDADVERTISEDNAME_REPLY_ALREADY_DISCOVERING:
                status = ER_ALLJOYN_FINDADVERTISEDNAME_REPLY_ALREADY_DISCOVERING;
                break;

            case ALLJOYN_FINDADVERTISEDNAME_REPLY_FAILED:
                status = ER_ALLJOYN_FINDADVERTISEDNAME_REPLY_FAILED;
                break;

            case ALLJOYN_FINDADVERTISEDNAME_REPLY_TRANSPORT_NOT_AVAILABLE:
                status = ER_ALLJOYN_FINDADVERTISEDNAME_REPLY_TRANSPORT_NOT_AVAILABLE;
                break;

            default:
                status = ER_BUS_UNEXPECTED_DISPOSITION;
                break;
            }
        }
    } else {
        QCC_LogError(status, ("%s.FindAdvertisedName returned ERROR_MESSAGE (error=%s)", org::alljoyn::Bus::InterfaceName, reply->GetErrorDescription().c_str()));
    }
    return status;
}

QStatus BusAttachment::CancelFindAdvertisedName(const char* namePrefix)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    Message reply(*this);
    MsgArg args[1];
    size_t numArgs = ArraySize(args);

    MsgArg::Set(args, numArgs, "s", namePrefix);

    const ProxyBusObject& alljoynObj = this->GetAllJoynProxyObj();
    QStatus status = alljoynObj.MethodCall(org::alljoyn::Bus::InterfaceName, "CancelFindAdvertisedName", args, numArgs, reply);
    if (ER_OK == status) {
        uint32_t disposition;
        status = reply->GetArgs("u", &disposition);
        if (ER_OK == status) {
            switch (disposition) {
            case ALLJOYN_CANCELFINDADVERTISEDNAME_REPLY_SUCCESS:
                break;

            case ALLJOYN_CANCELFINDADVERTISEDNAME_REPLY_FAILED:
                status = ER_ALLJOYN_CANCELFINDADVERTISEDNAME_REPLY_FAILED;
                break;

            default:
                status = ER_BUS_UNEXPECTED_DISPOSITION;
                break;
            }
        }
    } else {
        QCC_LogError(status, ("%s.CancelFindAdvertisedName returned ERROR_MESSAGE (error=%s)", org::alljoyn::Bus::InterfaceName, reply->GetErrorDescription().c_str()));
    }
    return status;
}

QStatus BusAttachment::CancelFindAdvertisedNameByTransport(const char* namePrefix, TransportMask transports)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    Message reply(*this);
    MsgArg args[2];
    size_t numArgs = ArraySize(args);

    MsgArg::Set(args, numArgs, "sq", namePrefix, transports);

    const ProxyBusObject& alljoynObj = this->GetAllJoynProxyObj();
    QStatus status = alljoynObj.MethodCall(org::alljoyn::Bus::InterfaceName, "CancelFindAdvertisedNameByTransport", args, numArgs, reply);
    if (ER_OK == status) {
        uint32_t disposition;
        status = reply->GetArgs("u", &disposition);
        if (ER_OK == status) {
            switch (disposition) {
            case ALLJOYN_CANCELFINDADVERTISEDNAME_REPLY_SUCCESS:
                break;

            case ALLJOYN_CANCELFINDADVERTISEDNAME_REPLY_FAILED:
                status = ER_ALLJOYN_CANCELFINDADVERTISEDNAME_REPLY_FAILED;
                break;

            default:
                status = ER_BUS_UNEXPECTED_DISPOSITION;
                break;
            }
        }
    } else {
        QCC_LogError(status, ("%s.CancelFindAdvertisedName returned ERROR_MESSAGE (error=%s)", org::alljoyn::Bus::InterfaceName, reply->GetErrorDescription().c_str()));
    }
    return status;
}

QStatus BusAttachment::AdvertiseName(const char* name, TransportMask transports)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    Message reply(*this);
    MsgArg args[2];
    size_t numArgs = ArraySize(args);

    MsgArg::Set(args, numArgs, "sq", name, transports);

    const ProxyBusObject& alljoynObj = this->GetAllJoynProxyObj();
    QStatus status = alljoynObj.MethodCall(org::alljoyn::Bus::InterfaceName, "AdvertiseName", args, numArgs, reply);
    if (ER_OK == status) {
        int32_t disposition;
        status = reply->GetArgs("u", &disposition);
        if (ER_OK == status) {
            switch (disposition) {
            case ALLJOYN_ADVERTISENAME_REPLY_SUCCESS:
                break;

            case ALLJOYN_ADVERTISENAME_REPLY_ALREADY_ADVERTISING:
                status = ER_ALLJOYN_ADVERTISENAME_REPLY_ALREADY_ADVERTISING;
                break;

            case ALLJOYN_ADVERTISENAME_REPLY_FAILED:
                status = ER_ALLJOYN_ADVERTISENAME_REPLY_FAILED;
                break;

            case ALLJOYN_ADVERTISENAME_REPLY_TRANSPORT_NOT_AVAILABLE:
                status = ER_ALLJOYN_ADVERTISENAME_REPLY_TRANSPORT_NOT_AVAILABLE;
                break;

            default:
                status = ER_BUS_UNEXPECTED_DISPOSITION;
                break;
            }
        }
    } else {
        QCC_LogError(status, ("%s.AdvertiseName returned ERROR_MESSAGE (error=%s)", org::alljoyn::Bus::InterfaceName, reply->GetErrorDescription().c_str()));
    }
    return status;
}

QStatus BusAttachment::CancelAdvertiseName(const char* name, TransportMask transports)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    Message reply(*this);
    MsgArg args[2];
    size_t numArgs = ArraySize(args);

    MsgArg::Set(args, numArgs, "sq", name, transports);

    const ProxyBusObject& alljoynObj = this->GetAllJoynProxyObj();
    QStatus status = alljoynObj.MethodCall(org::alljoyn::Bus::InterfaceName, "CancelAdvertiseName", args, numArgs, reply);
    if (ER_OK == status) {
        uint32_t disposition;
        status = reply->GetArgs("u", &disposition);
        if (ER_OK == status) {
            switch (disposition) {
            case ALLJOYN_CANCELADVERTISENAME_REPLY_SUCCESS:
                break;

            case ALLJOYN_CANCELADVERTISENAME_REPLY_FAILED:
                status = ER_ALLJOYN_CANCELADVERTISENAME_REPLY_FAILED;
                break;

            default:
                status = ER_BUS_UNEXPECTED_DISPOSITION;
                break;
            }
        }
    } else {
        QCC_LogError(status, ("%s.CancelAdvertiseName returned ERROR_MESSAGE (error=%s)", org::alljoyn::Bus::InterfaceName, reply->GetErrorDescription().c_str()));
    }
    return status;
}

void BusAttachment::RegisterBusListener(BusListener& listener)
{
    busInternal->listenersLock.Lock(MUTEX_CONTEXT);
    // push front so that we can easily get an iterator pointing to the new element
    BusListener* pListener = &listener;
    Internal::ProtectedBusListener protectedListener(pListener);
    busInternal->listeners.insert(protectedListener);

    /* Let listener know which bus attachment it has been registered on */
    busInternal->listenersLock.Unlock(MUTEX_CONTEXT);
    (*protectedListener)->ListenerRegistered(this);
}

void BusAttachment::UnregisterBusListener(BusListener& listener)
{
    busInternal->listenersLock.Lock(MUTEX_CONTEXT);

    /* Look for listener on ListenerSet */
    Internal::ListenerSet::iterator it = busInternal->listeners.begin();
    while (it != busInternal->listeners.end()) {
        if (**it == &listener) {
            break;
        }
        ++it;
    }

    /* Wait for all refs to ProtectedBusListener to exit */
    while ((it != busInternal->listeners.end()) && (it->GetRefCount() > 1)) {
        Internal::ProtectedBusListener l = *it;
        busInternal->listenersLock.Unlock(MUTEX_CONTEXT);
        qcc::Sleep(5);
        busInternal->listenersLock.Lock(MUTEX_CONTEXT);
        it = busInternal->listeners.find(l);
    }

    /* Delete the listeners entry and call user's callback (unlocked) */
    if (it != busInternal->listeners.end()) {
        Internal::ProtectedBusListener l = *it;
        busInternal->listeners.erase(it);
        busInternal->listenersLock.Unlock(MUTEX_CONTEXT);
        (*l)->ListenerUnregistered();
    } else {
        busInternal->listenersLock.Unlock(MUTEX_CONTEXT);
    }
}

void BusAttachment::Internal::GetConnectedPeers(set<string>& names)
{
    for (size_t i = 0; i < ArraySize(sessions); i++) {
        sessionsLock[i].Lock();
        for (auto sessionMember: sessions[i]) {
            names.insert(sessionMember.second.otherParticipants.begin(), sessionMember.second.otherParticipants.end());
        }
        sessionsLock[i].Unlock();
    }
}

QStatus BusAttachment::SecureConnectionInternal(const char* name, bool forceAuth, bool async)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    if (!IsPeerSecurityEnabled()) {
        return ER_BUS_SECURITY_NOT_ENABLED;
    }
    LocalEndpoint localEndpoint = GetInternal().GetLocalEndpoint();
    if (!localEndpoint->IsValid()) {
        return ER_BUS_ENDPOINT_CLOSING;
    } else {
        AllJoynPeerObj* peerObj = localEndpoint->GetPeerObj();
        set<string> names;
        if (name) {
            names.insert(name);
        } else {
            GetInternal().GetConnectedPeers(names);
        }

        for (auto name: names) {
            const char* cname = name.c_str();
            if (forceAuth) {
                peerObj->ForceAuthentication(cname);
            }
            QStatus status = async ? peerObj->AuthenticatePeerAsync(cname) : peerObj->AuthenticatePeer(MESSAGE_METHOD_CALL, cname);
            if (status != ER_OK) {
                return status;
            }
        }
    }
    return ER_OK;
}

QStatus BusAttachment::SecureConnection(const char* name, bool forceAuth)
{
    return SecureConnectionInternal(name, forceAuth, false);
}

QStatus BusAttachment::SecureConnectionAsync(const char* name, bool forceAuth)
{
    return SecureConnectionInternal(name, forceAuth, true);
}

QStatus BusAttachment::NameHasOwner(const char* name, bool& hasOwner)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    Message reply(*this);
    MsgArg arg("s", name);
    QStatus status = this->GetDBusProxyObj().MethodCall(org::freedesktop::DBus::InterfaceName, "NameHasOwner", &arg, 1, reply);
    if (ER_OK == status) {
        status = reply->GetArgs("b", &hasOwner);
    } else {
        QCC_LogError(status, ("%s.NameHasOwner returned ERROR_MESSAGE (error=%s)", org::freedesktop::DBus::InterfaceName, reply->GetErrorDescription().c_str()));
    }
    return status;
}

QStatus BusAttachment::SetDaemonDebug(const char* module, uint32_t level)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    Message reply(*this);
    MsgArg args[2];
    size_t argsSize = ArraySize(args);
    MsgArg::Set(args, argsSize, "su", module, level);
    QStatus status = this->GetAllJoynDebugObj().MethodCall(org::alljoyn::Daemon::Debug::InterfaceName, "SetDebugLevel", args, argsSize, reply);
    if (status != ER_OK) {
        String errMsg;
        reply->GetErrorName(&errMsg);
        if (errMsg == "ER_BUS_NO_SUCH_OBJECT") {
            status = ER_BUS_NO_SUCH_OBJECT;
        }
    }
    return status;
}

QStatus BusAttachment::BindSessionPort(SessionPort& sessionPort, const SessionOpts& opts, SessionPortListener& listener)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    Message reply(*this);
    MsgArg args[2];

    args[0].Set("q", sessionPort);
    SetSessionOpts(opts, args[1]);

    QStatus status = this->GetAllJoynProxyObj().MethodCall(org::alljoyn::Bus::InterfaceName, "BindSessionPort", args, ArraySize(args), reply);
    if (status != ER_OK) {
        QCC_LogError(status, ("%s.BindSessionPort returned ERROR_MESSAGE (error=%s)", org::alljoyn::Bus::InterfaceName, reply->GetErrorDescription().c_str()));
    } else {
        SessionPort tempPort;
        uint32_t disposition;
        status = reply->GetArgs("uq", &disposition, &tempPort);
        if (status == ER_OK) {
            switch (disposition) {
            case ALLJOYN_BINDSESSIONPORT_REPLY_SUCCESS:
                sessionPort = tempPort;
                break;

            case ALLJOYN_BINDSESSIONPORT_REPLY_ALREADY_EXISTS:
                status = ER_ALLJOYN_BINDSESSIONPORT_REPLY_ALREADY_EXISTS;
                break;

            case ALLJOYN_BINDSESSIONPORT_REPLY_INVALID_OPTS:
                status = ER_ALLJOYN_BINDSESSIONPORT_REPLY_INVALID_OPTS;
                break;

            default:
            case ALLJOYN_BINDSESSIONPORT_REPLY_FAILED:
                status = ER_ALLJOYN_BINDSESSIONPORT_REPLY_FAILED;
                break;
            }
        }
        if (status == ER_OK) {
            busInternal->sessionPortListenersLock.Lock(MUTEX_CONTEXT);
            SessionPortListener* pListener = &listener;
            pair<SessionPort, Internal::ProtectedSessionPortListener> elem(sessionPort, Internal::ProtectedSessionPortListener(pListener));
            busInternal->sessionPortListeners.insert(elem);
            busInternal->sessionPortListenersLock.Unlock(MUTEX_CONTEXT);
        }
    }
    return status;
}

QStatus BusAttachment::UnbindSessionPort(SessionPort sessionPort)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    Message reply(*this);
    MsgArg args[1];

    args[0].Set("q", sessionPort);

    QStatus status = this->GetAllJoynProxyObj().MethodCall(org::alljoyn::Bus::InterfaceName, "UnbindSessionPort", args, ArraySize(args), reply);
    if (status != ER_OK) {
        QCC_LogError(status, ("%s.UnbindSessionPort returned ERROR_MESSAGE (error=%s)", org::alljoyn::Bus::InterfaceName, reply->GetErrorDescription().c_str()));
    } else {
        uint32_t disposition;
        status = reply->GetArgs("u", &disposition);
        if (status == ER_OK) {
            switch (disposition) {
            case ALLJOYN_UNBINDSESSIONPORT_REPLY_SUCCESS:
                status = ER_OK;
                break;

            case ALLJOYN_UNBINDSESSIONPORT_REPLY_BAD_PORT:
                status = ER_ALLJOYN_UNBINDSESSIONPORT_REPLY_BAD_PORT;
                break;

            case ALLJOYN_UNBINDSESSIONPORT_REPLY_FAILED:
            default:
                status = ER_ALLJOYN_UNBINDSESSIONPORT_REPLY_FAILED;
                break;
            }
        }
        if (status == ER_OK) {
            busInternal->sessionPortListenersLock.Lock(MUTEX_CONTEXT);
            Internal::SessionPortListenerMap::iterator it =
                busInternal->sessionPortListeners.find(sessionPort);

            if (it != busInternal->sessionPortListeners.end()) {
                while (it->second.GetRefCount() > 1) {
                    busInternal->sessionPortListenersLock.Unlock(MUTEX_CONTEXT);
                    qcc::Sleep(5);
                    busInternal->sessionPortListenersLock.Lock(MUTEX_CONTEXT);
                }
                busInternal->sessionPortListeners.erase(sessionPort);
            }
            busInternal->sessionPortListenersLock.Unlock(MUTEX_CONTEXT);
        }
    }
    return status;
}


bool BusAttachment::Internal::IsSessionPortBound(SessionPort sessionPort) {
    sessionPortListenersLock.Lock(MUTEX_CONTEXT);
    if (sessionPortListeners.find(sessionPort) != sessionPortListeners.end()) {
        sessionPortListenersLock.Unlock(MUTEX_CONTEXT);
        return true;
    }
    sessionPortListenersLock.Unlock(MUTEX_CONTEXT);
    return false;
}
QStatus BusAttachment::JoinSessionAsync(const char* sessionHost, SessionPort sessionPort, SessionListener* sessionListener,
                                        const SessionOpts& opts, BusAttachment::JoinSessionAsyncCB* callback, void* context)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }
    if (!IsLegalBusName(sessionHost)) {
        return ER_BUS_BAD_BUS_NAME;
    }

    MsgArg args[3];
    size_t numArgs = 2;

    MsgArg::Set(args, numArgs, "sq", sessionHost, sessionPort);
    SetSessionOpts(opts, args[2]);

    const ProxyBusObject& alljoynObj = this->GetAllJoynProxyObj();
    JoinSessionAsyncCBContext* cbCtx = new JoinSessionAsyncCBContext(callback, sessionListener, sessionHost, context);

    QStatus status = alljoynObj.MethodCallAsync(org::alljoyn::Bus::InterfaceName,
                                                "JoinSession",
                                                busInternal,
                                                static_cast<MessageReceiver::ReplyHandler>(&BusAttachment::Internal::JoinSessionAsyncCB),
                                                args,
                                                ArraySize(args),
                                                cbCtx,
                                                90000);
    if (status != ER_OK) {
        delete cbCtx;
    }
    return status;
}

void BusAttachment::Internal::JoinSessionAsyncCB(Message& reply, void* context)
{
    JoinSessionAsyncCBContext* ctx = reinterpret_cast<JoinSessionAsyncCBContext*>(context);

    QStatus status = ER_FAIL;
    SessionId sessionId = 0;
    SessionOpts opts;
    if (reply->GetType() == MESSAGE_METHOD_RET) {
        status = bus.GetJoinSessionResponse(reply, sessionId, opts);
    } else if (reply->GetType() == MESSAGE_ERROR) {
        status = ER_BUS_REPLY_IS_ERROR_MESSAGE;
        QCC_LogError(status, ("%s.JoinSession returned ERROR_MESSAGE (error=%s)", org::alljoyn::Bus::InterfaceName, reply->GetErrorDescription().c_str()));
    }
    if (status == ER_OK) {
        Session s;
        s.id = sessionId;
        s.host = false;
        s.multipoint = opts.isMultipoint;
        if (ctx->sessionListener) {
            s.listener = ProtectedSessionListener(ctx->sessionListener);
        }
        s.otherParticipants.insert(ctx->host);

        sessionsLock[SESSION_SIDE_JOINER].Lock(MUTEX_CONTEXT);
        sessions[SESSION_SIDE_JOINER][sessionId] = s;
        sessionsLock[SESSION_SIDE_JOINER].Unlock(MUTEX_CONTEXT);
    }

    /* Call the callback */
    ctx->callback->JoinSessionCB(status, sessionId, opts, ctx->context);
    delete ctx;
}

QStatus BusAttachment::GetJoinSessionResponse(Message& reply, SessionId& sessionId, SessionOpts& opts)
{
    QStatus status = ER_OK;
    const MsgArg* replyArgs;
    size_t na;
    reply->GetArgs(na, replyArgs);
    QCC_ASSERT(na == 3);
    uint32_t disposition = replyArgs[0].v_uint32;
    sessionId = replyArgs[1].v_uint32;
    status = GetSessionOpts(replyArgs[2], opts);
    if (status != ER_OK) {
        sessionId = 0;
    } else {
        switch (disposition) {
        case ALLJOYN_JOINSESSION_REPLY_SUCCESS:
            break;

        case ALLJOYN_JOINSESSION_REPLY_NO_SESSION:
            status = ER_ALLJOYN_JOINSESSION_REPLY_NO_SESSION;
            break;

        case ALLJOYN_JOINSESSION_REPLY_UNREACHABLE:
            status = ER_ALLJOYN_JOINSESSION_REPLY_UNREACHABLE;
            break;

        case ALLJOYN_JOINSESSION_REPLY_CONNECT_FAILED:
            status = ER_ALLJOYN_JOINSESSION_REPLY_CONNECT_FAILED;
            break;

        case ALLJOYN_JOINSESSION_REPLY_REJECTED:
            status = ER_ALLJOYN_JOINSESSION_REPLY_REJECTED;
            break;

        case ALLJOYN_JOINSESSION_REPLY_BAD_SESSION_OPTS:
            status = ER_ALLJOYN_JOINSESSION_REPLY_BAD_SESSION_OPTS;
            break;

        case ALLJOYN_JOINSESSION_REPLY_ALREADY_JOINED:
            status = ER_ALLJOYN_JOINSESSION_REPLY_ALREADY_JOINED;
            break;

        case ALLJOYN_JOINSESSION_REPLY_FAILED:
            status = ER_ALLJOYN_JOINSESSION_REPLY_FAILED;
            break;

        default:
            status = ER_BUS_UNEXPECTED_DISPOSITION;
            break;
        }
    }

    return status;
}

QStatus BusAttachment::JoinSession(const char* sessionHost, SessionPort sessionPort, SessionListener* listener, SessionId& sessionId, SessionOpts& opts)
{
    if (busInternal->localEndpoint->IsReentrantCall()) {
        return ER_BUS_BLOCKING_CALL_NOT_ALLOWED;
    }
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }
    if (!IsLegalBusName(sessionHost)) {
        return ER_BUS_BAD_BUS_NAME;
    }

    QCC_ASSERT(busInternal);
    return busInternal->JoinSession(sessionHost, sessionPort, listener, sessionId, opts);
}

QStatus BusAttachment::Internal::JoinSession(const char* sessionHost, SessionPort sessionPort, SessionListener* listener, SessionId& sessionId, SessionOpts& opts)
{
    /* Return early if BusAttachment is stopping */
    joinLock.Lock();
    if (bus.IsStopping()) {
        joinLock.Unlock();
        return ER_BUS_STOPPING;
    }

    /* Create JointSessionContext */
    Thread* thisThread = Thread::GetThread();
    joinThreads.insert(pair<Thread*, JoinContext>(thisThread, JoinContext()));
    joinLock.Unlock();

    /* Send JoinSessionAsync and block caller until it completes */
    QStatus status = bus.JoinSessionAsync(sessionHost, sessionPort, listener, opts, this, (void*) thisThread);

    if (status == ER_OK) {
        /* Wait for join to succeed or fail */
        status = Event::Wait(Event::neverSet);

        /* Clear alerted state */
        if (status == ER_ALERTED_THREAD) {
            thisThread->ResetAlertCode();
            thisThread->GetStopEvent().ResetEvent();
            status = ER_OK;
        }
    }
    /* Fetch context */
    joinLock.Lock();
    map<Thread*, JoinContext>::iterator it = joinThreads.find(thisThread);
    if (it != joinThreads.end()) {
        if (status == ER_OK) {
            /* Populate session details */
            if (thisThread->GetAlertCode() == 0) {
                status = it->second.status;
                if (status == ER_OK) {
                    sessionId = it->second.sessionId;
                    opts = it->second.opts;
                }
            } else {
                /* Alert came from BusAttachment::Stop */
                status = ER_BUS_STOPPING;
            }
        }
        /* Remove entry */
        joinThreads.erase(it);
    } else {
        /* JoinContext is missing */
        if (status == ER_OK) {
            status = ER_FAIL;
        }
    }
    joinLock.Unlock();
    return status;
}

void BusAttachment::Internal::JoinSessionCB(QStatus status, SessionId sessionId, const SessionOpts& opts, void* context)
{
    Thread* thread = reinterpret_cast<Thread*>(context);
    joinLock.Lock();
    map<Thread*, JoinContext>::iterator it = joinThreads.find(thread);
    if (it != joinThreads.end()) {
        it->second.status = status;
        if (status == ER_OK) {
            it->second.sessionId = sessionId;
            it->second.opts = opts;
        }
        it->first->Alert();
    }
    joinLock.Unlock();
}

void BusAttachment::ClearSession(SessionId sessionId, SessionSideMask bitset) {
    for (size_t side = 0; side < ArraySize(busInternal->sessions); ++side) {
        if (!(bitset & (1 << side))) {
            continue;
        }

        busInternal->sessionsLock[side].Lock(MUTEX_CONTEXT);
        Internal::SessionMap::iterator it = busInternal->sessions[side].find(sessionId);
        Internal::Session s;
        bool found = false;
        if (it != busInternal->sessions[side].end()) {
            found = true;
            s = it->second;
            busInternal->sessions[side].erase(it);
        }
        busInternal->sessionsLock[side].Unlock(MUTEX_CONTEXT);

        /* Wait for any outstanding callback to complete */
        while (found && s.listener.GetRefCount() > 1) {
            qcc::Sleep(4);
        }
    }
}

void BusAttachment::Internal::LeaveSessionAsyncCB(Message& reply, void* context)
{
    LeaveSessionAsyncCBContext* ctx = reinterpret_cast<LeaveSessionAsyncCBContext*>(context);

    QStatus status = ER_FAIL;
    if (reply->GetType() == MESSAGE_METHOD_RET) {
        uint32_t disposition;
        status = reply->GetArgs("u", &disposition);
        if (ER_OK == status) {
            switch (disposition) {
            case ALLJOYN_LEAVESESSION_REPLY_SUCCESS:
                break;

            case ALLJOYN_LEAVESESSION_REPLY_NO_SESSION:
                status = ER_ALLJOYN_LEAVESESSION_REPLY_NO_SESSION;
                break;

            case ALLJOYN_LEAVESESSION_REPLY_FAILED:
                status = ER_ALLJOYN_LEAVESESSION_REPLY_FAILED;
                break;

            default:
                status = ER_BUS_UNEXPECTED_DISPOSITION;
                break;
            }
        }
    } else if (reply->GetType() == MESSAGE_ERROR) {
        status = ER_BUS_REPLY_IS_ERROR_MESSAGE;
        QCC_LogError(status, ("%s.LeaveSession returned ERROR_MESSAGE (error=%s)", org::alljoyn::Bus::InterfaceName, reply->GetErrorDescription().c_str()));
    }

    /* Call the callback */
    ctx->callback->LeaveSessionCB(status, ctx->context);
    delete ctx;
}

QStatus BusAttachment::LeaveSessionAsync(const SessionId& sessionId, const char* method, SessionSideMask bitset, BusAttachment::LeaveSessionAsyncCB* callback, void* context)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    ClearSession(sessionId, bitset);

    MsgArg arg("u", sessionId);

    const ProxyBusObject& alljoynObj = this->GetAllJoynProxyObj();
    LeaveSessionAsyncCBContext* cbCtx = new LeaveSessionAsyncCBContext(callback, context);

    QStatus status = alljoynObj.MethodCallAsync(org::alljoyn::Bus::InterfaceName,
                                                method,
                                                busInternal,
                                                static_cast<MessageReceiver::ReplyHandler>(&BusAttachment::Internal::LeaveSessionAsyncCB),
                                                &arg,
                                                1,
                                                cbCtx);
    if (status != ER_OK) {
        delete cbCtx;
    }
    return status;
}

QStatus BusAttachment::LeaveSessionAsync(const SessionId& sessionId, BusAttachment::LeaveSessionAsyncCB* callback, void* context)
{
    return LeaveSessionAsync(sessionId, "LeaveSession", SESSION_SIDE_MASK_BOTH, callback, context);
}

QStatus BusAttachment::LeaveHostedSessionAsync(const SessionId& sessionId, BusAttachment::LeaveSessionAsyncCB* callback, void* context)
{
    return LeaveSessionAsync(sessionId, "LeaveHostedSession", SESSION_SIDE_MASK_HOST, callback, context);
}

QStatus BusAttachment::LeaveJoinedSessionAsync(const SessionId& sessionId, BusAttachment::LeaveSessionAsyncCB* callback, void* context)
{
    return LeaveSessionAsync(sessionId, "LeaveJoinedSession", SESSION_SIDE_MASK_JOINER, callback, context);
}

QStatus BusAttachment::LeaveSession(const SessionId& sessionId, const char*method, SessionSideMask bitset)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    ClearSession(sessionId, bitset);

    Message reply(*this);
    MsgArg arg("u", sessionId);
    const ProxyBusObject& alljoynObj = this->GetAllJoynProxyObj();
    QStatus status = alljoynObj.MethodCall(org::alljoyn::Bus::InterfaceName, method, &arg, 1, reply);
    if (ER_OK == status) {
        uint32_t disposition;
        status = reply->GetArgs("u", &disposition);
        if (ER_OK == status) {
            switch (disposition) {
            case ALLJOYN_LEAVESESSION_REPLY_SUCCESS:
                break;

            case ALLJOYN_LEAVESESSION_REPLY_NO_SESSION:
                status = ER_ALLJOYN_LEAVESESSION_REPLY_NO_SESSION;
                break;

            case ALLJOYN_LEAVESESSION_REPLY_FAILED:
                status = ER_ALLJOYN_LEAVESESSION_REPLY_FAILED;
                break;

            default:
                status = ER_BUS_UNEXPECTED_DISPOSITION;
                break;
            }
        }
    } else {
        QCC_LogError(status, ("%s.LeaveSession returned ERROR_MESSAGE (error=%s)", org::alljoyn::Bus::InterfaceName, reply->GetErrorDescription().c_str()));
    }

    return status;

}

QStatus BusAttachment::LeaveSession(const SessionId& sessionId)
{
    return LeaveSession(sessionId, "LeaveSession", SESSION_SIDE_MASK_BOTH);
}

QStatus BusAttachment::LeaveHostedSession(const SessionId& sessionId)
{
    return LeaveSession(sessionId, "LeaveHostedSession", SESSION_SIDE_MASK_HOST);
}

QStatus BusAttachment::LeaveJoinedSession(const SessionId& sessionId)
{
    return LeaveSession(sessionId, "LeaveJoinedSession", SESSION_SIDE_MASK_JOINER);
}

QStatus BusAttachment::RemoveSessionMember(SessionId sessionId, String memberName)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    Message reply(*this);
    MsgArg args[2];
    size_t numArgs = ArraySize(args);

    MsgArg::Set(args, numArgs, "us", sessionId, memberName.c_str());

    const ProxyBusObject& alljoynObj = this->GetAllJoynProxyObj();
    QStatus status = alljoynObj.MethodCall(org::alljoyn::Bus::InterfaceName, "RemoveSessionMember", args, numArgs, reply);
    if (ER_OK == status) {
        uint32_t disposition;
        status = reply->GetArgs("u", &disposition);
        if (ER_OK == status) {
            switch (disposition) {

            case ALLJOYN_REMOVESESSIONMEMBER_REPLY_SUCCESS:
                break;

            case ALLJOYN_REMOVESESSIONMEMBER_REPLY_NO_SESSION:
                status = ER_ALLJOYN_REMOVESESSIONMEMBER_REPLY_NO_SESSION;
                break;

            case ALLJOYN_REMOVESESSIONMEMBER_REPLY_NOT_BINDER:
                status = ER_ALLJOYN_REMOVESESSIONMEMBER_NOT_BINDER;
                break;

            case ALLJOYN_REMOVESESSIONMEMBER_REPLY_NOT_MULTIPOINT:
                status = ER_ALLJOYN_REMOVESESSIONMEMBER_NOT_MULTIPOINT;
                break;

            case ALLJOYN_REMOVESESSIONMEMBER_REPLY_NOT_FOUND:
                status = ER_ALLJOYN_REMOVESESSIONMEMBER_NOT_FOUND;
                break;

            case ALLJOYN_REMOVESESSIONMEMBER_REPLY_INCOMPATIBLE_REMOTE_DAEMON:
                status = ER_ALLJOYN_REMOVESESSIONMEMBER_INCOMPATIBLE_REMOTE_DAEMON;
                break;

            case ALLJOYN_REMOVESESSIONMEMBER_REPLY_FAILED:
                status = ER_ALLJOYN_REMOVESESSIONMEMBER_REPLY_FAILED;
                break;

            default:
                status = ER_BUS_UNEXPECTED_DISPOSITION;
                break;
            }
        }
    } else {
        QCC_LogError(status, ("%s.RemoveSessionMember returned ERROR_MESSAGE (error=%s)", org::alljoyn::Bus::InterfaceName, reply->GetErrorDescription().c_str()));
    }

    return status;
}

QStatus BusAttachment::GetSessionFd(SessionId sessionId, SocketFd& sockFd)
{
    QCC_DbgTrace(("BusAttachment::GetSessionFd sessionId:%d", sessionId));
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    sockFd = qcc::INVALID_SOCKET_FD;

    Message reply(*this);
    MsgArg arg("u", sessionId);
    const ProxyBusObject& alljoynObj = this->GetAllJoynProxyObj();
    QStatus status = alljoynObj.MethodCall(org::alljoyn::Bus::InterfaceName, "GetSessionFd", &arg, 1, reply);
    if (ER_OK == status) {
        status = reply->GetArgs("h", &sockFd);
        if (status == ER_OK) {
            status = qcc::SocketDup(sockFd, sockFd);
            if (status == ER_OK) {
                status = qcc::SetBlocking(sockFd, false);
                if (status != ER_OK) {
                    qcc::Close(sockFd);
                }
            }
        }
    } else {
        QCC_LogError(status, ("%s.GetSessionFd returned ERROR_MESSAGE (error=%s)", org::alljoyn::Bus::InterfaceName, reply->GetErrorDescription().c_str()));
    }
    return status;
}

QStatus BusAttachment::SetLinkTimeoutAsync(SessionId sessionid, uint32_t linkTimeout, BusAttachment::SetLinkTimeoutAsyncCB* callback, void* context)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    MsgArg args[2];
    args[0].Set("u", sessionid);
    args[1].Set("u", linkTimeout);

    const ProxyBusObject& alljoynObj = this->GetAllJoynProxyObj();
    SetLinkTimeoutAsyncCBContext* cbCtx = new SetLinkTimeoutAsyncCBContext(callback, context);
    QStatus status = alljoynObj.MethodCallAsync(
        org::alljoyn::Bus::InterfaceName,
        "SetLinkTimeout",
        busInternal,
        static_cast<MessageReceiver::ReplyHandler>(&BusAttachment::Internal::SetLinkTimeoutAsyncCB),
        args,
        ArraySize(args),
        cbCtx,
        90000);
    if (status != ER_OK) {
        delete cbCtx;
    }
    return status;
}

void BusAttachment::Internal::SetLinkTimeoutAsyncCB(Message& reply, void* context)
{
    SetLinkTimeoutAsyncCBContext* ctx = static_cast<SetLinkTimeoutAsyncCBContext*>(context);
    uint32_t timeout = 0;

    QStatus status = ER_OK;
    if (reply->GetType() == MESSAGE_METHOD_RET) {
        status = bus.GetLinkTimeoutResponse(reply, timeout);
    } else if (reply->GetType() == MESSAGE_ERROR) {
        status = ER_BUS_REPLY_IS_ERROR_MESSAGE;
        QCC_LogError(status, ("%s.SetLinkTimeout returned ERROR_MESSAGE (error=%s)", org::alljoyn::Bus::InterfaceName, reply->GetErrorDescription().c_str()));
    }

    /* Call the user's callback */
    ctx->callback->SetLinkTimeoutCB(status, timeout, ctx->context);
    delete ctx;
}

QStatus BusAttachment::GetLinkTimeoutResponse(Message& reply, uint32_t& timeout)
{
    QStatus status = ER_OK;
    const MsgArg* replyArgs;
    size_t na;
    reply->GetArgs(na, replyArgs);
    QCC_ASSERT(na == 2);

    switch (replyArgs[0].v_uint32) {
    case ALLJOYN_SETLINKTIMEOUT_REPLY_SUCCESS:
        timeout = replyArgs[1].v_uint32;
        break;

    case ALLJOYN_SETLINKTIMEOUT_REPLY_NO_DEST_SUPPORT:
        status = ER_ALLJOYN_SETLINKTIMEOUT_REPLY_NO_DEST_SUPPORT;
        break;

    case ALLJOYN_SETLINKTIMEOUT_REPLY_NO_SESSION:
        status = ER_BUS_NO_SESSION;
        break;

    default:
    case ALLJOYN_SETLINKTIMEOUT_REPLY_FAILED:
        status = ER_ALLJOYN_SETLINKTIMEOUT_REPLY_FAILED;
        break;
    }

    return status;
}

QStatus BusAttachment::SetLinkTimeout(SessionId sessionId, uint32_t& linkTimeout)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    Message reply(*this);
    MsgArg args[2];

    args[0].Set("u", sessionId);
    args[1].Set("u", linkTimeout);

    QStatus status = this->GetAllJoynProxyObj().MethodCall(org::alljoyn::Bus::InterfaceName, "SetLinkTimeout", args, ArraySize(args), reply);

    if (status == ER_OK) {
        status = GetLinkTimeoutResponse(reply, linkTimeout);
    } else {
        QCC_LogError(status, ("%s.SetLinkTimeout returned ERROR_MESSAGE (error=%s)", org::alljoyn::Bus::InterfaceName, reply->GetErrorDescription().c_str()));
        status = ER_ALLJOYN_SETLINKTIMEOUT_REPLY_NOT_SUPPORTED;
    }

    return status;
}

void BusAttachment::Internal::NonLocalEndpointDisconnected()
{
    bus.UnregisterSignalHandlers();

    listenersLock.Lock(MUTEX_CONTEXT);
    ListenerSet::iterator it = listeners.begin();
    while (it != listeners.end()) {
        ProtectedBusListener l = *it;
        listenersLock.Unlock(MUTEX_CONTEXT);
        (*l)->BusDisconnected();
        listenersLock.Lock(MUTEX_CONTEXT);
        it = listeners.upper_bound(l);
    }
    listenersLock.Unlock(MUTEX_CONTEXT);
}

void BusAttachment::EnableConcurrentCallbacks()
{
    busInternal->localEndpoint->EnableReentrancy();
}

void BusAttachment::Internal::AllJoynSignalHandler(const InterfaceDescription::Member* member,
                                                   const char* srcPath,
                                                   Message& msg)
{
    QCC_UNUSED(member);
    QCC_UNUSED(srcPath);
    /* Dispatch thread for BusListener callbacks */
    size_t numArgs;
    const MsgArg* args;
    msg->GetArgs(numArgs, args);

    if (msg->GetType() == MESSAGE_SIGNAL) {
        if (0 == strcmp("Announce", msg->GetMemberName())) {
            if (numArgs == 4) {
#if !defined(NDEBUG)
                for (int i = 0; i < 4; i++) {
                    QCC_DbgPrintf(("args[%d]=%s", i, args[i].ToString().c_str()));
                }
#endif
                /* Call aboutListener */
                aboutListenersLock.Lock(MUTEX_CONTEXT);
                AboutListenerSet::iterator it = aboutListeners.begin();
                while (it != aboutListeners.end()) {
                    ProtectedAboutListener listener = *it;
                    aboutListenersLock.Unlock(MUTEX_CONTEXT);
                    (*listener)->Announced(msg->GetSender(), args[0].v_uint16, static_cast<SessionPort>(args[1].v_uint16), args[2], args[3]);
                    aboutListenersLock.Lock(MUTEX_CONTEXT);
                    it = aboutListeners.upper_bound(listener);
                }
                aboutListenersLock.Unlock(MUTEX_CONTEXT);
            }
        } else if (0 == strcmp("FoundAdvertisedName", msg->GetMemberName())) {
            listenersLock.Lock(MUTEX_CONTEXT);
            ListenerSet::iterator it = listeners.begin();
            while (it != listeners.end()) {
                ProtectedBusListener pl = *it;
                listenersLock.Unlock(MUTEX_CONTEXT);
                (*pl)->FoundAdvertisedName(args[0].v_string.str, args[1].v_uint16, args[2].v_string.str);
                listenersLock.Lock(MUTEX_CONTEXT);
                it = listeners.upper_bound(pl);
            }
            listenersLock.Unlock(MUTEX_CONTEXT);
        } else if (0 == strcmp("LostAdvertisedName", msg->GetMemberName())) {
            listenersLock.Lock(MUTEX_CONTEXT);
            ListenerSet::iterator it = listeners.begin();
            while (it != listeners.end()) {
                ProtectedBusListener pl = *it;
                listenersLock.Unlock(MUTEX_CONTEXT);
                (*pl)->LostAdvertisedName(args[0].v_string.str, args[1].v_uint16, args[2].v_string.str);
                listenersLock.Lock(MUTEX_CONTEXT);
                it = listeners.upper_bound(pl);
            }
            listenersLock.Unlock(MUTEX_CONTEXT);
        } else if (0 == strcmp("SessionLostWithReasonAndDisposition", msg->GetMemberName())) {
            SessionId id = static_cast<SessionId>(args[0].v_uint32);
            SessionListener::SessionLostReason reason = static_cast<SessionListener::SessionLostReason>(args[1].v_uint32);
            unsigned int disposition = static_cast<unsigned int>(args[2].v_uint32);

            if (disposition < ArraySize(sessions)) {
                sessionsLock[disposition].Lock(MUTEX_CONTEXT);
                SessionMap::iterator it = sessions[disposition].find(id);
                Session s;
                bool found = false;
                if (it != sessions[disposition].end()) {
                    found = true;
                    s = it->second;
                    sessions[disposition].erase(it);
                }
                sessionsLock[disposition].Unlock(MUTEX_CONTEXT);

                if (found && *(s.listener)) {
                    /* call SessionLost on session listener */
                    (*(s.listener))->SessionLost(id, reason);
                }
            }
        } else if (0 == strcmp("NameOwnerChanged", msg->GetMemberName())) {
            listenersLock.Lock(MUTEX_CONTEXT);
            ListenerSet::iterator it = listeners.begin();
            while (it != listeners.end()) {
                ProtectedBusListener pl = *it;
                listenersLock.Unlock(MUTEX_CONTEXT);
                (*pl)->NameOwnerChanged(args[0].v_string.str,
                                        (0 < args[1].v_string.len) ? args[1].v_string.str : NULL,
                                        (0 < args[2].v_string.len) ? args[2].v_string.str : NULL);
                listenersLock.Lock(MUTEX_CONTEXT);
                it = listeners.upper_bound(pl);
            }
            listenersLock.Unlock(MUTEX_CONTEXT);
        } else if (0 == strcmp("MPSessionChangedWithReason", msg->GetMemberName())) {
            SessionId id = static_cast<SessionId>(args[0].v_uint32);
            unsigned int reason = args[3].v_uint32;
            const char* memberStr = args[1].v_string.str;

            for (size_t side = 0; side < ArraySize(sessions); ++side) {
                sessionsLock[side].Lock(MUTEX_CONTEXT);
                SessionMap::iterator it = sessions[side].find(id);

                if (it != sessions[side].end()) {
                    /* Getting this signal is the most sure-fire way of knowing a session is indeed multipoint, so
                     * we take the opportunity to set the appropriate flag in the Session structure */
                    it->second.multipoint = true;

                    /* now figure out if we have to invoke callbacks and update participant lists in the Session structure */
                    bool doCall = false;
                    if (args[2].v_bool) {
                        /* Member added. Special logic here because
                         * - as a host in a multipoint session you don't want to see members you already saw before. This extra logic is needed
                         *   in case of self-join. The exception here is the other side of the self-join */
                        if (side == SESSION_SIDE_JOINER ||
                            (side == SESSION_SIDE_HOST &&
                             (strcmp(memberStr, bus.GetUniqueName().c_str()) == 0 || reason == ALLJOYN_MPSESSIONCHANGED_REMOTE_MEMBER_ADDED))) {
                            doCall = true;
                            it->second.otherParticipants.insert(memberStr);
                        }
                    } else {
                        /* Member removed. More special logic here because
                           - As a host, you are not interested if this leaf node as also removed in a self-join session
                           - As a joiner, you are not interested if you were removed. */
                        if ((side == SESSION_SIDE_HOST && reason == ALLJOYN_MPSESSIONCHANGED_REMOTE_MEMBER_REMOVED) ||
                            (side == SESSION_SIDE_JOINER && !(reason == ALLJOYN_MPSESSIONCHANGED_LOCAL_MEMBER_REMOVED && strcmp(memberStr, bus.GetUniqueName().c_str()) == 0))) {
                            doCall = true;
                            it->second.otherParticipants.erase(memberStr);
                        }
                    }

                    ProtectedSessionListener pl = it->second.listener;
                    sessionsLock[side].Unlock(MUTEX_CONTEXT);
                    if (*pl && doCall) {
                        if (args[2].v_bool) {
                            (*pl)->SessionMemberAdded(id, memberStr);
                        } else {
                            (*pl)->SessionMemberRemoved(id, memberStr);
                        }
                    }
                } else {
                    sessionsLock[side].Unlock(MUTEX_CONTEXT);
                }
            }
        } else if (0 == strcmp("State", msg->GetMemberName())) {
            if (numArgs == 2) {
#if !defined(NDEBUG)
                for (int i = 0; i < 2; i++) {
                    QCC_DbgPrintf(("args[%d]=%s", i, args[i].ToString().c_str()));
                }
#endif
                /* Call applicationStateListener */
                applicationStateListenersLock.Lock(MUTEX_CONTEXT);
                if (!applicationStateListeners.empty()) {
                    qcc::KeyInfoNISTP256 keyInfo;
                    QStatus status = KeyInfoHelper::MsgArgToKeyInfoNISTP256PubKey(args[0], keyInfo);
                    if (ER_OK == status) {
                        PermissionConfigurator::ApplicationState applicationState = (PermissionConfigurator::ApplicationState) args[1].v_uint16;
                        if (applicationState <= PermissionConfigurator::NEED_UPDATE) {
                            ApplicationStateListenerSet::iterator it = applicationStateListeners.begin();
                            while (it != applicationStateListeners.end()) {
                                ProtectedApplicationStateListener listener = *it;
                                applicationStateListenersLock.Unlock(MUTEX_CONTEXT);
                                (*listener)->State(msg->GetSender(), keyInfo, applicationState);
                                applicationStateListenersLock.Lock(MUTEX_CONTEXT);
                                it = applicationStateListeners.upper_bound(listener);
                            }
                        }
                    }
                }
                applicationStateListenersLock.Unlock(MUTEX_CONTEXT);
            }
        } else {
            QCC_DbgPrintf(("Unrecognized signal \"%s.%s\" received", msg->GetInterface(), msg->GetMemberName()));
        }
    }
}

uint32_t AJ_CALL BusAttachment::GetTimestamp()
{
    return qcc::GetTimestamp();
}

void BusAttachment::RegisterAboutListener(AboutListener& aboutListener)
{
    busInternal->aboutListenersLock.Lock(MUTEX_CONTEXT);
    AboutListener* pListener = &aboutListener;
    Internal::ProtectedAboutListener protectedListener(pListener);
    busInternal->aboutListeners.insert(pListener);
    busInternal->aboutListenersLock.Unlock(MUTEX_CONTEXT);
}

void BusAttachment::UnregisterAboutListener(AboutListener& aboutListener)
{
    busInternal->aboutListenersLock.Lock(MUTEX_CONTEXT);

    /* Look for listener on ListenerSet */
    Internal::AboutListenerSet::iterator it = busInternal->aboutListeners.begin();
    while (it != busInternal->aboutListeners.end()) {
        if (**it == &aboutListener) {
            break;
        }
        ++it;
    }

    /* Wait for all refs to ProtectedAboutListener to exit */
    while ((it != busInternal->aboutListeners.end()) && (it->GetRefCount() > 1)) {
        Internal::ProtectedAboutListener l = *it;
        busInternal->aboutListenersLock.Unlock(MUTEX_CONTEXT);
        qcc::Sleep(5);
        busInternal->aboutListenersLock.Lock(MUTEX_CONTEXT);
        it = busInternal->aboutListeners.find(l);
    }

    /* Delete the listeners entry and call user's callback (unlocked) */
    if (it != busInternal->aboutListeners.end()) {
        Internal::ProtectedAboutListener l = *it;
        busInternal->aboutListeners.erase(it);
    }
    busInternal->aboutListenersLock.Unlock(MUTEX_CONTEXT);
}

void BusAttachment::UnregisterAllAboutListeners()
{
    busInternal->aboutListenersLock.Lock(MUTEX_CONTEXT);

    /* Look for listener on ListenerSet */
    Internal::AboutListenerSet::iterator it = busInternal->aboutListeners.begin();
    while (it != busInternal->aboutListeners.end()) {
        /* Wait for all refs to ProtectedAboutListener to exit */
        while ((it != busInternal->aboutListeners.end()) && (it->GetRefCount() > 1)) {
            Internal::ProtectedAboutListener l = *it;
            busInternal->aboutListenersLock.Unlock(MUTEX_CONTEXT);
            qcc::Sleep(5);
            busInternal->aboutListenersLock.Lock(MUTEX_CONTEXT);
            it = busInternal->aboutListeners.find(l);
        }

        /* Delete the listeners entry and call user's callback (unlocked) */
        if (it != busInternal->aboutListeners.end()) {
            Internal::ProtectedAboutListener l = *it;
            busInternal->aboutListeners.erase(it);
        }
        it = busInternal->aboutListeners.begin();
    }
    busInternal->aboutListenersLock.Unlock(MUTEX_CONTEXT);
}

QStatus BusAttachment::WhoImplements(const char** implementsInterfaces, size_t numberInterfaces)
{
    std::set<qcc::String> interfaces;
    for (size_t i = 0; i < numberInterfaces; ++i) {
        interfaces.insert(implementsInterfaces[i]);
    }

    qcc::String matchRule = "type='signal',interface='org.alljoyn.About',member='Announce',sessionless='t'";
    for (std::set<qcc::String>::iterator it = interfaces.begin(); it != interfaces.end(); ++it) {
        matchRule += qcc::String(",implements='") + *it + qcc::String("'");
    }

    QCC_DbgTrace(("Calling AddMatch(\"%s\")", matchRule.c_str()));
    return AddMatch(matchRule.c_str());
}

QStatus BusAttachment::WhoImplementsNonBlocking(const char** implementsInterfaces, size_t numberInterfaces)
{
    std::set<qcc::String> interfaces;
    for (size_t i = 0; i < numberInterfaces; ++i) {
        interfaces.insert(implementsInterfaces[i]);
    }

    qcc::String matchRule = "type='signal',interface='org.alljoyn.About',member='Announce',sessionless='t'";
    for (std::set<qcc::String>::iterator it = interfaces.begin(); it != interfaces.end(); ++it) {
        matchRule += qcc::String(",implements='") + *it + qcc::String("'");
    }

    QCC_DbgTrace(("Calling AddMatch(\"%s\")", matchRule.c_str()));
    return AddMatchNonBlocking(matchRule.c_str());
}

QStatus BusAttachment::WhoImplements(const char* iface)
{
    if (iface == NULL) {
        return WhoImplements(NULL, 0);
    }
    const char** tmp = &iface;
    return WhoImplements(tmp, 1);
}

QStatus BusAttachment::WhoImplementsNonBlocking(const char* iface)
{
    if (iface == NULL) {
        return WhoImplementsNonBlocking(NULL, 0);
    }
    const char** tmp = &iface;
    return WhoImplementsNonBlocking(tmp, 1);
}

QStatus BusAttachment::CancelWhoImplements(const char** implementsInterfaces, size_t numberInterfaces)
{
    std::set<qcc::String> interfaces;
    for (size_t i = 0; i < numberInterfaces; ++i) {
        interfaces.insert(implementsInterfaces[i]);
    }

    qcc::String matchRule = "type='signal',interface='org.alljoyn.About',member='Announce',sessionless='t'";
    for (std::set<qcc::String>::iterator it = interfaces.begin(); it != interfaces.end(); ++it) {
        matchRule += qcc::String(",implements='") + *it + qcc::String("'");
    }

    QCC_DbgTrace(("Calling RemoveMatch(\"%s\")", matchRule.c_str()));
    return RemoveMatch(matchRule.c_str());
}

QStatus BusAttachment::CancelWhoImplementsNonBlocking(const char** implementsInterfaces, size_t numberInterfaces)
{
    std::set<qcc::String> interfaces;
    for (size_t i = 0; i < numberInterfaces; ++i) {
        interfaces.insert(implementsInterfaces[i]);
    }

    qcc::String matchRule = "type='signal',interface='org.alljoyn.About',member='Announce',sessionless='t'";
    for (std::set<qcc::String>::iterator it = interfaces.begin(); it != interfaces.end(); ++it) {
        matchRule += qcc::String(",implements='") + *it + qcc::String("'");
    }

    QCC_DbgTrace(("Calling RemoveMatch(\"%s\")", matchRule.c_str()));
    return RemoveMatchNonBlocking(matchRule.c_str());
}

QStatus BusAttachment::CancelWhoImplements(const char* iface)
{
    if (iface == NULL) {
        return CancelWhoImplements(NULL, 0);
    }
    const char** tmp = &iface;
    return CancelWhoImplements(tmp, 1);
}

QStatus BusAttachment::RegisterApplicationStateListener(ApplicationStateListener& applicationStateListener)
{
    QStatus status = ER_OK;
#if defined(__GNUC__) && (__GNUC__ >= 6)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress"
#pragma GCC diagnostic ignored "-Wnonnull-compare"
#endif
    if (nullptr == &applicationStateListener) {
        status = ER_INVALID_ADDRESS;
    }
#if defined(__GNUC__) && (__GNUC__ >= 6)
#pragma GCC diagnostic pop
#pragma GCC diagnostic pop
#endif
    if (ER_OK == status) {
        status = busInternal->AddApplicationStateListener(applicationStateListener);
    }

    if (ER_OK == status) {
        status = AddMatch(BusAttachment::Internal::STATE_MATCH_RULE);
        QCC_ASSERT(ER_OK == status);
    }

    return status;
}

QStatus BusAttachment::UnregisterApplicationStateListener(ApplicationStateListener& applicationStateListener)
{
    QStatus status = ER_OK;
#if defined(__GNUC__) && (__GNUC__ >= 6)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress"
#pragma GCC diagnostic ignored "-Wnonnull-compare"
#endif
    if (nullptr == &applicationStateListener) {
        status = ER_INVALID_ADDRESS;
    }
#if defined(__GNUC__) && (__GNUC__ >= 6)
#pragma GCC diagnostic pop
#pragma GCC diagnostic pop
#endif
    if (ER_OK == status) {
        status = busInternal->RemoveApplicationStateListener(applicationStateListener);
    }

    if (ER_OK == status) {
        status = RemoveMatch(BusAttachment::Internal::STATE_MATCH_RULE);
        QCC_ASSERT(ER_OK == status);
    }

    return status;
}

QStatus BusAttachment::CancelWhoImplementsNonBlocking(const char* iface)
{
    if (iface == NULL) {
        return CancelWhoImplementsNonBlocking(NULL, 0);
    }
    const char** tmp = &iface;
    return CancelWhoImplementsNonBlocking(tmp, 1);
}

QStatus BusAttachment::Internal::GetAnnouncedObjectDescription(MsgArg& objectDescriptionArg) {
    return localEndpoint->GetAnnouncedObjectDescription(objectDescriptionArg);
}

QStatus BusAttachment::SetSessionListener(SessionId sessionId, SessionListener* listener)
{
    return busInternal->SetSessionListener(sessionId, listener, SESSION_SIDE_MASK_BOTH);
}

QStatus BusAttachment::SetJoinedSessionListener(SessionId sessionId, SessionListener* listener) {

    return busInternal->SetSessionListener(sessionId, listener, SESSION_SIDE_MASK_JOINER);

}

QStatus BusAttachment::SetHostedSessionListener(SessionId sessionId, SessionListener* listener) {

    return busInternal->SetSessionListener(sessionId, listener, SESSION_SIDE_MASK_HOST);

}

QStatus BusAttachment::CreateInterfacesFromXml(const char* xml)
{
    StringSource source(xml);

    /* Parse the XML to update this ProxyBusObject instance (plus any new children and interfaces) */
    XmlParseContext pc(source);
    QStatus status = XmlElement::Parse(pc);
    if (status == ER_OK) {
        XmlHelper xmlHelper(this, "BusAttachment");
        status = xmlHelper.AddInterfaceDefinitions(pc.GetRoot());
    }
    return status;
}

bool BusAttachment::Internal::CallAcceptListeners(SessionPort sessionPort, const char* joiner, const SessionOpts& opts)
{
    bool isAccepted = false;

    /* Call sessionPortListener */
    sessionPortListenersLock.Lock(MUTEX_CONTEXT);
    Internal::SessionPortListenerMap::iterator it = sessionPortListeners.find(sessionPort);
    if (it != sessionPortListeners.end()) {
        ProtectedSessionPortListener listener = it->second;
        sessionPortListenersLock.Unlock(MUTEX_CONTEXT);
        isAccepted = (*listener)->AcceptSessionJoiner(sessionPort, joiner, opts);
    } else {
        sessionPortListenersLock.Unlock(MUTEX_CONTEXT);
        QCC_LogError(ER_FAIL, ("Unable to find sessionPortListener for port=%d", sessionPort));
    }
    return isAccepted;
}

void BusAttachment::Internal::CallJoinedListeners(SessionPort sessionPort, SessionId sessionId, const char* joiner)
{
    /* do session bookkeeping */
    sessionsLock[SESSION_SIDE_HOST].Lock(MUTEX_CONTEXT);
    Session s;
    SessionMap::iterator sit = sessions[SESSION_SIDE_HOST].find(sessionId);
    if (sit != sessions[SESSION_SIDE_HOST].end()) {
        /* existing multipoint session */
        s = sit->second;
    } else {
        /* new point-to-point or multipoint session */
        s.id = sessionId;
        s.host = true;
        s.multipoint = false; /* if it's a multipoint session, the SessionMemberAdded code will patch this up for us */
    }
    s.otherParticipants.insert(joiner);
    sessions[SESSION_SIDE_HOST][sessionId] = s;
    sessionsLock[SESSION_SIDE_HOST].Unlock(MUTEX_CONTEXT);

    /* Call sessionPortListener */
    sessionPortListenersLock.Lock(MUTEX_CONTEXT);
    SessionPortListenerMap::iterator it = sessionPortListeners.find(sessionPort);
    if (it != sessionPortListeners.end()) {
        /* Notify user */
        ProtectedSessionPortListener cur = it->second;
        sessionPortListenersLock.Unlock(MUTEX_CONTEXT);
        (*cur)->SessionJoined(sessionPort, sessionId, joiner);
    } else {
        sessionPortListenersLock.Unlock(MUTEX_CONTEXT);
        QCC_LogError(ER_FAIL, ("Unable to find sessionPortListener for port=%d", sessionPort));
    }
}

QStatus BusAttachment::Internal::SetSessionListener(SessionId id, SessionListener* listener, SessionSideMask bitset)
{
    size_t fail = 0;
    size_t count = 0;

    /* Ambiguous */
    if (bitset == SESSION_SIDE_MASK_BOTH && IsSelfJoin(id) == true) {
        return ER_FAIL;
    }

    for (size_t side = 0; side < ArraySize(sessions); ++side) {
        int mask = 1 << side;
        if (bitset & mask) {
            sessionsLock[side].Lock(MUTEX_CONTEXT);
            SessionMap::iterator it = sessions[side].find(id);
            if (it != sessions[side].end()) {
                it->second.listener = ProtectedSessionListener(listener);
            } else {
                ++fail;
            }
            sessionsLock[side].Unlock(MUTEX_CONTEXT);
            ++count;
        }
    }

    if (fail == count) { /* take a relaxed approach: only fail if we could not find the session at least once */
        return ER_BUS_NO_SESSION;
    }
    return ER_OK;
}

bool BusAttachment::Internal::SessionExists(SessionId id, size_t index) const {

    sessionsLock[index].Lock(MUTEX_CONTEXT);
    bool found = (sessions[index].find(id) != sessions[index].end());
    sessionsLock[index].Unlock(MUTEX_CONTEXT);
    return found;

}

bool BusAttachment::Internal::IsSelfJoin(SessionId id) const {

    return SessionExists(id, SESSION_SIDE_HOST) && SessionExists(id, SESSION_SIDE_JOINER);

}

QStatus BusAttachment::GetPeerGUID(const char* name, qcc::String& guid)
{
    PeerStateTable* peerTable = busInternal->GetPeerStateTable();
    qcc::String peerName;
    if (name && *name) {
        peerName = name;
    } else {
        peerName = GetUniqueName();
    }
    if (peerTable->IsKnownPeer(peerName)) {
        guid = peerTable->GetPeerState(peerName)->GetGuid().ToString();
        return ER_OK;
    } else {
        return ER_BUS_NO_PEER_GUID;
    }
}

QStatus BusAttachment::ReloadKeyStore()
{
    return busInternal->keyStore.Reload();
}

QStatus BusAttachment::ClearKeys(const qcc::String& guid)
{
    if (!qcc::GUID128::IsGUID(guid)) {
        return ER_INVALID_GUID;
    } else {
        qcc::GUID128 g(guid);
        KeyStore::Key key(KeyStore::Key::REMOTE, g);
        if (busInternal->keyStore.HasKey(key)) {
            return busInternal->keyStore.DelKey(key);
        } else {
            return ER_BUS_KEY_UNAVAILABLE;
        }
    }
}

QStatus BusAttachment::SetKeyExpiration(const qcc::String& guid, uint32_t timeout)
{
    if (timeout == 0) {
        return ClearKeys(guid);
    }
    if (!qcc::GUID128::IsGUID(guid)) {
        return ER_INVALID_GUID;
    } else {
        qcc::GUID128 g(guid);
        KeyStore::Key key(KeyStore::Key::REMOTE, g);
        Timespec<EpochTime> expiration(GetEpochTimestamp() + (1000ull * timeout));
        return busInternal->keyStore.SetKeyExpiration(key, expiration);
    }
}

QStatus BusAttachment::GetKeyExpiration(const qcc::String& guid, uint32_t& timeout)
{
    if (!qcc::GUID128::IsGUID(guid)) {
        return ER_INVALID_GUID;
    } else {
        qcc::GUID128 g(guid);
        KeyStore::Key key(KeyStore::Key::REMOTE, g);
        Timespec<EpochTime> expiration;
        QStatus status = busInternal->keyStore.GetKeyExpiration(key, expiration);
        if (status == ER_OK) {
            Timespec<EpochTime> now(GetEpochTimestamp());
            int64_t deltaMillis = expiration - now;
            if (deltaMillis < 0) {
                timeout = 0;
            } else if (deltaMillis > (0xFFFFFFFFll * 1000)) {
                timeout = 0xFFFFFFFF;
            } else {
                timeout = (uint32_t)((deltaMillis + 500ull) / 1000ull);
            }
        }
        return status;
    }
}

QStatus BusAttachment::OnAppSuspend()
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    Message reply(*this);

    const ProxyBusObject& alljoynObj = this->GetAllJoynProxyObj();
    QStatus status = alljoynObj.MethodCall(org::alljoyn::Bus::InterfaceName, "OnAppSuspend", NULL, 0, reply);
    if (ER_OK == status) {
        uint32_t disposition;
        status = reply->GetArgs("u", &disposition);
        if (ER_OK == status) {
            switch (disposition) {
            case ALLJOYN_ONAPPSUSPEND_REPLY_SUCCESS:
                break;

            case ALLJOYN_ONAPPSUSPEND_REPLY_FAILED:
                status = ER_ALLJOYN_ONAPPSUSPEND_REPLY_FAILED;
                break;

            case ALLJOYN_ONAPPSUSPEND_REPLY_NO_SUPPORT:
                status = ER_ALLJOYN_ONAPPSUSPEND_REPLY_UNSUPPORTED;
                break;

            default:
                status = ER_BUS_UNEXPECTED_DISPOSITION;
                break;
            }
        }
    } else {
        QCC_LogError(status, ("%s.OnAppSuspend returned ERROR_MESSAGE (error=%s)", org::alljoyn::Bus::InterfaceName, reply->GetErrorDescription().c_str()));
    }
    return status;
}

QStatus BusAttachment::OnAppResume()
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    Message reply(*this);

    const ProxyBusObject& alljoynObj = this->GetAllJoynProxyObj();
    QStatus status = alljoynObj.MethodCall(org::alljoyn::Bus::InterfaceName, "OnAppResume", NULL, 0, reply);
    if (ER_OK == status) {
        uint32_t disposition;
        status = reply->GetArgs("u", &disposition);
        if (ER_OK == status) {
            switch (disposition) {
            case ALLJOYN_ONAPPRESUME_REPLY_SUCCESS:
                break;

            case ALLJOYN_ONAPPRESUME_REPLY_FAILED:
                status = ER_ALLJOYN_ONAPPRESUME_REPLY_FAILED;
                break;

            case ALLJOYN_ONAPPRESUME_REPLY_NO_SUPPORT:
                status = ER_ALLJOYN_ONAPPRESUME_REPLY_UNSUPPORTED;
                break;

            default:
                status = ER_BUS_UNEXPECTED_DISPOSITION;
                break;
            }
        }
    } else {
        QCC_LogError(status, ("%s.OnAppResume returned ERROR_MESSAGE (error=%s)", org::alljoyn::Bus::InterfaceName, reply->GetErrorDescription().c_str()));
    }
    return status;
}

QStatus BusAttachment::Ping(const char* name, uint32_t timeout)
{
    QCC_DbgTrace(("BusAttachment::Ping(name = %s , timeout = %d)", name, timeout));
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }

    if (!IsLegalBusName(name)) {
        return ER_BUS_BAD_BUS_NAME;
    }

    if (!name) {
        return ER_BAD_ARG_1;
    }

    Message reply(*this);
    MsgArg args[2];
    size_t numArgs = ArraySize(args);

    MsgArg::Set(args, numArgs, "su", name, timeout);

    const ProxyBusObject& alljoynObj = this->GetAllJoynProxyObj();
    QStatus status = alljoynObj.MethodCall(org::alljoyn::Bus::InterfaceName, "Ping", args, numArgs, reply, timeout + 1000);
    if (ER_OK == status) {
        uint32_t disposition;
        status = reply->GetArgs("u", &disposition);
        if (ER_OK == status) {
            switch (disposition) {
            case ALLJOYN_PING_REPLY_SUCCESS:
                break;

            case ALLJOYN_PING_REPLY_FAILED:
                status = ER_ALLJOYN_PING_FAILED;
                break;

            case ALLJOYN_PING_REPLY_TIMEOUT:
                status = ER_ALLJOYN_PING_REPLY_TIMEOUT;
                break;

            case ALLJOYN_PING_REPLY_UNKNOWN_NAME:
                status = ER_ALLJOYN_PING_REPLY_UNKNOWN_NAME;
                break;

            case ALLJOYN_PING_REPLY_INCOMPATIBLE_REMOTE_ROUTING_NODE:
                status = ER_ALLJOYN_PING_REPLY_INCOMPATIBLE_REMOTE_ROUTING_NODE;
                break;

            case ALLJOYN_PING_REPLY_UNREACHABLE:
                status = ER_ALLJOYN_PING_REPLY_UNREACHABLE;
                break;

            case ALLJOYN_PING_REPLY_IN_PROGRESS:
                status = ER_ALLJOYN_PING_REPLY_IN_PROGRESS;
                break;

            default:
                status = ER_BUS_UNEXPECTED_DISPOSITION;
                break;
            }
        }
    } else if (reply->GetType() == MESSAGE_ERROR) {
        if (!strcmp(reply->GetErrorDescription().c_str(), "org.alljoyn.Bus.Timeout")) {
            status = ER_ALLJOYN_PING_REPLY_TIMEOUT;
        } else {
            status = ER_BUS_REPLY_IS_ERROR_MESSAGE;
        }
        QCC_LogError(status, ("%s.Ping returned ERROR_MESSAGE (error=%s)", org::alljoyn::Bus::InterfaceName, reply->GetErrorDescription().c_str()));
    }
    return status;
}

QStatus BusAttachment::PingAsync(const char* name, uint32_t timeout, BusAttachment::PingAsyncCB* callback, void* context)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }
    if (!IsLegalBusName(name)) {
        return ER_BUS_BAD_BUS_NAME;
    }
    if (!name) {
        return ER_BAD_ARG_1;
    }

    MsgArg args[2];
    size_t numArgs = ArraySize(args);

    MsgArg::Set(args, numArgs, "su", name, timeout);

    const ProxyBusObject& alljoynObj = this->GetAllJoynProxyObj();
    PingAsyncCBContext* cbCtx = new PingAsyncCBContext(callback, context);

    QStatus status = alljoynObj.MethodCallAsync(org::alljoyn::Bus::InterfaceName,
                                                "Ping",
                                                busInternal,
                                                static_cast<MessageReceiver::ReplyHandler>(&BusAttachment::Internal::PingAsyncCB),
                                                args,
                                                ArraySize(args),
                                                cbCtx,
                                                timeout + 1000);
    if (status != ER_OK) {
        delete cbCtx;
    }
    return status;
}


void BusAttachment::Internal::PingAsyncCB(Message& reply, void* context)
{
    PingAsyncCBContext* ctx = reinterpret_cast<PingAsyncCBContext*>(context);

    QStatus status = ER_FAIL;
    if (reply->GetType() == MESSAGE_METHOD_RET) {
        uint32_t disposition;
        status = reply->GetArgs("u", &disposition);
        if (ER_OK == status) {
            switch (disposition) {
            case ALLJOYN_PING_REPLY_SUCCESS:
                break;

            case ALLJOYN_PING_REPLY_FAILED:
                status = ER_ALLJOYN_PING_FAILED;
                break;

            case ALLJOYN_PING_REPLY_TIMEOUT:
                status = ER_ALLJOYN_PING_REPLY_TIMEOUT;
                break;

            case ALLJOYN_PING_REPLY_UNKNOWN_NAME:
                status = ER_ALLJOYN_PING_REPLY_UNKNOWN_NAME;
                break;

            case ALLJOYN_PING_REPLY_INCOMPATIBLE_REMOTE_ROUTING_NODE:
                status = ER_ALLJOYN_PING_REPLY_INCOMPATIBLE_REMOTE_ROUTING_NODE;
                break;

            case ALLJOYN_PING_REPLY_UNREACHABLE:
                status = ER_ALLJOYN_PING_REPLY_UNREACHABLE;
                break;

            case ALLJOYN_PING_REPLY_IN_PROGRESS:
                status = ER_ALLJOYN_PING_REPLY_IN_PROGRESS;
                break;

            default:
                status = ER_BUS_UNEXPECTED_DISPOSITION;
                break;
            }
        }
    } else if (reply->GetType() == MESSAGE_ERROR) {
        if (!strcmp(reply->GetErrorDescription().c_str(), "org.alljoyn.Bus.Timeout")) {
            status = ER_ALLJOYN_PING_REPLY_TIMEOUT;
        } else {
            status = ER_BUS_REPLY_IS_ERROR_MESSAGE;
        }
        QCC_LogError(status, ("%s.Ping returned ERROR_MESSAGE (error=%s)", org::alljoyn::Bus::InterfaceName, reply->GetErrorDescription().c_str()));
    }

    /* Call the callback */
    ctx->callback->PingCB(status, ctx->context);
    delete ctx;
}

qcc::String BusAttachment::GetNameOwner(const char* alias)
{
    if (!IsConnected()) {
        return "";
    }
    if (!IsLegalBusName(alias)) {
        return "";
    }
    String ret;
    if (alias[0] == ':') {
        // the alias is already a unique name - just return it
        ret = alias;
    } else {
        Message reply(*this);
        MsgArg arg("s", alias);
        ProxyBusObject dbusObj = GetDBusProxyObj();
        QStatus status = dbusObj.MethodCall(org::freedesktop::DBus::InterfaceName, "GetNameOwner", &arg, 1, reply);
        if (status == ER_OK) {
            const char* rawUniqueName;
            status = reply->GetArgs("s", &rawUniqueName);
            if (status == ER_OK) {
                ret = rawUniqueName;
            }
        }
    }
    return ret;
}

QStatus BusAttachment::GetNameOwnerAsync(const char* alias, GetNameOwnerAsyncCB* callback, void* context)
{
    if (!IsConnected()) {
        return ER_BUS_NOT_CONNECTED;
    }
    if (!IsLegalBusName(alias)) {
        return ER_BUS_BAD_BUS_NAME;
    }
    QStatus status = ER_OK;
    if (alias[0] == ':') {
        // the alias is already a unique name - just return it
        String uniqueName = alias;
        callback->GetNameOwnerCB(ER_OK, uniqueName.c_str(), context);
    } else {
        MsgArg arg("s", alias);
        ProxyBusObject dbusObj = GetDBusProxyObj();
        GetNameOwnerCBContext* cbCtx = new GetNameOwnerCBContext(callback, context);
        status = dbusObj.MethodCallAsync(org::freedesktop::DBus::InterfaceName, "GetNameOwner",
                                         busInternal,
                                         static_cast<MessageReceiver::ReplyHandler>(&BusAttachment::Internal::GetNameOwnerAsyncCB),
                                         &arg, 1, cbCtx);
    }
    return status;
}

void BusAttachment::Internal::GetNameOwnerAsyncCB(Message& reply, void* context)
{
    GetNameOwnerCBContext* ctx = reinterpret_cast<GetNameOwnerCBContext*>(context);
    QStatus status = ER_FAIL;
    String uniqueName;

    if (reply->GetType() == MESSAGE_ERROR) {
        status = ER_BUS_REPLY_IS_ERROR_MESSAGE;
    } else {
        const char* rawUniqueName;
        status = reply->GetArgs("s", &rawUniqueName);
        if (status == ER_OK) {
            uniqueName = rawUniqueName;
        }
    }

    /* Call the callback */
    ctx->callback->GetNameOwnerCB(status, uniqueName.c_str(), ctx->context);
    delete ctx;
}

bool KeyStoreKeyEventListener::NotifyAutoDelete(KeyStore* holder, const KeyStore::Key& key)
{
    return holder->DelKey(key, true);
}

void BusAttachment::SetDescriptionTranslator(Translator* newTranslator)
{
    this->translator = newTranslator;
}

Translator* BusAttachment::GetDescriptionTranslator()
{
    return translator;
}

PermissionConfigurator& BusAttachment::GetPermissionConfigurator()
{
    return busInternal->permissionConfigurator;
}

void BusAttachment::Internal::Init()
{
    clientTransportsContainer = new ClientTransportFactoryContainer();
    s_allBusAttachments = new BusAttachmentSet();
}

void BusAttachment::Internal::Shutdown()
{
    delete clientTransportsContainer;
    clientTransportsContainer = NULL;
    delete s_allBusAttachments;
    s_allBusAttachments = nullptr;
}

QStatus BusAttachment::Internal::CallFactoryResetCallback()
{
    QStatus status = ER_OK;

    permissionConfigurationListenerLock.Lock(MUTEX_CONTEXT);
    if (permissionConfigurationListener) {
        PermissionConfigurationListener* listener = (**permissionConfigurationListener);
        if (listener) {
            status = listener->FactoryReset();
            if (status == ER_NOT_IMPLEMENTED) {
                // Treat not implemented the same as no listener registered.
                status = ER_OK;
            }
        }
    }
    permissionConfigurationListenerLock.Unlock(MUTEX_CONTEXT);
    return status;
}

void BusAttachment::Internal::CallPolicyChangedCallback()
{
    permissionConfigurationListenerLock.Lock(MUTEX_CONTEXT);
    if (permissionConfigurationListener) {
        PermissionConfigurationListener* listener = (**permissionConfigurationListener);
        if (listener) {
            listener->PolicyChanged();
        }
    }
    permissionConfigurationListenerLock.Unlock(MUTEX_CONTEXT);
}

void BusAttachment::Internal::CallStartManagementCallback()
{
    permissionConfigurationListenerLock.Lock(MUTEX_CONTEXT);
    if (permissionConfigurationListener) {
        PermissionConfigurationListener* listener = (**permissionConfigurationListener);
        if (listener) {
            listener->StartManagement();
        }
    }
    permissionConfigurationListenerLock.Unlock(MUTEX_CONTEXT);
}

void BusAttachment::Internal::CallEndManagementCallback()
{
    permissionConfigurationListenerLock.Lock(MUTEX_CONTEXT);
    if (permissionConfigurationListener) {
        PermissionConfigurationListener* listener = (**permissionConfigurationListener);
        if (listener) {
            listener->EndManagement();
        }
    }
    permissionConfigurationListenerLock.Unlock(MUTEX_CONTEXT);
}

QStatus BusAttachment::Internal::SetPermissionConfigurationListener(PermissionConfigurationListener* listener)
{
    permissionConfigurationListenerLock.Lock(MUTEX_CONTEXT);
    delete permissionConfigurationListener;
    permissionConfigurationListener = new ProtectedPermissionConfigurationListener(listener);
    permissionConfigurationListenerLock.Unlock(MUTEX_CONTEXT);
    return ER_OK;
}

}
