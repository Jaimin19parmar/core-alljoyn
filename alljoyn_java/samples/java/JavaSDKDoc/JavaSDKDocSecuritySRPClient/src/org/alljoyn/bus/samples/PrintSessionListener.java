/*
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
 */
package org.alljoyn.bus.samples;

import org.alljoyn.bus.SessionListener;

public class PrintSessionListener extends SessionListener {
    @Override
    public void sessionLost(int sessionId, int reason) {
        System.out.println("Session Lost : " + sessionId);
        switch (reason) {
            case SessionListener.ALLJOYN_SESSIONLOST_INVALID:
                System.out.println("Reason: INVALID");
                break;
            case SessionListener.ALLJOYN_SESSIONLOST_REMOTE_END_LEFT_SESSION:
                System.out.println("Reason: REMOTE END LEFT SESSION");
                break;
            case SessionListener.ALLJOYN_SESSIONLOST_REMOTE_END_CLOSED_ABRUPTLY:
                System.out.println("Reason: REMOTE END CLOSED ABRUPTLY");
                break;
            case SessionListener.ALLJOYN_SESSIONLOST_REMOVED_BY_BINDER:
                System.out.println("Reason: REMOVED BY BINDER");
                break;
            case SessionListener.ALLJOYN_SESSIONLOST_LINK_TIMEOUT:
                System.out.println("Reason: LINK TIMEOUT");
                break;
            case SessionListener.ALLJOYN_SESSIONLOST_REASON_OTHER:
                System.out.println("Reason: OTHER");
                break;
            case SessionListener.ALLJOYN_SESSIONLOST_REMOVED_BY_BINDER_SELF:
                System.out.println("Reason: REMOVED BY BINDER SELF");
                break;
        }
    }
}
