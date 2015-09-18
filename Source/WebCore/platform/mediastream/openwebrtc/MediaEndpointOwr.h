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

#ifndef MediaEndpointOwr_h
#define MediaEndpointOwr_h

#if ENABLE(MEDIA_STREAM)

#include "MediaEndpoint.h"
#include <owr/owr_media_session.h>
#include <owr/owr_transport_agent.h>

namespace WebCore {

class PeerMediaDescription;
class RTCConfigurationPrivate;

class MediaEndpointOwr : public MediaEndpoint {
public:
    MediaEndpointOwr(MediaEndpointClient*);
    ~MediaEndpointOwr();

    virtual void setConfiguration(RefPtr<MediaEndpointInit>&&) override;

    virtual void prepareToReceive(MediaEndpointConfiguration*, bool isInitiator) override;
    virtual void prepareToSend(MediaEndpointConfiguration*, bool isInitiator) override;

    virtual void addRemoteCandidate(IceCandidate&, unsigned mdescIndex, const String& ufrag, const String& password) override;

    virtual void stop() override;

    unsigned sessionIndex(OwrSession*) const;

    void dispatchNewIceCandidate(unsigned sessionIndex, RefPtr<IceCandidate>&&);
    void dispatchGatheringDone(unsigned sessionIndex);
    void dispatchDtlsCertificate(unsigned sessionIndex, const String& certificate);
    void dispatchRemoteSource(unsigned sessionIndex, RefPtr<RealtimeMediaSource>&&);

private:
    enum SessionType { SessionTypeMedia };

    struct SessionConfig {
        SessionType type;
        bool isDtlsClient;
    };

    void prepareSession(OwrSession*, PeerMediaDescription*);
    void prepareMediaSession(OwrMediaSession*, PeerMediaDescription*, bool isInitiator);

    void ensureTransportAgentAndSessions(bool isInitiator, const Vector<SessionConfig>& sessionConfigs);
    void internalAddRemoteCandidate(OwrSession*, IceCandidate&, const String& ufrag, const String& password);

    RefPtr<MediaEndpointInit> m_configuration;

    OwrTransportAgent* m_transportAgent;
    Vector<OwrSession*> m_sessions;

    MediaEndpointClient* m_client;

    unsigned m_numberOfReceivePreparedSessions;
    unsigned m_numberOfSendPreparedSessions;
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)

#endif // MediaEndpointOwr_h
