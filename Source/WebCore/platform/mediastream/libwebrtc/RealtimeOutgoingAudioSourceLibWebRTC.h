/*
 * Copyright (C) 2017 Igalia S.L
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

#if USE(LIBWEBRTC)

#include "RealtimeOutgoingAudioSource.h"

#include "LibWebRTCAudioFormat.h"
#include "LibWebRTCMacros.h"
#include "LibWebRTCProvider.h"
#include "webrtc/api/mediastreaminterface.h"
#include "webrtc/modules/audio_device/include/audio_device_defines.h"
#include "gstreamer/GStreamerAudioStreamDescription.h"

#include <wtf/glib/GMutexLocker.h>
#include <gst/audio/audio.h>

namespace WebCore {

class RealtimeOutgoingAudioSourceLibWebRTC final : public RealtimeOutgoingAudioSource {
public:
    static Ref<RealtimeOutgoingAudioSourceLibWebRTC> create(Ref<MediaStreamTrackPrivate>&& audioTrackPrivate)
    {
        return adoptRef(*new RealtimeOutgoingAudioSourceLibWebRTC(WTFMove(audioTrackPrivate)));
    }

private:
    explicit RealtimeOutgoingAudioSourceLibWebRTC(Ref<MediaStreamTrackPrivate>&&);
    ~RealtimeOutgoingAudioSourceLibWebRTC();

    void audioSamplesAvailable(const MediaTime&, const PlatformAudioData&, const AudioStreamDescription&, size_t) final;

    bool isReachingBufferedAudioDataHighLimit() final;
    bool isReachingBufferedAudioDataLowLimit() final;
    bool hasBufferedEnoughData() final;

    void pullAudioData() final;

    void handleMutedIfNeeded() final;
    void sendSilence() final;

    GstAudioConverter * m_sampleConverter;
    std::unique_ptr<GStreamerAudioStreamDescription> m_inputStreamDescription;
    std::unique_ptr<GStreamerAudioStreamDescription> m_outputStreamDescription;

    GMutex m_adapterMutex;
    GRefPtr<GstAdapter> m_Adapter;
};

} // namespace WebCore

#endif // USE(LIBWEBRTC)
