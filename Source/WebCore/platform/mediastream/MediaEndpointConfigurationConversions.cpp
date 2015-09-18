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
#include "MediaEndpointConfigurationConversions.h"

#include "inspector/InspectorValues.h"

using namespace JSC;
using namespace Inspector;

namespace WebCore {

namespace MediaEndpointConfigurationConversions {

// Note that MediaEndpointConfiguration is a "flatter" structure that the JSON representation. For
// example, the JSON representation has an "ice" object which collects a set of properties under a
// namespace. MediaEndpointConfiguration has "ice"-prefixes on the corresponding properties.

static RefPtr<InspectorObject> createCandidateObject(IceCandidate* candidate)
{
    RefPtr<InspectorObject> candidateObject = InspectorObject::create();

    candidateObject->setString(ASCIILiteral("type"), candidate->type());
    candidateObject->setString(ASCIILiteral("foundation"), candidate->foundation());
    candidateObject->setInteger(ASCIILiteral("componentId"), candidate->componentId());
    candidateObject->setString(ASCIILiteral("transport"), candidate->transport());
    candidateObject->setInteger(ASCIILiteral("priority"), candidate->priority());
    candidateObject->setString(ASCIILiteral("address"), candidate->address());
    candidateObject->setInteger(ASCIILiteral("port"), candidate->port());
    if (!candidate->tcpType().isEmpty())
        candidateObject->setString(ASCIILiteral("tcpType"), candidate->tcpType());
    if (candidate->type().upper() != "HOST") {
        candidateObject->setString(ASCIILiteral("relatedAddress"), candidate->relatedAddress());
        candidateObject->setInteger(ASCIILiteral("relatedPort"), candidate->relatedPort());
    }

    return candidateObject;
}

static RefPtr<IceCandidate> createCandidate(InspectorObject* candidateObject)
{
    RefPtr<IceCandidate> candidate = IceCandidate::create();
    String stringValue;
    unsigned intValue;

    if (candidateObject->getString(ASCIILiteral("type"), stringValue))
        candidate->setType(stringValue);

    if (candidateObject->getString(ASCIILiteral("foundation"), stringValue))
        candidate->setFoundation(stringValue);

    if (candidateObject->getInteger(ASCIILiteral("componentId"), intValue))
        candidate->setComponentId(intValue);

    if (candidateObject->getString(ASCIILiteral("transport"), stringValue))
        candidate->setTransport(stringValue);

    if (candidateObject->getInteger(ASCIILiteral("priority"), intValue))
        candidate->setPriority(intValue);

    if (candidateObject->getString(ASCIILiteral("address"), stringValue))
        candidate->setAddress(stringValue);

    if (candidateObject->getInteger(ASCIILiteral("port"), intValue))
        candidate->setPort(intValue);

    if (candidateObject->getString(ASCIILiteral("tcpType"), stringValue))
        candidate->setTcpType(stringValue);

    if (candidateObject->getString(ASCIILiteral("relatedAddress"), stringValue))
        candidate->setRelatedAddress(stringValue);

    if (candidateObject->getInteger(ASCIILiteral("relatedPort"), intValue))
        candidate->setRelatedPort(intValue);

    return candidate;
}

RefPtr<MediaEndpointConfiguration> fromJSON(const String& json)
{
    RefPtr<InspectorValue> value;
    if (!InspectorValue::parseJSON(json, value))
        return nullptr;

    RefPtr<InspectorObject> object;
    if (!value->asObject(object))
        return nullptr;

    RefPtr<MediaEndpointConfiguration> configuration = MediaEndpointConfiguration::create();

    String stringValue;
    unsigned intValue;
    unsigned long longValue;
    bool boolValue;

    RefPtr<InspectorObject> originatorObject = InspectorObject::create();
    if (object->getObject(ASCIILiteral("originator"), originatorObject)) {
        if (originatorObject->getInteger(ASCIILiteral("sessionId"), longValue))
            configuration->setSessionId(longValue);
        if (originatorObject->getInteger(ASCIILiteral("sessionVersion"), intValue))
            configuration->setSessionVersion(intValue);
    }

    RefPtr<InspectorArray> mediaDescriptionsArray = InspectorArray::create();
    object->getArray(ASCIILiteral("mediaDescriptions"), mediaDescriptionsArray);

    for (unsigned i = 0; i < mediaDescriptionsArray->length(); ++i) {
        RefPtr<InspectorObject> mdescObject = InspectorObject::create();
        mediaDescriptionsArray->get(i)->asObject(mdescObject);

        RefPtr<PeerMediaDescription> mdesc = PeerMediaDescription::create();

        if (mdescObject->getString(ASCIILiteral("type"), stringValue))
            mdesc->setType(stringValue);

        if (mdescObject->getInteger(ASCIILiteral("port"), intValue))
            mdesc->setPort(intValue);

        if (mdescObject->getString(ASCIILiteral("address"), stringValue))
            mdesc->setAddress(stringValue);

        if (mdescObject->getString(ASCIILiteral("mode"), stringValue))
            mdesc->setMode(stringValue);

        RefPtr<InspectorArray> payloadsArray = InspectorArray::create();
        mdescObject->getArray(ASCIILiteral("payloads"), payloadsArray);

        for (unsigned j = 0; j < payloadsArray->length(); ++j) {
            RefPtr<InspectorObject> payloadsObject = InspectorObject::create();
            payloadsArray->get(j)->asObject(payloadsObject);

            RefPtr<MediaPayload> payload = MediaPayload::create();

            if (payloadsObject->getInteger(ASCIILiteral("type"), intValue))
                payload->setType(intValue);

            if (payloadsObject->getString(ASCIILiteral("encodingName"), stringValue))
                payload->setEncodingName(stringValue);

            if (payloadsObject->getInteger(ASCIILiteral("clockRate"), intValue))
                payload->setClockRate(intValue);

            if (payloadsObject->getInteger(ASCIILiteral("channels"), intValue))
                payload->setChannels(intValue);

            if (payloadsObject->getBoolean(ASCIILiteral("ccmfir"), boolValue))
                payload->setCcmfir(boolValue);

            if (payloadsObject->getBoolean(ASCIILiteral("nackpli"), boolValue))
                payload->setNackpli(boolValue);

            if (payloadsObject->getBoolean(ASCIILiteral("nack"), boolValue))
                payload->setNack(boolValue);

            RefPtr<InspectorObject> parametersObject = InspectorObject::create();
            if (payloadsObject->getObject(ASCIILiteral("parameters"), parametersObject)) {
                if (parametersObject->getInteger(ASCIILiteral("packetizationMode"), intValue))
                    payload->addParameter("packetizationMode", intValue);

                if (parametersObject->getInteger(ASCIILiteral("apt"), intValue))
                    payload->addParameter("apt", intValue);

                if (parametersObject->getInteger(ASCIILiteral("rtxTime"), intValue))
                    payload->addParameter("rtxTime", intValue);
            }

            mdesc->addPayload(WTF::move(payload));
        }

        RefPtr<InspectorObject> rtcpObject = InspectorObject::create();
        if (mdescObject->getObject(ASCIILiteral("rtcp"), rtcpObject)) {
            if (rtcpObject->getBoolean(ASCIILiteral("mux"), boolValue))
                mdesc->setRtcpMux(boolValue);

            if (rtcpObject->getString(ASCIILiteral("rtcpAddress"), stringValue))
                mdesc->setRtcpAddress(stringValue);

            if (rtcpObject->getInteger(ASCIILiteral("rtcpPort"), intValue))
                mdesc->setRtcpPort(intValue);
        }

        if (mdescObject->getString(ASCIILiteral("mediaStreamId"), stringValue))
            mdesc->setMediaStreamId(stringValue);

        if (mdescObject->getString(ASCIILiteral("mediaStreamTrackId"), stringValue))
            mdesc->setMediaStreamTrackId(stringValue);

        RefPtr<InspectorObject> dtlsObject = InspectorObject::create();
        if (mdescObject->getObject(ASCIILiteral("dtls"), dtlsObject)) {
            if (dtlsObject->getString(ASCIILiteral("setup"), stringValue))
                mdesc->setDtlsSetup(stringValue);

            if (dtlsObject->getString(ASCIILiteral("fingerprintHashFunction"), stringValue))
                mdesc->setDtlsFingerprintHashFunction(stringValue);

            if (dtlsObject->getString(ASCIILiteral("fingerprint"), stringValue))
                mdesc->setDtlsFingerprint(stringValue);
        }

        RefPtr<InspectorArray> ssrcsArray = InspectorArray::create();
        mdescObject->getArray(ASCIILiteral("ssrcs"), ssrcsArray);

        for (unsigned j = 0; j < ssrcsArray->length(); ++j) {
            ssrcsArray->get(j)->asInteger(intValue);
            mdesc->addSsrc(intValue);
        }

        if (mdescObject->getString(ASCIILiteral("cname"), stringValue))
            mdesc->setCname(stringValue);

        RefPtr<InspectorObject> iceObject = InspectorObject::create();
        if (mdescObject->getObject(ASCIILiteral("ice"), iceObject)) {
            if (iceObject->getString(ASCIILiteral("ufrag"), stringValue))
                mdesc->setIceUfrag(stringValue);

            if (iceObject->getString(ASCIILiteral("password"), stringValue))
                mdesc->setIcePassword(stringValue);

            RefPtr<InspectorArray> candidatesArray = InspectorArray::create();
            iceObject->getArray(ASCIILiteral("candidates"), candidatesArray);

            for (unsigned j = 0; j < candidatesArray->length(); ++j) {
                RefPtr<InspectorObject> candidateObject = InspectorObject::create();
                candidatesArray->get(j)->asObject(candidateObject);

                mdesc->addIceCandidate(createCandidate(candidateObject.get()));
            }
        }

        configuration->addMediaDescription(WTF::move(mdesc));
    }

    return configuration;
}

RefPtr<IceCandidate> iceCandidateFromJSON(const String& json)
{
    RefPtr<InspectorValue> value;
    if (!InspectorValue::parseJSON(json, value))
        return nullptr;

    RefPtr<InspectorObject> candidateObject;
    if (!value->asObject(candidateObject))
        return nullptr;

    return createCandidate(candidateObject.get());
}

String toJSON(MediaEndpointConfiguration* configuration)
{
    RefPtr<InspectorObject> object = InspectorObject::create();

    RefPtr<InspectorObject> originatorObject = InspectorObject::create();
    originatorObject->setDouble(ASCIILiteral("sessionId"), configuration->sessionId());
    originatorObject->setInteger(ASCIILiteral("sessionVersion"), configuration->sessionVersion());
    object->setObject(ASCIILiteral("originator"), originatorObject);

    RefPtr<InspectorArray> mediaDescriptionsArray = InspectorArray::create();

    for (const RefPtr<PeerMediaDescription>& mdesc : configuration->mediaDescriptions()) {
        RefPtr<InspectorObject> mdescObject = InspectorObject::create();

        mdescObject->setString(ASCIILiteral("type"), mdesc->type());
        mdescObject->setInteger(ASCIILiteral("port"), mdesc->port());
        mdescObject->setString(ASCIILiteral("address"), mdesc->address());
        mdescObject->setString(ASCIILiteral("mode"), mdesc->mode());

        RefPtr<InspectorArray> payloadsArray = InspectorArray::create();

        for (RefPtr<MediaPayload> payload : mdesc->payloads()) {
            RefPtr<InspectorObject> payloadObject = InspectorObject::create();

            payloadObject->setInteger(ASCIILiteral("type"), payload->type());
            payloadObject->setString(ASCIILiteral("encodingName"), payload->encodingName());
            payloadObject->setInteger(ASCIILiteral("clockRate"), payload->clockRate());
            payloadObject->setInteger(ASCIILiteral("channels"), payload->channels());
            payloadObject->setBoolean(ASCIILiteral("ccmfir"), payload->ccmfir());
            payloadObject->setBoolean(ASCIILiteral("nackpli"), payload->nackpli());
            payloadObject->setBoolean(ASCIILiteral("nack"), payload->nack());

            if (!payload->parameters().isEmpty()) {
                RefPtr<InspectorObject> parametersObject = InspectorObject::create();

                for (auto& name : payload->parameters().keys())
                    parametersObject->setInteger(name, payload->parameters().get(name));

                payloadObject->setObject(ASCIILiteral("parameters"), parametersObject);
            }

            payloadsArray->pushObject(payloadObject);
        }
        mdescObject->setArray(ASCIILiteral("payloads"), payloadsArray);

        RefPtr<InspectorObject> rtcpObject = InspectorObject::create();
        rtcpObject->setBoolean(ASCIILiteral("mux"), mdesc->rtcpMux());
        rtcpObject->setString(ASCIILiteral("address"), mdesc->rtcpAddress());
        rtcpObject->setInteger(ASCIILiteral("port"), mdesc->rtcpPort());
        mdescObject->setObject(ASCIILiteral("rtcp"), rtcpObject);

        mdescObject->setString(ASCIILiteral("mediaStreamId"), mdesc->mediaStreamId());
        mdescObject->setString(ASCIILiteral("mediaStreamTrackId"), mdesc->mediaStreamTrackId());

        RefPtr<InspectorObject> dtlsObject = InspectorObject::create();
        dtlsObject->setString(ASCIILiteral("setup"), mdesc->dtlsSetup());
        dtlsObject->setString(ASCIILiteral("fingerprintHashFunction"), mdesc->dtlsFingerprintHashFunction());
        dtlsObject->setString(ASCIILiteral("fingerprint"), mdesc->dtlsFingerprint());
        mdescObject->setObject(ASCIILiteral("dtls"), dtlsObject);

        RefPtr<InspectorArray> ssrcsArray = InspectorArray::create();

        for (auto ssrc : mdesc->ssrcs())
            ssrcsArray->pushDouble(ssrc);
        mdescObject->setArray(ASCIILiteral("ssrcs"), ssrcsArray);

        mdescObject->setString(ASCIILiteral("cname"), mdesc->cname());

        RefPtr<InspectorObject> iceObject = InspectorObject::create();
        iceObject->setString(ASCIILiteral("ufrag"), mdesc->iceUfrag());
        iceObject->setString(ASCIILiteral("password"), mdesc->icePassword());

        RefPtr<InspectorArray> candidatesArray = InspectorArray::create();

        for (RefPtr<IceCandidate> candidate : mdesc->iceCandidates())
            candidatesArray->pushObject(createCandidateObject(candidate.get()));

        iceObject->setArray(ASCIILiteral("candidates"), candidatesArray);
        mdescObject->setObject(ASCIILiteral("ice"), iceObject);

        mediaDescriptionsArray->pushObject(mdescObject);
    }
    object->setArray(ASCIILiteral("mediaDescriptions"), mediaDescriptionsArray);

    return object->toJSONString();
}

String iceCandidateToJSON(IceCandidate* candidate)
{
    return createCandidateObject(candidate)->toJSONString();
}

}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)
