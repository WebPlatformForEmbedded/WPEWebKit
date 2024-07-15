/*
 * Copyright (C) 2017 Igalia Inc. All rights reserved.
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

#if USE(LIBWEBRTC) && USE(GSTREAMER)
#include "RealtimeIncomingAudioSourceLibWebRTC.h"

#include "LibWebRTCAudioFormat.h"
#include "gstreamer/GStreamerAudioData.h"
#include "gstreamer/GStreamerAudioStreamDescription.h"

namespace WebCore {

Ref<RealtimeIncomingAudioSource> RealtimeIncomingAudioSource::create(rtc::scoped_refptr<webrtc::AudioTrackInterface>&& audioTrack, String&& audioTrackId)
{
    auto source = RealtimeIncomingAudioSourceLibWebRTC::create(WTFMove(audioTrack), WTFMove(audioTrackId));
    source->start();
    return source;
}

Ref<RealtimeIncomingAudioSourceLibWebRTC> RealtimeIncomingAudioSourceLibWebRTC::create(rtc::scoped_refptr<webrtc::AudioTrackInterface>&& audioTrack, String&& audioTrackId)
{
    return adoptRef(*new RealtimeIncomingAudioSourceLibWebRTC(WTFMove(audioTrack), WTFMove(audioTrackId)));
}

RealtimeIncomingAudioSourceLibWebRTC::RealtimeIncomingAudioSourceLibWebRTC(rtc::scoped_refptr<webrtc::AudioTrackInterface>&& audioTrack, String&& audioTrackId)
    : RealtimeIncomingAudioSource(WTFMove(audioTrack), WTFMove(audioTrackId))
{
}

void* convertMonoToStereo(const void* audioData, size_t numberOfSamples) {
    uint16_t* result = ((uint16_t*)g_malloc(numberOfSamples * 4));
    for (size_t i = 0; i < numberOfSamples; i++) {
        result[i*2] = result[i*2+1] = ((uint16_t*)audioData)[i]; // same value for left and right channel
    }
    return result;
}

void RealtimeIncomingAudioSourceLibWebRTC::OnData(const void* audioData, int, int sampleRate, size_t numberOfChannels, size_t numberOfFrames)
{
    GstAudioInfo info;
    GstAudioFormat format = gst_audio_format_build_integer(
        LibWebRTCAudioFormat::isSigned,
        LibWebRTCAudioFormat::isBigEndian ? G_BIG_ENDIAN : G_LITTLE_ENDIAN,
        LibWebRTCAudioFormat::sampleSize,
        LibWebRTCAudioFormat::sampleSize);

    GRefPtr<GstBuffer> buffer;
    if (numberOfChannels == 1 && LibWebRTCAudioFormat::sampleSize == 16) {
        numberOfChannels = 2;
        audioData = convertMonoToStereo(audioData, numberOfFrames);
        //audioData will be g_freed when the pipeline is done with the buffer
        buffer = adoptGRef(gst_buffer_new_wrapped(const_cast<gpointer>(audioData), numberOfFrames * 4));
    }
    gst_audio_info_set_format(&info, format, sampleRate, numberOfChannels, NULL);

    if (!buffer) {
        auto bufferSize = GST_AUDIO_INFO_BPF(&info) * numberOfFrames;
        buffer = adoptGRef(gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, const_cast<gpointer>(audioData), bufferSize, 0, bufferSize, nullptr, nullptr));
    }
    auto caps = adoptGRef(gst_audio_info_to_caps(&info));
    auto sample = adoptGRef(gst_sample_new(buffer.get(), caps.get(), nullptr, nullptr));
    GStreamerAudioData data(WTFMove(sample), info);
    MediaTime mediaTime((m_numberOfFrames * G_USEC_PER_SEC) / sampleRate, G_USEC_PER_SEC);
    audioSamplesAvailable(mediaTime, data, GStreamerAudioStreamDescription(info), numberOfFrames);

    m_numberOfFrames += numberOfFrames;
}
}

#endif // USE(LIBWEBRTC)
