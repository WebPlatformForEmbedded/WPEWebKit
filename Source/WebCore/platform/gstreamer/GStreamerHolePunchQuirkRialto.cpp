/*
 * Copyright 2024 RDK Management
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

#include "config.h"
#include "GStreamerHolePunchQuirkRialto.h"

#if USE(GSTREAMER)

#include "GStreamerCommon.h"
#include "MediaPlayerPrivateGStreamer.h"
#include <wtf/text/MakeString.h>

namespace WebCore {

GstElement* GStreamerHolePunchQuirkRialto::createHolePunchVideoSink(bool isLegacyPlaybin, const MediaPlayer* player)
{
    AtomString value;
    bool isPIPRequested = player && player->doesHaveAttribute("pip"_s, &value) && equalLettersIgnoringASCIICase(value, "true"_s);
    if (isLegacyPlaybin && !isPIPRequested)
        return nullptr;

    // Rialto using holepunch.
    GstElement* videoSink = makeGStreamerElement("rialtomsevideosink", nullptr);
    if (isPIPRequested)
        g_object_set(G_OBJECT(videoSink), "maxVideoWidth", 640, "maxVideoHeight", 480, "has-drm", FALSE, nullptr);
    return videoSink;
}

bool GStreamerHolePunchQuirkRialto::setHolePunchVideoRectangle(GstElement* videoSink, const IntRect& rect)
{
    if (UNLIKELY(!gstObjectHasProperty(videoSink, "rectangle")))
        return false;

    auto rectString = makeString(rect.x(), ',', rect.y(), ',', rect.width(), ',', rect.height());
    g_object_set(videoSink, "rectangle", rectString.ascii().data(), nullptr);
    return true;
}

std::optional<VideoFrameMetadata> GStreamerHolePunchQuirkRialto::videoFrameMetadata(GRefPtr<GstElement> videoSink, uint64_t& lastVideoFrameMetadataSampleCount)
{
    if (UNLIKELY(!gstObjectHasProperty(videoSink.get(), "video_pts")
        || !gstObjectHasProperty(videoSink.get(), "stats")))
        return { };

    gint64 pts90kHz = 0;
    GstStructure* stats = nullptr;
    g_object_get(videoSink.get(), "video_pts", &pts90kHz, "stats", &stats, nullptr);

    if (!pts90kHz || !stats)
        return { };

    guint64 rendered = 0;
    gst_structure_get_uint64(stats, "rendered", &rendered);
    gst_structure_free(stats);

    if (rendered == lastVideoFrameMetadataSampleCount)
        return { };
    lastVideoFrameMetadataSampleCount = rendered;

    gint width = 0;
    gint height = 0;
    if (GstPad *sinkPad = gst_element_get_static_pad(GST_ELEMENT(videoSink.get()), "sink")) {
        if (GstCaps *caps = gst_pad_get_current_caps(sinkPad)){
            GstStructure *s = gst_caps_get_structure(caps, 0);
            gst_structure_get_int(s, "width", &width);
            gst_structure_get_int(s, "height", &height);
            gst_caps_unref(caps);
        }
        gst_object_unref(sinkPad);
    }

    const auto now = MonotonicTime::now().secondsSinceEpoch().seconds();

    VideoFrameMetadata metadata;
    // Convert 90kHz ticks to seconds
    metadata.mediaTime = MediaTime(pts90kHz, 90000.0).toDouble();
    metadata.width = width;
    metadata.height = height;
    metadata.presentedFrames = rendered;

    // FIXME: presentationTime and expectedDisplayTime might not always have the same value, we should try getting more precise values.
    metadata.presentationTime = now;
    metadata.expectedDisplayTime = metadata.presentationTime;

    return metadata;
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // USE(GSTREAMER)
