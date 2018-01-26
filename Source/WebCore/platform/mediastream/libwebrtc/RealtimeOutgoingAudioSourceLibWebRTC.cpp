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

#include "config.h"
#if USE(LIBWEBRTC)

#include "LibWebRTCAudioFormat.h"
#include "gstreamer/GStreamerAudioData.h"
#include "RealtimeOutgoingAudioSourceLibWebRTC.h"

namespace WebCore {

RealtimeOutgoingAudioSourceLibWebRTC::RealtimeOutgoingAudioSourceLibWebRTC(Ref<MediaStreamTrackPrivate>&& audioSource)
    : RealtimeOutgoingAudioSource(WTFMove(audioSource))
{
      g_mutex_init(&m_adapterMutex);
      m_Adapter = gst_adapter_new(),
      m_sampleConverter = nullptr;
}

RealtimeOutgoingAudioSourceLibWebRTC::~RealtimeOutgoingAudioSourceLibWebRTC() {
    g_mutex_clear(&m_adapterMutex);
}

Ref<RealtimeOutgoingAudioSource> RealtimeOutgoingAudioSource::create(Ref<MediaStreamTrackPrivate>&& audioSource)
{
    return RealtimeOutgoingAudioSourceLibWebRTC::create(WTFMove(audioSource));
}

static inline std::unique_ptr<GStreamerAudioStreamDescription> libwebrtcAudioFormat(int sampleRate,
    size_t channelCount)
{
    GstAudioFormat format = gst_audio_format_build_integer (
        LibWebRTCAudioFormat::isSigned,
        LibWebRTCAudioFormat::isBigEndian ? G_BIG_ENDIAN : G_LITTLE_ENDIAN,
        LibWebRTCAudioFormat::sampleSize,
        LibWebRTCAudioFormat::sampleSize);

    GstAudioInfo info;

    size_t libWebRTCChannelCount = channelCount >= 2 ? 2 : channelCount;
    gst_audio_info_set_format (&info, format, sampleRate, libWebRTCChannelCount, NULL);

    return std::unique_ptr<GStreamerAudioStreamDescription>(new GStreamerAudioStreamDescription(info));
}

void RealtimeOutgoingAudioSourceLibWebRTC::audioSamplesAvailable(const MediaTime&,
    const PlatformAudioData& audioData, const AudioStreamDescription& streamDescription,
    size_t /* sampleCount */)
{
    auto data = static_cast<const GStreamerAudioData&>(audioData);
    auto desc = static_cast<const GStreamerAudioStreamDescription&>(streamDescription);

    if (m_sampleConverter && !gst_audio_info_is_equal (m_inputStreamDescription->getInfo(), desc.getInfo())) {
        GST_ERROR_OBJECT(this, "FIXME - Audio format renegotiation is not possible yet!");
        g_clear_pointer (&m_sampleConverter, gst_audio_converter_free);
    }

    if (!m_sampleConverter) {
        m_inputStreamDescription = std::unique_ptr<GStreamerAudioStreamDescription>(new GStreamerAudioStreamDescription(desc.getInfo()));
        m_outputStreamDescription = libwebrtcAudioFormat(LibWebRTCAudioFormat::sampleRate, streamDescription.numberOfChannels());

        m_sampleConverter = gst_audio_converter_new (GST_AUDIO_CONVERTER_FLAG_IN_WRITABLE,
            m_inputStreamDescription->getInfo(),
            m_inputStreamDescription->getInfo(),
            NULL);
    }

    WTF::GMutexLocker<GMutex> lock(m_adapterMutex);
    gst_adapter_push (m_Adapter.get(), gst_buffer_ref(gst_sample_get_buffer(data.getSample())));
    LibWebRTCProvider::callOnWebRTCSignalingThread([protectedThis = makeRef(*this)] {
        protectedThis->pullAudioData();
    });
}

void RealtimeOutgoingAudioSourceLibWebRTC::handleMutedIfNeeded()
{
    fprintf(stderr, "handleMutedIfNeeded\n");
}

void RealtimeOutgoingAudioSourceLibWebRTC::sendSilence()
{
    if (!m_outputStreamDescription)
        return;

    LibWebRTCProvider::callOnWebRTCSignalingThread([ this, protectedThis = makeRef(*this) ] {
        GstMapInfo outmap;

        size_t outChunkSampleCount = m_outputStreamDescription->sampleRate() / 100;
        size_t outBufferSize = outChunkSampleCount * m_outputStreamDescription->getInfo()->bpf;

        if (!outBufferSize)
            return;

        GstBuffer *outbuf = gst_buffer_new_allocate(NULL, outBufferSize, 0);

        gst_buffer_map(outbuf, &outmap, (GstMapFlags)GST_MAP_WRITE);
        gst_audio_format_fill_silence(m_outputStreamDescription->getInfo()->finfo,
            outmap.data, outBufferSize);

        for (auto sink : m_sinks)
            sink->OnData(outmap.data,
                LibWebRTCAudioFormat::sampleSize,
                m_outputStreamDescription->sampleRate(),
                m_outputStreamDescription->numberOfChannels(),
                outChunkSampleCount);

        gst_buffer_unmap(outbuf, &outmap);
        gst_buffer_unref (outbuf);
    });
}

void RealtimeOutgoingAudioSourceLibWebRTC::pullAudioData()
{
    if (!m_inputStreamDescription || !m_outputStreamDescription)
        return;

    // libwebrtc expects 10 ms chunks.
    size_t inChunkSampleCount = m_inputStreamDescription->sampleRate() / 100;
    size_t inBufferSize = inChunkSampleCount * m_inputStreamDescription->getInfo()->bpf;

    size_t outChunkSampleCount = m_outputStreamDescription->sampleRate() / 100;
    size_t outBufferSize = outChunkSampleCount * m_outputStreamDescription->getInfo()->bpf;

    WTF::GMutexLocker<GMutex> lock(m_adapterMutex);
    GstBuffer *inbuf = gst_adapter_take_buffer (m_Adapter.get(), inBufferSize);
    GstBuffer *outbuf = gst_buffer_new_allocate (NULL, outBufferSize, 0);
    GstMapInfo inmap;
    GstMapInfo outmap;

    gst_buffer_map (inbuf, &inmap, (GstMapFlags) GST_MAP_READ);
    gst_buffer_map (outbuf, &outmap, (GstMapFlags) GST_MAP_WRITE);

    gpointer in[1] = { inmap.data };
    gpointer out[1] = { outmap.data };

    gst_audio_converter_samples (m_sampleConverter,
        (GstAudioConverterFlags) 0, in, inChunkSampleCount,
        out, outChunkSampleCount);

    gst_buffer_unmap (inbuf, &inmap);
    gst_buffer_unref (inbuf);
    for (auto sink : m_sinks) {
        sink->OnData(outmap.data,
            m_outputStreamDescription->sampleWordSize(),
            (int) m_outputStreamDescription->sampleRate(),
            (int) m_outputStreamDescription->numberOfChannels(),
            outChunkSampleCount);
    }
    gst_buffer_unmap (outbuf, &outmap);
    gst_buffer_unref (outbuf);

}

bool RealtimeOutgoingAudioSourceLibWebRTC::isReachingBufferedAudioDataHighLimit()
{
    fprintf(stderr, "isReachingBufferedAudioDataHighLimit\n");
    return false;
}

bool RealtimeOutgoingAudioSourceLibWebRTC::isReachingBufferedAudioDataLowLimit()
{
    fprintf(stderr, "isReachingBufferedAudioDataHighLimit\n");
    return false;
}

bool RealtimeOutgoingAudioSourceLibWebRTC::hasBufferedEnoughData()
{
    fprintf(stderr, "hasBufferedEnoughData\n");
    return false;
}

} // namespace WebCore

#endif // USE(LIBWEBRTC)
