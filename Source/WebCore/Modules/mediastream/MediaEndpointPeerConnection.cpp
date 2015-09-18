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

#include "config.h"

#if ENABLE(MEDIA_STREAM)
#include "MediaEndpointPeerConnection.h"

#include "CryptoDigest.h"
#include "DOMError.h"
#include "MediaEndpointConfigurationConversions.h"
#include "MediaStreamTrack.h"
#include "PeerConnectionBackend.h"
#include "PeerMediaDescription.h"
#include "RTCConfiguration.h"
#include "RTCIceCandidate.h"
#include "RTCIceCandidateEvent.h"
#include "RTCOfferAnswerOptions.h"
#include "RTCSessionDescription.h"
#include "RTCRtpReceiver.h"
#include "RTCRtpSender.h"
#include "RTCTrackEvent.h"
#include "UUID.h"
#include <wtf/text/Base64.h>

// FIXME: Headers for sdp.js supported SDP conversion is kept on the side for now
#include "Document.h"
#include "Frame.h"
#include "JSDOMWindow.h"
#include "ScriptController.h"
#include "ScriptGlobalObject.h"
#include "ScriptSourceCode.h"
#include "SDPScriptResource.h"
#include <bindings/ScriptObject.h>

namespace WebCore {

using namespace PeerConnectionStates;

static std::unique_ptr<PeerConnectionBackend> createMediaEndpointPeerConnection(PeerConnectionBackendClient* client)
{
    return std::unique_ptr<PeerConnectionBackend>(new MediaEndpointPeerConnection(client));
}

CreatePeerConnectionBackend PeerConnectionBackend::create = createMediaEndpointPeerConnection;

static String randomString(size_t length)
{
    const size_t size = ceil(length * 3 / 4);
    unsigned char randomValues[size];
    cryptographicallyRandomValues(randomValues, size);
    return base64Encode(randomValues, size);
}

static RefPtr<MediaEndpointInit> createMediaEndpointInit(RTCConfiguration& rtcConfig)
{
    Vector<RefPtr<IceServerInfo>> iceServers;
    for (auto& server : rtcConfig.iceServers())
        iceServers.append(IceServerInfo::create(server->urls(), server->credential(), server->username()));

    return MediaEndpointInit::create(iceServers, rtcConfig.iceTransportPolicy(), rtcConfig.bundlePolicy());
}

MediaEndpointPeerConnection::MediaEndpointPeerConnection(PeerConnectionBackendClient* client)
    : m_client(client)
    , m_cname(randomString(16))
    , m_iceUfrag(randomString(4))
    , m_icePassword(randomString(22))
{
    m_mediaEndpoint = MediaEndpoint::create(this);
    ASSERT(m_mediaEndpoint);
}

MediaEndpointPeerConnection::~MediaEndpointPeerConnection()
{
}

static RefPtr<RTCRtpSender> takeFirstSenderOfType(Vector<RefPtr<RTCRtpSender>>& senders, const String& type)
{
    for (unsigned i = 0; i < senders.size(); ++i) {
        if (senders[i]->track()->kind() == type) {
            RefPtr<RTCRtpSender> sender = senders[i];
            senders.remove(i);
            return sender;
        }
    }
    return nullptr;
}

// FIXME: This information should be fetched from the platform
static Vector<RefPtr<MediaPayload>> createDefaultPayloads(const String& type)
{
    Vector<RefPtr<MediaPayload>> payloads;
    RefPtr<MediaPayload> payload;

    if (type == "audio") {
        payload = MediaPayload::create();
        payload->setType(111);
        payload->setEncodingName("OPUS");
        payload->setClockRate(48000);
        payload->setChannels(2);
        payloads.append(payload);

        payload = MediaPayload::create();
        payload->setType(8);
        payload->setEncodingName("PCMA");
        payload->setClockRate(8000);
        payload->setChannels(1);
        payloads.append(payload);

        payload = MediaPayload::create();
        payload->setType(0);
        payload->setEncodingName("PCMU");
        payload->setClockRate(8000);
        payload->setChannels(1);
        payloads.append(payload);
    } else {
        // payload = MediaPayload::create();
        // payload->setType(103);
        // payload->setEncodingName("H264");
        // payload->setClockRate(90000);
        // payload->setCcmfir(true);
        // payload->setNackpli(true);
        // payload->addParameter("packetizationMode", 1);
        // payloads.append(payload);

        payload = MediaPayload::create();
        payload->setType(100);
        payload->setEncodingName("VP8");
        payload->setClockRate(90000);
        payload->setCcmfir(true);
        payload->setNackpli(true);
        payload->setNack(true);
        payloads.append(payload);

        payload = MediaPayload::create();
        payload->setType(120);
        payload->setEncodingName("RTX");
        payload->setClockRate(90000);
        payload->addParameter("apt", 100);
        payload->addParameter("rtxTime", 200);
        payloads.append(payload);
    }

    return payloads;
}

static void updateMediaDescriptionsWithSenders(const Vector<RefPtr<PeerMediaDescription>>& mediaDescriptions, Vector<RefPtr<RTCRtpSender>>& senders)
{
    // Remove any sender(s) from the senders list that already have their tracks represented by a media
    // description. Mark media descriptions that don't have a sender/track (anymore) as "available".
    for (auto& mdesc : mediaDescriptions) {
        const String& mdescTrackId = mdesc->mediaStreamTrackId();
        bool foundSender = senders.removeFirstMatching([mdescTrackId](const RefPtr<RTCRtpSender>& sender) -> bool {
            return sender->track()->id() == mdescTrackId;
        });
        if (!foundSender) {
            mdesc->setMediaStreamId(emptyString());
            mdesc->setMediaStreamTrackId(emptyString());
            mdesc->clearSsrcs();
        }
    }

    // Remove any sender(s) from the senders list that can be matched (by track type) to an "available"
    // media description. Mark media descriptions that don't get matched with a sender as receive only.
    for (auto& mdesc : mediaDescriptions) {
        if (mdesc->mediaStreamTrackId() != emptyString())
            continue;

        RefPtr<RTCRtpSender> sender = takeFirstSenderOfType(senders, mdesc->type());
        if (sender) {
            // FIXME: what else needs to be updated to reuse a media description?
            mdesc->setMediaStreamId(sender->mediaStreamId());
            mdesc->setMediaStreamTrackId(sender->track()->id());
            mdesc->addSsrc(cryptographicallyRandomNumber());
            mdesc->setMode("sendrecv");
        } else
            mdesc->setMode("recvonly");
    }
}

void MediaEndpointPeerConnection::enqueueOperation(std::function<void ()> operation)
{
    m_operationsQueue.append(operation);
    if (m_operationsQueue.size() == 1)
        callOnMainThread(m_operationsQueue[0]);
}

void MediaEndpointPeerConnection::completeQueuedOperation()
{
    m_operationsQueue.remove(0);
    if (!m_operationsQueue.isEmpty())
        callOnMainThread(m_operationsQueue[0]);
}

void MediaEndpointPeerConnection::createOffer(const RefPtr<RTCOfferOptions>& options, OfferAnswerResolveCallback resolveCallback, RejectCallback rejectCallback)
{
    RefPtr<RTCOfferOptions> protectedOptions = options;
    enqueueOperation([this, protectedOptions, resolveCallback, rejectCallback]() {
        queuedCreateOffer(protectedOptions, resolveCallback, rejectCallback);
    });
}

void MediaEndpointPeerConnection::queuedCreateOffer(const RefPtr<RTCOfferOptions>& options, OfferAnswerResolveCallback resolveCallback, RejectCallback)
{
    RefPtr<MediaEndpointConfiguration> configurationSnapshot = m_localConfiguration ?
        MediaEndpointConfigurationConversions::fromJSON(MediaEndpointConfigurationConversions::toJSON(m_localConfiguration.get())) : MediaEndpointConfiguration::create();

    Vector<RefPtr<RTCRtpSender>> senders = m_client->getSenders();
    updateMediaDescriptionsWithSenders(configurationSnapshot->mediaDescriptions(), senders);

    // Add media descriptions for remaining senders.
    for (auto& sender : senders) {
        RefPtr<PeerMediaDescription> mediaDescription = PeerMediaDescription::create();
        MediaStreamTrack* track = sender->track();

        mediaDescription->setMediaStreamId(sender->mediaStreamId());
        mediaDescription->setMediaStreamTrackId(track->id());
        mediaDescription->setType(track->kind());
        mediaDescription->setMode("sendrecv");
        mediaDescription->setPayloads(createDefaultPayloads(track->kind()));
        mediaDescription->setRtcpMux(true);
        mediaDescription->setDtlsSetup("actpass");
        mediaDescription->setCname(m_cname);
        mediaDescription->addSsrc(cryptographicallyRandomNumber());
        mediaDescription->setIceUfrag(m_iceUfrag);
        mediaDescription->setIcePassword(m_icePassword);

        configurationSnapshot->addMediaDescription(WTF::move(mediaDescription));
    }

    int extraMediaDescriptionCount = options->offerToReceiveAudio() + options->offerToReceiveVideo();
    for (int i = 0; i < extraMediaDescriptionCount; ++i) {
        String type = i < options->offerToReceiveAudio() ? "audio" : "video";
        RefPtr<PeerMediaDescription> mediaDescription = PeerMediaDescription::create();

        mediaDescription->setType(type);
        mediaDescription->setMode("recvonly");
        mediaDescription->setPayloads(createDefaultPayloads(type));
        mediaDescription->setRtcpMux(true);
        mediaDescription->setDtlsSetup("actpass");
        mediaDescription->setIceUfrag(m_iceUfrag);
        mediaDescription->setIcePassword(m_icePassword);

        configurationSnapshot->addMediaDescription(WTF::move(mediaDescription));
    }

    String json = MediaEndpointConfigurationConversions::toJSON(configurationSnapshot.get());
    RefPtr<RTCSessionDescription> offer = RTCSessionDescription::create("offer", toSDP(json));

    resolveCallback(*offer);
    completeQueuedOperation();
}

void MediaEndpointPeerConnection::createAnswer(const RefPtr<RTCAnswerOptions>& options, OfferAnswerResolveCallback resolveCallback, RejectCallback rejectCallback)
{
    RefPtr<RTCAnswerOptions> protectedOptions = options;
    enqueueOperation([this, protectedOptions, resolveCallback, rejectCallback]() {
        queuedCreateAnswer(protectedOptions, resolveCallback, rejectCallback);
    });
}

void MediaEndpointPeerConnection::queuedCreateAnswer(const RefPtr<RTCAnswerOptions>&, OfferAnswerResolveCallback resolveCallback, RejectCallback)
{
    RefPtr<MediaEndpointConfiguration> configurationSnapshot = m_localConfiguration ?
        MediaEndpointConfigurationConversions::fromJSON(MediaEndpointConfigurationConversions::toJSON(m_localConfiguration.get())) : MediaEndpointConfiguration::create();

    for (unsigned i = 0; i < m_remoteConfiguration->mediaDescriptions().size(); ++i) {
        RefPtr<PeerMediaDescription> remoteMediaDescription = m_remoteConfiguration->mediaDescriptions()[i];
        RefPtr<PeerMediaDescription> localMediaDescription;

        if (i < configurationSnapshot->mediaDescriptions().size())
            localMediaDescription = configurationSnapshot->mediaDescriptions()[i];
        else {
            localMediaDescription = PeerMediaDescription::create();
            localMediaDescription->setType(remoteMediaDescription->type());
            localMediaDescription->setDtlsSetup(remoteMediaDescription->dtlsSetup() == "active" ? "passive" : "active");
            localMediaDescription->setCname(m_cname);
            localMediaDescription->setIceUfrag(m_iceUfrag);
            localMediaDescription->setIcePassword(m_icePassword);

            configurationSnapshot->addMediaDescription(localMediaDescription.copyRef());
        }

        localMediaDescription->setPayloads(remoteMediaDescription->payloads());

        localMediaDescription->setRtcpMux(remoteMediaDescription->rtcpMux());

        if (!localMediaDescription->ssrcs().size())
            localMediaDescription->addSsrc(cryptographicallyRandomNumber());

        if (localMediaDescription->dtlsSetup() == "actpass")
            localMediaDescription->setDtlsSetup("passive");
    }

    Vector<RefPtr<RTCRtpSender>> senders = m_client->getSenders();
    updateMediaDescriptionsWithSenders(configurationSnapshot->mediaDescriptions(), senders);

    String json = MediaEndpointConfigurationConversions::toJSON(configurationSnapshot.get());
    RefPtr<RTCSessionDescription> answer = RTCSessionDescription::create("answer", toSDP(json));

    resolveCallback(*answer);
    completeQueuedOperation();
}

void MediaEndpointPeerConnection::setLocalDescription(RTCSessionDescription* description, PeerConnectionStates::SignalingState targetState, VoidResolveCallback resolveCallback, RejectCallback rejectCallback)
{
    RefPtr<RTCSessionDescription> protectedDescription = description;
    enqueueOperation([this, protectedDescription, targetState, resolveCallback, rejectCallback]() {
        queuedSetLocalDescription(protectedDescription.get(), targetState, resolveCallback, rejectCallback);
    });
}

void MediaEndpointPeerConnection::queuedSetLocalDescription(RTCSessionDescription* description, PeerConnectionStates::SignalingState targetState, VoidResolveCallback resolveCallback, RejectCallback rejectCallback)
{
    unsigned previousNumberOfMediaDescriptions = m_localConfiguration ? m_localConfiguration->mediaDescriptions().size() : 0;

    String json = fromSDP(description->sdp());
    m_localConfiguration = MediaEndpointConfigurationConversions::fromJSON(json);
    m_localConfigurationType = description->type();

    if (!m_localConfiguration) {
        // FIXME: Error type?
        RefPtr<DOMError> error = DOMError::create("InvalidSessionDescriptionError (unable to parse description)");
        rejectCallback(*error);
        completeQueuedOperation();
        return;
    }

    bool hasNewMediaDescriptions = m_localConfiguration->mediaDescriptions().size() > previousNumberOfMediaDescriptions;
    bool isInitiator = m_localConfigurationType == "offer";

    m_resolveSetLocalDescription = [this, targetState, resolveCallback]() mutable {
        m_client->changeSignalingState(targetState);
        resolveCallback();
        completeQueuedOperation();
    };

    if (hasNewMediaDescriptions)
        m_mediaEndpoint->prepareToReceive(m_localConfiguration.get(), isInitiator);

    if (m_remoteConfiguration)
        m_mediaEndpoint->prepareToSend(m_remoteConfiguration.get(), isInitiator);
}

RefPtr<RTCSessionDescription> MediaEndpointPeerConnection::localDescription() const
{
    if (!m_localConfiguration)
        return nullptr;

    String json = MediaEndpointConfigurationConversions::toJSON(m_localConfiguration.get());
    return RTCSessionDescription::create(m_localConfigurationType, toSDP(json));
}

static Vector<RefPtr<MediaPayload>> filterPayloads(const Vector<RefPtr<MediaPayload>>& remotePayloads, const String& type)
{
    Vector<RefPtr<MediaPayload>> defaultPayloads = createDefaultPayloads(type);
    Vector<RefPtr<MediaPayload>> filteredPayloads;

    for (auto& remotePayload : remotePayloads) {
        MediaPayload* defaultPayload = nullptr;
        for (auto& p : defaultPayloads) {
            if (p->encodingName() == remotePayload->encodingName().upper()) {
                defaultPayload = p.get();
                break;
            }
        }
        if (!defaultPayload)
            continue;

        if (defaultPayload->parameters().contains("packetizationMode") && remotePayload->parameters().contains("packetizationMode")
            && (defaultPayload->parameters().get("packetizationMode") != defaultPayload->parameters().get("packetizationMode")))
            continue;

        filteredPayloads.append(remotePayload);
    }

    return filteredPayloads;
}

void MediaEndpointPeerConnection::setRemoteDescription(RTCSessionDescription* description, PeerConnectionStates::SignalingState targetState, VoidResolveCallback resolveCallback, RejectCallback rejectCallback)
{
    RefPtr<RTCSessionDescription> protectedDescription = description;
    enqueueOperation([this, protectedDescription, targetState, resolveCallback, rejectCallback]() {
        queuedSetRemoteDescription(protectedDescription.get(), targetState, resolveCallback, rejectCallback);
    });
}

void MediaEndpointPeerConnection::queuedSetRemoteDescription(RTCSessionDescription* description, PeerConnectionStates::SignalingState targetState, VoidResolveCallback resolveCallback, RejectCallback rejectCallback)
{
    String json = fromSDP(description->sdp());
    m_remoteConfiguration = MediaEndpointConfigurationConversions::fromJSON(json);
    m_remoteConfigurationType = description->type();

    if (!m_remoteConfiguration) {
        // FIXME: Error type?
        RefPtr<DOMError> error = DOMError::create("InvalidSessionDescriptionError (unable to parse description)");
        rejectCallback(*error);
        completeQueuedOperation();
        return;
    }

    Vector<RefPtr<RTCRtpSender>> senders = m_client->getSenders();

    for (auto& mediaDescription : m_remoteConfiguration->mediaDescriptions()) {
        if (mediaDescription->type() != "audio" && mediaDescription->type() != "video")
            continue;

        mediaDescription->setPayloads(filterPayloads(mediaDescription->payloads(), mediaDescription->type()));

        RefPtr<RTCRtpSender> sender = takeFirstSenderOfType(senders, mediaDescription->type());
        if (sender)
            mediaDescription->setSource(sender->track()->source());
    }

    bool isInitiator = m_remoteConfigurationType == "answer";
    m_mediaEndpoint->prepareToSend(m_remoteConfiguration.get(), isInitiator);

    // FIXME: event firing task should update state
    m_client->changeSignalingState(targetState);

    resolveCallback();
    completeQueuedOperation();
}

RefPtr<RTCSessionDescription> MediaEndpointPeerConnection::remoteDescription() const
{
    if (!m_remoteConfiguration)
        return nullptr;

    String json = MediaEndpointConfigurationConversions::toJSON(m_remoteConfiguration.get());
    return RTCSessionDescription::create(m_remoteConfigurationType, toSDP(json));
}

void MediaEndpointPeerConnection::setConfiguration(RTCConfiguration& configuration)
{
    // FIXME: updateIce() might be renamed to setConfiguration(). It's also possible
    // that its current behavior with update deltas will change.
    m_mediaEndpoint->setConfiguration(createMediaEndpointInit(configuration));
}

void MediaEndpointPeerConnection::addIceCandidate(RTCIceCandidate* rtcCandidate, VoidResolveCallback resolveCallback, RejectCallback rejectCallback)
{
    RefPtr<RTCIceCandidate> protectedCandidate = rtcCandidate;
    enqueueOperation([this, protectedCandidate, resolveCallback, rejectCallback]() {
        queuedAddIceCandidate(protectedCandidate.get(), resolveCallback, rejectCallback);
    });
}

void MediaEndpointPeerConnection::queuedAddIceCandidate(RTCIceCandidate* rtcCandidate, VoidResolveCallback resolveCallback, RejectCallback rejectCallback)
{
    String json = iceCandidateFromSDP(rtcCandidate->candidate());
    RefPtr<IceCandidate> candidate = MediaEndpointConfigurationConversions::iceCandidateFromJSON(json);
    if (!candidate) {
        // FIXME: Error type?
        RefPtr<DOMError> error = DOMError::create("SyntaxError (malformed candidate)");
        rejectCallback(*error);
        completeQueuedOperation();
        return;
    }

    unsigned mdescIndex = rtcCandidate->sdpMLineIndex();
    if (mdescIndex >= m_remoteConfiguration->mediaDescriptions().size()) {
        // FIXME: Error type?
        RefPtr<DOMError> error = DOMError::create("InvalidSdpMlineIndex (sdpMLineIndex out of range");
        rejectCallback(*error);
        completeQueuedOperation();
        return;
    }

    PeerMediaDescription& mdesc = *m_remoteConfiguration->mediaDescriptions()[mdescIndex];
    mdesc.addIceCandidate(candidate.copyRef());

    m_mediaEndpoint->addRemoteCandidate(*candidate, mdescIndex, mdesc.iceUfrag(), mdesc.icePassword());

    resolveCallback();
    completeQueuedOperation();
}

void MediaEndpointPeerConnection::stop()
{
    m_mediaEndpoint->stop();
}

bool MediaEndpointPeerConnection::isLocalConfigurationComplete() const
{
    for (auto& mdesc : m_localConfiguration->mediaDescriptions()) {
        if (mdesc->dtlsFingerprint().isEmpty())
            return false;
        // Test: No trickle
        if (!mdesc->iceCandidateGatheringDone())
            return false;
    }

    return true;
}

MediaEndpointPeerConnection::ResolveSetLocalDescriptionResult MediaEndpointPeerConnection::maybeResolveSetLocalDescription()
{
    if (!m_resolveSetLocalDescription) {
        ASSERT(isLocalConfigurationComplete());
        return SetLocalDescriptionAlreadyResolved;
    }

    if (isLocalConfigurationComplete()) {
        m_resolveSetLocalDescription();
        m_resolveSetLocalDescription = nullptr;
        return SetLocalDescriptionResolvedSuccessfully;
    }

    return LocalConfigurationIncomplete;
}

void MediaEndpointPeerConnection::maybeDispatchGatheringDone()
{
    if (!isLocalConfigurationComplete())
        return;

    for (auto& mdesc : m_localConfiguration->mediaDescriptions()) {
        if (!mdesc->iceCandidateGatheringDone())
            return;
    }

    m_client->scheduleDispatchEvent(RTCIceCandidateEvent::create(false, false, nullptr));
}

void MediaEndpointPeerConnection::gotDtlsCertificate(unsigned mdescIndex, const String& certificate)
{
    Vector<String> certificateRows;
    Vector<uint8_t> der;

    der.reserveCapacity(certificate.length() * 3/4 + 2);
    certificate.split("\n", certificateRows);

    for (auto& row : certificateRows) {
        if (row.startsWith("-----"))
            continue;

        Vector<uint8_t> decodedRow;
        if (!base64Decode(row, decodedRow, Base64FailOnInvalidCharacterOrExcessPadding)) {
            ASSERT_NOT_REACHED();
            return;
        }
        der.appendVector(decodedRow);
    }

    std::unique_ptr<CryptoDigest> digest = CryptoDigest::create(CryptoAlgorithmIdentifier::SHA_256);
    if (!digest) {
        ASSERT_NOT_REACHED();
        return;
    }

    digest->addBytes(der.data(), der.size());
    Vector<uint8_t> fingerprintVector = digest->computeHash();

    StringBuilder fingerprint;
    for (unsigned i = 0; i < fingerprintVector.size(); ++i)
        fingerprint.append(String::format(i ? ":%02X" : "%02X", fingerprintVector[i]));

    m_localConfiguration->mediaDescriptions()[mdescIndex]->setDtlsFingerprintHashFunction("sha-256");
    m_localConfiguration->mediaDescriptions()[mdescIndex]->setDtlsFingerprint(fingerprint.toString());

    if (maybeResolveSetLocalDescription() == SetLocalDescriptionResolvedSuccessfully)
        maybeDispatchGatheringDone();
}

void MediaEndpointPeerConnection::gotIceCandidate(unsigned mdescIndex, RefPtr<IceCandidate>&& candidate)
{
    printf("-> gotIceCandidate()\n");

    ASSERT(scriptExecutionContext()->isContextThread());

    PeerMediaDescription& mdesc = *m_localConfiguration->mediaDescriptions()[mdescIndex];
    mdesc.addIceCandidate(candidate.copyRef());

    if (!candidate->address().contains(':')) { // not IPv6
        if (candidate->componentId() == 1) { // RTP
            if (mdesc.address().isEmpty() || mdesc.address() == "0.0.0.0") {
                mdesc.setAddress(candidate->address());
                mdesc.setPort(candidate->port());
            }
        } else { // RTCP
            if (mdesc.rtcpAddress().isEmpty() || !mdesc.rtcpPort()) {
                mdesc.setRtcpAddress(candidate->address());
                mdesc.setRtcpPort(candidate->port());
            }
        }
    }

    ResolveSetLocalDescriptionResult result = maybeResolveSetLocalDescription();
    if (result == SetLocalDescriptionResolvedSuccessfully)
        maybeDispatchGatheringDone();
    else if (result == SetLocalDescriptionAlreadyResolved) {
        String candidateString = MediaEndpointConfigurationConversions::iceCandidateToJSON(candidate.get());
        String sdpFragment = iceCandidateToSDP(candidateString);
        RefPtr<RTCIceCandidate> iceCandidate = RTCIceCandidate::create(sdpFragment, "", mdescIndex);
        m_client->scheduleDispatchEvent(RTCIceCandidateEvent::create(false, false, WTF::move(iceCandidate)));
    }
}

void MediaEndpointPeerConnection::doneGatheringCandidates(unsigned mdescIndex)
{
    printf("-> doneGatheringCandidates()\n");

    ASSERT(scriptExecutionContext()->isContextThread());

    m_localConfiguration->mediaDescriptions()[mdescIndex]->setIceCandidateGatheringDone(true);
    // Test: No trickle
    maybeResolveSetLocalDescription();
    maybeDispatchGatheringDone();
}

void MediaEndpointPeerConnection::gotRemoteSource(unsigned mdescIndex, RefPtr<RealtimeMediaSource>&& source)
{
    if (m_client->internalSignalingState() == SignalingState::Closed)
        return;

    if (mdescIndex >= m_remoteConfiguration->mediaDescriptions().size()) {
        printf("Warning: No remote configuration for incoming source.\n");
        return;
    }

    PeerMediaDescription& mediaDescription = *m_remoteConfiguration->mediaDescriptions()[mdescIndex];
    String trackId = mediaDescription.mediaStreamTrackId();

    if (trackId.isEmpty()) {
        // Non WebRTC media description (e.g. legacy)
        trackId = createCanonicalUUIDString();
    }

    // FIXME: track should be set to muted (not supported yet)
    // FIXME: MediaStream handling not implemented

    RefPtr<MediaStreamTrackPrivate> trackPrivate = MediaStreamTrackPrivate::create(WTF::move(source), trackId);
    RefPtr<MediaStreamTrack> track = MediaStreamTrack::create(*m_client->context(), *trackPrivate);
    RefPtr<RTCRtpReceiver> receiver = RTCRtpReceiver::create(track.copyRef());

    m_client->scheduleDispatchEvent(RTCTrackEvent::create(eventNames().trackEvent, false, false, WTF::move(receiver), WTF::move(track)));
}

String MediaEndpointPeerConnection::toSDP(const String& json) const
{
    return sdpConversion("toSDP", json);
}

String MediaEndpointPeerConnection::fromSDP(const String& sdp) const
{
    return sdpConversion("fromSDP", sdp);
}

String MediaEndpointPeerConnection::iceCandidateToSDP(const String& json) const
{
    return sdpConversion("iceCandidateToSDP", json);
}

String MediaEndpointPeerConnection::iceCandidateFromSDP(const String& sdpFragment) const
{
    return sdpConversion("iceCandidateFromSDP", sdpFragment);
}

String MediaEndpointPeerConnection::sdpConversion(const String& functionName, const String& argument) const
{
    Document* document = downcast<Document>(m_client->context());

    if (!m_isolatedWorld)
        m_isolatedWorld = DOMWrapperWorld::create(JSDOMWindow::commonVM());

    ScriptController& scriptController = document->frame()->script();
    JSDOMGlobalObject* globalObject = JSC::jsCast<JSDOMGlobalObject*>(scriptController.globalObject(*m_isolatedWorld));
    JSC::ExecState* exec = globalObject->globalExec();
    JSC::JSLockHolder lock(exec);

    JSC::JSValue probeFunctionValue = globalObject->get(exec, JSC::Identifier::fromString(exec, "toSDP"));
    if (!probeFunctionValue.isFunction()) {
        URL scriptURL;
        scriptController.evaluateInWorld(ScriptSourceCode(SDPScriptResource::getString(), scriptURL), *m_isolatedWorld);
        if (exec->hadException()) {
            exec->clearException();
            return emptyString();
        }
    }

    JSC::JSValue functionValue = globalObject->get(exec, JSC::Identifier::fromString(exec, functionName));
    if (!functionValue.isFunction())
        return emptyString();

    JSC::JSObject* function = functionValue.toObject(exec);
    JSC::CallData callData;
    JSC::CallType callType = function->methodTable()->getCallData(function, callData);
    if (callType == JSC::CallTypeNone)
        return emptyString();

    JSC::MarkedArgumentBuffer argList;
    argList.append(JSC::jsString(exec, argument));

    JSC::JSValue result = JSC::call(exec, function, callType, callData, globalObject, argList);
    if (exec->hadException()) {
        printf("sdpConversion: js function (%s) threw\n", functionName.ascii().data());
        exec->clearException();
        return emptyString();
    }

    return result.isString() ? result.getString(exec) : emptyString();
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)
