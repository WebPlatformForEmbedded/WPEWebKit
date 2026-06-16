/*
 * Copyright (C) 2018 Metrological Group B.V.
 * Copyright (C) 2018 Igalia S.L. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * aint with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#if USE(LIBWEBRTC) && USE(GSTREAMER)

#include "LibWebRTCMacros.h"
#include "api/video_codecs/video_decoder_factory.h"
#include <wtf/Forward.h>
#include <wtf/ThreadSafeRefCounted.h>

namespace WebCore {

class GStreamerVideoDecoderFactory : public webrtc::VideoDecoderFactory {
    WTF_MAKE_FAST_ALLOCATED;
public:
    GStreamerVideoDecoderFactory(bool isSupportingVP9Profile0, bool isSupportingVP9Profile2);

    // Used to report rendering errors on the playback pipeline. Useful when the decoding pipeline
    // managed by the VideoDecoder is just a passthrough that provides encoded frames with the
    // expectation that actual decoding happens on a playback pipeline (beyond the reach of
    // VideoDecoder). When the data is corrupt/missing and the decoding/rendering errors happen
    // there, we need a way to request a sync frame to WebRTC. This observer exists to report that
    // condition, but internals of the GStreamer-specific VideoDecoder can't be exposed in this
    // header. An actual (private) RenderErrorObserver will implement this interface and do
    // the job wihtout exposing internals in this header.
    class RenderErrorObserverInterface : public ThreadSafeRefCounted<RenderErrorObserverInterface>
    {
    public:
        // Error is transfer-none (owned by the caller).
        virtual void onRenderingError(GError* error) = 0;
        virtual ~RenderErrorObserverInterface() = default;
    };

private:
    std::unique_ptr<webrtc::VideoDecoder> CreateVideoDecoder(const webrtc::SdpVideoFormat&) final;
    std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const final;

    bool m_isSupportingVP9Profile0;
    bool m_isSupportingVP9Profile2;
};
}

#endif
