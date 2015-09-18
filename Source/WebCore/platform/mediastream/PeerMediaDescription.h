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

#ifndef PeerMediaDescription_h
#define PeerMediaDescription_h

#if ENABLE(MEDIA_STREAM)

#include "IceCandidate.h"
#include "MediaPayload.h"
#include "RealtimeMediaSource.h"
#include <wtf/RefCounted.h>
#include <wtf/Vector.h>

namespace WebCore {

class PeerMediaDescription : public RefCounted<PeerMediaDescription> {
public:
    static RefPtr<PeerMediaDescription> create()
    {
        return adoptRef(new PeerMediaDescription());
    }
    virtual ~PeerMediaDescription() { }

    const String& type() const { return m_type; }
    void setType(const String& type) { m_type = type; }

    unsigned short port() const { return m_port; }
    void setPort(unsigned short port) { m_port = port; }

    const String& address() const { return m_address; }
    void setAddress(const String& address) { m_address = address; }

    const String& mode() const { return m_mode; }
    void setMode(const String& mode) { m_mode = mode; }

    const Vector<RefPtr<MediaPayload>>& payloads() const { return m_payloads; }
    void addPayload(RefPtr<MediaPayload>&& payload) { m_payloads.append(WTF::move(payload)); }
    void setPayloads(Vector<RefPtr<MediaPayload>>&& payloads) { m_payloads = payloads; }
    void setPayloads(const Vector<RefPtr<MediaPayload>>& payloads) { m_payloads = payloads; }

    bool rtcpMux() const { return m_rtcpMux; }
    void setRtcpMux(bool rtcpMux) { m_rtcpMux = rtcpMux; }

    const String& rtcpAddress() const { return m_rtcpAddress; }
    void setRtcpAddress(const String& rtcpAddress) { m_rtcpAddress = rtcpAddress; }

    unsigned short rtcpPort() const { return m_rtcpPort; }
    void setRtcpPort(unsigned short rtcpPort) { m_rtcpPort = rtcpPort; }

    const String& mediaStreamId() const { return m_mediaStreamId; }
    void setMediaStreamId(const String& mediaStreamId) { m_mediaStreamId = mediaStreamId; }

    const String& mediaStreamTrackId() const { return m_mediaStreamTrackId; }
    void setMediaStreamTrackId(const String& mediaStreamTrackId) { m_mediaStreamTrackId = mediaStreamTrackId; }

    const String& dtlsSetup() const { return m_dtlsSetup; }
    void setDtlsSetup(const String& dtlsSetup) { m_dtlsSetup = dtlsSetup; }

    const String& dtlsFingerprintHashFunction() const { return m_dtlsFingerprintHashFunction; }
    void setDtlsFingerprintHashFunction(const String& dtlsFingerprintHashFunction) { m_dtlsFingerprintHashFunction = dtlsFingerprintHashFunction; }

    const String& dtlsFingerprint() const { return m_dtlsFingerprint; }
    void setDtlsFingerprint(const String& dtlsFingerprint) { m_dtlsFingerprint = dtlsFingerprint; }

    const String& cname() const { return m_cname; }
    void setCname(const String& cname) { m_cname = cname; }

    const Vector<unsigned>& ssrcs() const { return m_ssrcs; }
    void addSsrc(unsigned ssrc) { m_ssrcs.append(ssrc); }
    void clearSsrcs() { m_ssrcs.clear(); }

    const String& iceUfrag() const { return m_iceUfrag; }
    void setIceUfrag(const String& iceUfrag) { m_iceUfrag = iceUfrag; }

    const String& icePassword() const { return m_icePassword; }
    void setIcePassword(const String& icePassword) { m_icePassword = icePassword; }

    const Vector<RefPtr<IceCandidate>>& iceCandidates() const { return m_iceCandidates; }
    void addIceCandidate(RefPtr<IceCandidate>&& candidate) { m_iceCandidates.append(WTF::move(candidate)); }

    bool iceCandidateGatheringDone() const { return m_iceCandidateGatheringDone; }
    void setIceCandidateGatheringDone(bool iceCandidateGatheringDone) { m_iceCandidateGatheringDone = iceCandidateGatheringDone; }

    RealtimeMediaSource* source() const { return m_source.get(); }
    void setSource(RefPtr<RealtimeMediaSource>&& source) { m_source = source; }

private:
    PeerMediaDescription()
        : m_port(0)
        , m_rtcpMux(false)
        , m_iceCandidateGatheringDone(false)
        , m_source(nullptr)
    { }

    String m_type;
    unsigned short m_port;
    String m_address;
    String m_mode;

    Vector<RefPtr<MediaPayload>> m_payloads;

    bool m_rtcpMux;
    String m_rtcpAddress;
    unsigned short m_rtcpPort;

    String m_mediaStreamId;
    String m_mediaStreamTrackId;

    String m_dtlsSetup;
    String m_dtlsFingerprintHashFunction;
    String m_dtlsFingerprint;

    Vector<unsigned> m_ssrcs;
    String m_cname;

    String m_iceUfrag;
    String m_icePassword;
    Vector<RefPtr<IceCandidate>> m_iceCandidates;
    bool m_iceCandidateGatheringDone;

    RefPtr<RealtimeMediaSource> m_source;
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)

#endif // PeerMediaDescription_h
