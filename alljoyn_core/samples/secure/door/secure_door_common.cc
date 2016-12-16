/******************************************************************************
 *    Copyright (c) Open Connectivity Foundation (OCF) and AllJoyn Open
 *    Source Project (AJOSP) Contributors and others.
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
 *     THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 *     WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 *     WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 *     AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 *     DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 *     PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 *     TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 *     PERFORMANCE OF THIS SOFTWARE.
 ******************************************************************************/

#include "secure_door_common.h"

#include <alljoyn/PermissionPolicy.h>

#if defined(QCC_OS_GROUP_WINDOWS)
#include <Winsock2.h> // gethostname
#endif

#define PASSWORD_LEN 6

using namespace ajn;
using namespace qcc;

namespace sample {
namespace secure {
namespace door {

void DoorCommonPCL::StartManagement()
{
    printf("StartManagement called.\n");
}

void DoorCommonPCL::EndManagement()
{
    printf("EndManagement called.\n");
    lock.Lock();
    QStatus status;
    PermissionConfigurator::ApplicationState appState;
    if (ER_OK == (status = ba.GetPermissionConfigurator().GetApplicationState(appState))) {
        if (PermissionConfigurator::ApplicationState::CLAIMED == appState) {
            sem.Signal();
        } else {
            fprintf(stderr, "App not claimed after management finished. Continuing to wait.\n");
        }
    } else {
        fprintf(stderr, "Failed to GetApplicationState - status (%s)\n", QCC_StatusText(status));
    }
    lock.Unlock();
}

QStatus DoorCommonPCL::WaitForClaimedState()
{
    lock.Lock();
    PermissionConfigurator::ApplicationState appState;

    QStatus status = ba.GetPermissionConfigurator().GetApplicationState(appState);
    if (ER_OK != status) {
        fprintf(stderr, "Failed to GetApplicationState - status (%s)\n",
                QCC_StatusText(status));
        lock.Unlock();
        return status;
    }

    if (PermissionConfigurator::ApplicationState::CLAIMED == appState) {
        printf("Already claimed !\n");
        lock.Unlock();
        return ER_OK;
    }

    printf("Waiting to be claimed...\n");
    status = sem.Wait(lock);
    if (ER_OK != status) {
        lock.Unlock();
        return status;
    }

    printf("Claimed !\n");
    lock.Unlock();
    return ER_OK;
}

Door::Door(BusAttachment* ba) :
    BusObject(DOOR_OBJECT_PATH), autoSignal(false), open(false),
    busAttachment(ba), stateSignal(nullptr)
{
}

QStatus Door::Init()
{
    QStatus status = ER_FAIL;

    const InterfaceDescription* secPermIntf = busAttachment->GetInterface(DOOR_INTERFACE);
    if (!secPermIntf) {
        fprintf(stderr, "Failed to GetInterface\n");
        return status;
    }

    status = AddInterface(*secPermIntf, ANNOUNCED);
    if (ER_OK != status) {
        fprintf(stderr, "Failed to AddInterface - status (%s)\n", QCC_StatusText(status));
        return status;
    }

    /* Register the method handlers with the door bus object */
    const MethodEntry methodEntries[] = {
        { secPermIntf->GetMember(DOOR_OPEN), static_cast<MessageReceiver::MethodHandler>(&Door::Open) },
        { secPermIntf->GetMember(DOOR_CLOSE), static_cast<MessageReceiver::MethodHandler>(&Door::Close) },
        { secPermIntf->GetMember(DOOR_GET_STATE), static_cast<MessageReceiver::MethodHandler>(&Door::GetState) }
    };
    status = AddMethodHandlers(methodEntries, sizeof(methodEntries) / sizeof(methodEntries[0]));
    if (ER_OK != status) {
        fprintf(stderr, "Failed to AddMethodHandlers - status (%s)\n", QCC_StatusText(status));
        return status;
    }

    stateSignal = secPermIntf->GetMember(DOOR_STATE_CHANGED);

    return status;
}

QStatus Door::SendDoorEvent()
{
    printf("Sending door event ...\n");
    MsgArg outArg;
    outArg.Set("b", open);

    QStatus status = Signal(nullptr, SESSION_ID_ALL_HOSTED, *stateSignal, &outArg, 1, 0, 0,  nullptr);
    if (status != ER_OK) {
        fprintf(stderr, "Failed to send Signal - status (%s)\n", QCC_StatusText(status));
    }

    return status;
}

void Door::ReplyWithBoolean(bool answer, Message& msg)
{
    MsgArg outArg;
    outArg.Set("b", answer);

    QStatus status = MethodReply(msg, &outArg, 1);
    if (status != ER_OK) {
        fprintf(stderr, "Failed to send MethodReply - status (%s)\n", QCC_StatusText(status));
    }
}

void Door::Open(const InterfaceDescription::Member* member, Message& msg)
{
    QCC_UNUSED(member);

    printf("Door Open method was called\n");
    if (open == false) {
        open = true;
        if (autoSignal) {
            SendDoorEvent();
        }
    }
    ReplyWithBoolean(true, msg);
}

void Door::Close(const InterfaceDescription::Member* member,
                 Message& msg)
{
    QCC_UNUSED(member);

    printf("Door Close method called\n");
    if (open) {
        open = false;
        if (autoSignal) {
            SendDoorEvent();
        }
    }
    ReplyWithBoolean(true, msg);
}

QStatus Door::Get(const char* ifcName, const char* propName, MsgArg& val)
{
    printf("Door::Get(%s)@%s\n", propName, ifcName);
    // Only one property is available
    if (strcmp(ifcName, DOOR_INTERFACE) == 0 && strcmp(propName, DOOR_STATE) == 0) {
        val.Set("b", open);
        return ER_OK;
    }
    return ER_BUS_NO_SUCH_PROPERTY;
}

void Door::GetState(const InterfaceDescription::Member* member,
                    Message& msg)
{
    QCC_UNUSED(member);

    printf("Door GetState method was called\n");
    ReplyWithBoolean(open, msg);
}

QStatus DoorCommon::CreateInterface()
{
    InterfaceDescription* doorIntf = nullptr;
    // Create a secure door interface on the bus attachment
    QStatus status = ba->CreateInterface(DOOR_INTERFACE, doorIntf, AJ_IFC_SECURITY_REQUIRED);
    if (ER_OK == status) {
        printf("Secure door interface was created.\n");
        doorIntf->AddMethod(DOOR_OPEN, nullptr, "b", "success");
        doorIntf->AddMethod(DOOR_CLOSE, nullptr, "b", "success");
        doorIntf->AddMethod(DOOR_GET_STATE, nullptr, "b", "state");
        doorIntf->AddSignal(DOOR_STATE_CHANGED, "b", "state", 0);
        doorIntf->AddProperty(DOOR_STATE, "b", PROP_ACCESS_RW);
        doorIntf->Activate();
    } else {
        printf("Failed to create Secure PermissionMgmt interface.\n");
    }

    return status;
}

void DoorCommon::SetAboutData()
{
    GUID128 appId;
    aboutData.SetAppId(appId.ToString().c_str());

    char buf[64];
    gethostname(buf, sizeof(buf));
    aboutData.SetDeviceName(buf);

    GUID128 deviceId;
    aboutData.SetDeviceId(deviceId.ToString().c_str());
    aboutData.SetAppName(appName.c_str());
    aboutData.SetManufacturer("Manufacturer");
    aboutData.SetModelNumber("1");
    aboutData.SetDescription(appName.c_str());
    aboutData.SetDateOfManufacture("2015-04-14");
    aboutData.SetSoftwareVersion("0.1");
    aboutData.SetHardwareVersion("0.0.1");
    aboutData.SetSupportUrl("https://allseenalliance.org/");
}

QStatus DoorCommon::HostSession()
{
    SessionOpts opts;
    SessionPort port = DOOR_APPLICATION_PORT;

    return ba->BindSessionPort(port, opts, spl);
}

QStatus DoorCommon::AnnounceAbout()
{
    SetAboutData();

    if (!aboutData.IsValid()) {
        fprintf(stderr, "Invalid aboutData\n");
        return ER_FAIL;
    }

    return aboutObj->Announce(DOOR_APPLICATION_PORT, aboutData);
}

QStatus RandomPassword(string& password)
{
    GUID128 randomGuid;
    const uint8_t* randomData = randomGuid.GetBytes();

    QCC_ASSERT(password.length() <= GUID128::SIZE);

    for (size_t i = 0; i < password.length(); i++) {
        uint8_t value = (randomData[i] % 16);
        if (value < 10) {
            password[i] = '0' + value;
        } else {
            password[i] = 'A' + value - 10;
        }
    }

    return ER_OK;
}

QStatus DoorCommon::Init(bool provider, PermissionConfigurationListener* inPcl)
{
    CreateInterface();

    pcl = inPcl;

    QStatus status = ba->Start();
    if (ER_OK != status) {
        fprintf(stderr, "Failed to Start bus attachment - status (%s)\n", QCC_StatusText(status));
        return status;
    }

    status = ba->Connect();
    if (ER_OK != status) {
        fprintf(stderr, "Failed to Connect bus attachment - status (%s)\n", QCC_StatusText(status));
        return status;
    }

    string password;
    password.resize(PASSWORD_LEN);
    status = RandomPassword(password);
    if (ER_OK != status) {
        fprintf(stderr, "Failed to generate random password");
        return status;
    }

    authListener = new DefaultECDHEAuthListener();
    if (provider) {
        status = authListener->SetPassword((uint8_t*)password.data(), password.length());
        if (ER_OK != status) {
            fprintf(stderr, "Failed to set password");
            return status;
        }
    }

    status = ba->EnablePeerSecurity(KEYX_ECDHE_DSA " " KEYX_ECDHE_NULL " " KEYX_ECDHE_PSK " " KEYX_ECDHE_SPEKE, authListener, nullptr, false, pcl);
    if (ER_OK != status) {
        fprintf(stderr, "Failed to EnablePeerSecurity - status (%s)\n", QCC_StatusText(status));
        return status;
    }

    if (provider) {
        printf("Allow doors to be claimable using a password.\n");
        status = ba->GetPermissionConfigurator().SetClaimCapabilities(
            PermissionConfigurator::CAPABLE_ECDHE_SPEKE | PermissionConfigurator::CAPABLE_ECDHE_NULL);
        if (ER_OK != status) {
            fprintf(stderr, "Failed to SetClaimCapabilities - status (%s)\n", QCC_StatusText(status));
            return status;
        }

        status = ba->GetPermissionConfigurator().SetClaimCapabilityAdditionalInfo(
            PermissionConfigurator::PSK_GENERATED_BY_APPLICATION);
        if (ER_OK != status) {
            fprintf(stderr, "Failed to SetClaimCapabilityAdditionalInfo - status (%s)\n",
                    QCC_StatusText(status));
            return status;
        }
    } else {
        /* For consumers only claiming with ECDHE_NULL is supported, and the default claim capabilities allow other options. */
        printf("This application must be claimed with ECDHE_NULL.\n");
        status = ba->GetPermissionConfigurator().SetClaimCapabilities(PermissionConfigurator::CAPABLE_ECDHE_NULL);
        if (ER_OK != status) {
            fprintf(stderr, "Failed to SetClaimCapabilities - status (%s)\n", QCC_StatusText(status));
            return status;
        }
    }

    PermissionPolicy::Rule manifestRule;
    manifestRule.SetInterfaceName(DOOR_INTERFACE);

    if (provider) {
        // Set a very flexible default manifest for the door provider
        PermissionPolicy::Rule::Member members[2];
        members[0].SetMemberName("*");
        members[0].SetActionMask(PermissionPolicy::Rule::Member::ACTION_PROVIDE);
        members[0].SetMemberType(PermissionPolicy::Rule::Member::METHOD_CALL);
        members[1].SetMemberName("*");
        members[1].SetActionMask(PermissionPolicy::Rule::Member::ACTION_PROVIDE);
        members[1].SetMemberType(PermissionPolicy::Rule::Member::PROPERTY);
        manifestRule.SetMembers(2, members);
    } else {
        // Set a very flexible default manifest for the door consumer
        PermissionPolicy::Rule::Member member;
        member.SetMemberName("*");
        member.SetActionMask(PermissionPolicy::Rule::Member::ACTION_MODIFY |
                             PermissionPolicy::Rule::Member::ACTION_OBSERVE);
        member.SetMemberType(PermissionPolicy::Rule::Member::NOT_SPECIFIED);
        manifestRule.SetMembers(1, &member);
    }

    status = ba->GetPermissionConfigurator().SetPermissionManifestTemplate(&manifestRule, 1);
    if (ER_OK != status) {
        fprintf(stderr, "Failed to SetPermissionManifestTemplate - status (%s)\n", QCC_StatusText(status));
        return status;
    }

    if (provider) {
        PermissionConfigurator::ApplicationState state;
        status = ba->GetPermissionConfigurator().GetApplicationState(state);
        if (ER_OK != status) {
            fprintf(stderr, "Failed to GetApplicationState - status (%s)\n", QCC_StatusText(status));
            return status;
        }

        if (PermissionConfigurator::CLAIMABLE == state) {
            printf("Door provider is not claimed.\n");
            printf("The provider can be claimed using SPEKE with an application generated secret.\n");
            printf("Password = (%s)\n", password.c_str());
        }
    }

    return HostSession();
}

QStatus DoorCommon::SetSecurityForClaimedMode()
{
    QStatus status = ba->EnablePeerSecurity("", nullptr, nullptr, true);
    if (ER_OK != status) {
        fprintf(stderr, "SetSecurityForClaimedMode: Could not clear peer security - status (%s)\n", QCC_StatusText(status));
        return status;
    }

    status = ba->EnablePeerSecurity(KEYX_ECDHE_DSA, authListener, nullptr, false, pcl);
    if (ER_OK != status) {
        fprintf(stderr, "SetSecurityForClaimedMode: Could not reset peer security - status (%s)\n", QCC_StatusText(status));
        return status;
    }

    return ER_OK;
}

QStatus DoorCommon::UpdateManifest(const PermissionPolicy::Acl& manifest)
{
    PermissionPolicy::Rule* rules = const_cast<PermissionPolicy::Rule*> (manifest.GetRules());

    QStatus status = ba->GetPermissionConfigurator().SetPermissionManifestTemplate(rules, manifest.GetRulesSize());
    if (ER_OK != status) {
        fprintf(stderr, "Failed to SetPermissionManifestTemplate - status (%s)\n", QCC_StatusText(status));
        return status;
    }

    status = ba->GetPermissionConfigurator().SetApplicationState(PermissionConfigurator::NEED_UPDATE);
    if (ER_OK != status) {
        fprintf(stderr, "Failed to SetApplicationState - status (%s)\n", QCC_StatusText(status));
    }

    return status;
}

void DoorCommon::Fini()
{
    /**
     * This is needed to make sure that the authentication listener is removed before the
     * bus attachment is destructed.
     * Use an empty string as a first parameter (authMechanism) to avoid resetting the keyStore
     * so previously claimed apps can still be so after restarting.
     **/
    ba->EnablePeerSecurity("", nullptr, nullptr, true);

    delete authListener;
    authListener = nullptr;

    delete aboutObj;
    aboutObj = nullptr;

    ba->Disconnect();
    ba->Stop();
    ba->Join();

    delete ba;
    ba = nullptr;
}

DoorCommon::~DoorCommon()
{
}
}
}
}