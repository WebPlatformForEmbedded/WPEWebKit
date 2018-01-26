/*
 * Copyright (C) 2017 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "LibWebRTCMacros.h"
#include <wtf/Forward.h>
#include <wtf/UniqueRef.h>
#include <gst/gst.h>

#if USE(LIBWEBRTC)

#if USE(GSTREAMER)
#include "GStreamerVideoEncoderFactory.h"
#include "GStreamerVideoDecoderFactory.h"
#endif

#include <webrtc/api/peerconnectioninterface.h>
#include <webrtc/base/scoped_ref_ptr.h>
#include "LibWebRTCAudioModule.h"
#include "webrtc/media/engine/webrtcvideoencoderfactory.h"
#include "webrtc/media/engine/webrtcvideodecoderfactory.h"

namespace rtc {
class NetworkManager;
class PacketSocketFactory;
}

namespace webrtc {
class PeerConnectionFactoryInterface;
}
#endif

namespace WebCore {

#if PLATFORM(COCOA)
class VideoToolboxVideoDecoderFactory;
class VideoToolboxVideoEncoderFactory;
#endif

class WEBCORE_EXPORT LibWebRTCProvider {
public:
    static UniqueRef<LibWebRTCProvider> create();

    LibWebRTCProvider() = default;
    virtual ~LibWebRTCProvider() = default;

    static bool webRTCAvailable();

    void setActive(bool);

#if USE(LIBWEBRTC)
    WEBCORE_EXPORT virtual rtc::scoped_refptr<webrtc::PeerConnectionInterface> createPeerConnection(webrtc::PeerConnectionObserver&, webrtc::PeerConnectionInterface::RTCConfiguration&&);

    virtual webrtc::PeerConnectionFactoryInterface* factory();

    // FIXME: Make these methods not static.
    static WEBCORE_EXPORT void callOnWebRTCNetworkThread(Function<void()>&&);
    static WEBCORE_EXPORT void callOnWebRTCSignalingThread(Function<void()>&&);

    // Used for mock testing
    void setPeerConnectionFactory(rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>&&);

    void disableEnumeratingAllNetworkInterfaces() { m_enableEnumeratingAllNetworkInterfaces = false; }
    void enableEnumeratingAllNetworkInterfaces() { m_enableEnumeratingAllNetworkInterfaces = true; }

protected:
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> createPeerConnection(webrtc::PeerConnectionObserver&, rtc::NetworkManager&, rtc::PacketSocketFactory&, webrtc::PeerConnectionInterface::RTCConfiguration&&);

    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> createPeerConnectionFactory(rtc::Thread* networkThread, rtc::Thread* signalingThread, LibWebRTCAudioModule*);
    virtual std::unique_ptr<cricket::WebRtcVideoDecoderFactory> createDecoderFactory() { return nullptr; }
    virtual std::unique_ptr<cricket::WebRtcVideoEncoderFactory> createEncoderFactory() { return nullptr; }

    bool m_enableEnumeratingAllNetworkInterfaces { false };
    // FIXME: Remove m_useNetworkThreadWithSocketServer member variable and make it a global.
    bool m_useNetworkThreadWithSocketServer { false };

    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> m_factory;

#if PLATFORM(COCOA)
    VideoToolboxVideoDecoderFactory* m_decoderFactory { nullptr };
    VideoToolboxVideoEncoderFactory* m_encoderFactory { nullptr };
#endif
    GStreamerVideoDecoderFactory* m_decoderFactory { nullptr };
    GStreamerVideoEncoderFactory* m_encoderFactory { nullptr };
#endif
};

} // namespace WebCore
