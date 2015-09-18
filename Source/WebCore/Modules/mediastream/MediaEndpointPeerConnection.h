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

#ifndef MediaEndpointPeerConnection_h
#define MediaEndpointPeerConnection_h

#if ENABLE(MEDIA_STREAM)

#include "MediaEndpoint.h"
#include "MediaEndpointConfiguration.h"
#include "PeerConnectionBackend.h"
#include <wtf/RefPtr.h>
#include <wtf/RefCounted.h>

namespace WebCore {

class DOMWrapperWorld;

class MediaEndpointPeerConnection : public PeerConnectionBackend, public MediaEndpointClient {
public:
    MediaEndpointPeerConnection(PeerConnectionBackendClient*);
    ~MediaEndpointPeerConnection();

    void createOffer(const RefPtr<RTCOfferOptions>&, OfferAnswerResolveCallback, RejectCallback) override;
    void createAnswer(const RefPtr<RTCAnswerOptions>&, OfferAnswerResolveCallback, RejectCallback) override;

    void setLocalDescription(RTCSessionDescription*, PeerConnectionStates::SignalingState targetState, VoidResolveCallback, RejectCallback) override;
    RefPtr<RTCSessionDescription> localDescription() const override;

    void setRemoteDescription(RTCSessionDescription*, PeerConnectionStates::SignalingState targetState, VoidResolveCallback, RejectCallback) override;
    RefPtr<RTCSessionDescription> remoteDescription() const override;

    void setConfiguration(RTCConfiguration&) override;
    void addIceCandidate(RTCIceCandidate*, VoidResolveCallback, RejectCallback) override;

    void stop() override;

private:
    enum ResolveSetLocalDescriptionResult {
        LocalConfigurationIncomplete,
        SetLocalDescriptionResolvedSuccessfully,
        SetLocalDescriptionAlreadyResolved
    };

    void enqueueOperation(std::function<void ()>);
    void completeQueuedOperation();

    void queuedCreateOffer(const RefPtr<RTCOfferOptions>&, OfferAnswerResolveCallback, RejectCallback);
    void queuedCreateAnswer(const RefPtr<RTCAnswerOptions>&, OfferAnswerResolveCallback, RejectCallback);

    void queuedSetLocalDescription(RTCSessionDescription*, PeerConnectionStates::SignalingState targetState, VoidResolveCallback, RejectCallback);
    void queuedSetRemoteDescription(RTCSessionDescription*, PeerConnectionStates::SignalingState targetState, VoidResolveCallback, RejectCallback);

    void queuedAddIceCandidate(RTCIceCandidate*, VoidResolveCallback, RejectCallback);

    bool isLocalConfigurationComplete() const;
    ResolveSetLocalDescriptionResult maybeResolveSetLocalDescription();
    void maybeDispatchGatheringDone();

    // MediaEndpointClient
    virtual void gotDtlsCertificate(unsigned mdescIndex, const String& certificate) override;
    virtual void gotIceCandidate(unsigned mdescIndex, RefPtr<IceCandidate>&&) override;
    virtual void doneGatheringCandidates(unsigned mdescIndex) override;
    virtual void gotRemoteSource(unsigned mdescIndex, RefPtr<RealtimeMediaSource>&&) override;

    String toSDP(const String& json) const;
    String fromSDP(const String& sdp) const;
    String iceCandidateToSDP(const String& json) const;
    String iceCandidateFromSDP(const String& sdpFragment) const;
    String sdpConversion(const String& functionName, const String& argument) const;

    PeerConnectionBackendClient* m_client;
    std::unique_ptr<MediaEndpoint> m_mediaEndpoint;

    Vector<std::function<void ()>> m_operationsQueue;

    mutable RefPtr<DOMWrapperWorld> m_isolatedWorld;

    String m_cname;
    String m_iceUfrag;
    String m_icePassword;

    RefPtr<MediaEndpointConfiguration> m_localConfiguration;
    RefPtr<MediaEndpointConfiguration> m_remoteConfiguration;

    String m_localConfigurationType;
    String m_remoteConfigurationType;

    std::function<void()> m_resolveSetLocalDescription;

    RefPtr<RTCConfiguration> m_configuration;
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)

#endif // MediaEndpointPeerConnection_h
