/**
 * @file
 *
 * Implement Alarm object
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
#include <qcc/Alarm.h>

#define QCC_MODULE  "ALARM"

using namespace qcc;

volatile int32_t qcc::_Alarm::nextId = 0;

_Alarm::_Alarm() : listener(NULL), periodMs(0), context(NULL), id(IncrementAndFetch(&nextId)), limitable(true)
{
}

_Alarm::_Alarm(Timespec<MonotonicTime> relativeTime, AlarmListener* listener, void* context, uint32_t periodMs, bool limitable)
    : alarmTime(relativeTime), listener(listener), periodMs(periodMs), context(context), id(IncrementAndFetch(&nextId)), limitable(limitable)
{
}

_Alarm::_Alarm(uint32_t relativeTime, AlarmListener* listener, void* context, uint32_t periodMs, bool limitable)
    : alarmTime(), listener(listener), periodMs(periodMs), context(context), id(IncrementAndFetch(&nextId)), limitable(limitable)
{
    if (relativeTime == WAIT_FOREVER) {
        alarmTime = Timespec<MonotonicTime>(END_OF_TIME);
    } else {
        GetTimeNow(&alarmTime);
        alarmTime += relativeTime;
    }
}

_Alarm::_Alarm(AlarmListener* listener, void* context, bool limitable)
    : alarmTime(0), listener(listener), periodMs(0), context(context), id(IncrementAndFetch(&nextId)), limitable(limitable)
{
}

void* _Alarm::GetContext(void) const
{
    return context;
}

void _Alarm::SetContext(void* c) const
{
    context = c;
}

uint64_t _Alarm::GetAlarmTime() const
{
    return alarmTime.GetMillis();
}

bool _Alarm::operator<(const _Alarm& other) const
{
    return (alarmTime < other.alarmTime) || ((alarmTime == other.alarmTime) && (id < other.id));
}

bool _Alarm::operator==(const _Alarm& other) const
{
    return (alarmTime == other.alarmTime) && (id == other.id);
}
