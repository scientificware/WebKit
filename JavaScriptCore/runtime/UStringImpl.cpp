/*
 * Copyright (C) 2009 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"
#include "UStringImpl.h"

#include "Identifier.h"
#include "UString.h"
#include <wtf/unicode/UTF8.h>

using namespace WTF::Unicode;
using namespace std;

namespace JSC {
 
SharedUChar* UStringImpl::baseSharedBuffer()
{
    ASSERT((bufferOwnership() == BufferShared)
        || ((bufferOwnership() == BufferOwned) && !m_dataBuffer.asPtr<void*>()));

    if (bufferOwnership() != BufferShared)
        m_dataBuffer = UntypedPtrAndBitfield(SharedUChar::create(new OwnFastMallocPtr<UChar>(m_data)).releaseRef(), BufferShared);

    return m_dataBuffer.asPtr<SharedUChar*>();
}

SharedUChar* UStringImpl::sharedBuffer()
{
    if (m_length < s_minLengthToShare)
        return 0;
    ASSERT(!isStatic());

    UStringImpl* owner = bufferOwnerString();
    if (owner->bufferOwnership() == BufferInternal)
        return 0;

    return owner->baseSharedBuffer();
}

void UStringImpl::destroy()
{
    ASSERT(!isStatic());
    checkConsistency();

    if (isIdentifier())
        Identifier::remove(this);

    if (bufferOwnership() != BufferInternal) {
        if (bufferOwnership() == BufferOwned)
            fastFree(m_data);
        else if (bufferOwnership() == BufferSubstring)
            m_dataBuffer.asPtr<UStringImpl*>()->deref();
        else {
            ASSERT(bufferOwnership() == BufferShared);
            m_dataBuffer.asPtr<SharedUChar*>()->deref();
        }
    }

    delete this;
}

}
