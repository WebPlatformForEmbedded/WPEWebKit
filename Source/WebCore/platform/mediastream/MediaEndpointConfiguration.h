/*
 * Copyright (C) 2015 Ericsson AB. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of Ericsson nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef MediaEndpointConfiguration_h
#define MediaEndpointConfiguration_h

#if ENABLE(MEDIA_STREAM)

#include "PeerMediaDescription.h"
#include <wtf/CryptographicallyRandomNumber.h>

namespace WebCore {

class MediaEndpointConfiguration : public RefCounted<MediaEndpointConfiguration> {
public:
    static RefPtr<MediaEndpointConfiguration> create()
    {
        return adoptRef(new MediaEndpointConfiguration());
    }
    virtual ~MediaEndpointConfiguration() { }

    uint64_t sessionId() const { return m_sessionId; }
    void setSessionId(uint64_t sessionId) { m_sessionId = sessionId; }

    unsigned sessionVersion() const { return m_sessionVersion; }
    void setSessionVersion(unsigned sessionVersion) { m_sessionVersion = sessionVersion; }

    const Vector<RefPtr<PeerMediaDescription>>& mediaDescriptions() const { return m_mediaDescriptions; }
    void addMediaDescription(RefPtr<PeerMediaDescription>&& description) { m_mediaDescriptions.append(WTF::move(description)); }

private:
    MediaEndpointConfiguration()
        : m_sessionId(cryptographicallyRandomNumber()) // FIXME: should be 64 bits
        , m_sessionVersion(0)
    { }

    uint64_t m_sessionId;
    unsigned m_sessionVersion;

    Vector<RefPtr<PeerMediaDescription>> m_mediaDescriptions;
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)

#endif // MediaEndpointConfiguration_h
