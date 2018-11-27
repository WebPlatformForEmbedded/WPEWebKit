/*
 *  Copyright (C) 2012, 2015, 2016 Igalia S.L
 *  Copyright (C) 2015, 2016 Metrological Group B.V.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once

#if USE(GSTREAMER)
#include "FloatSize.h"
#include "GRefPtrGStreamer.h"
#include "GUniquePtrGStreamer.h"
#include <gst/gst.h>
#include <gst/video/video-format.h>
#include <gst/video/video-info.h>
#include <wtf/MediaTime.h>

namespace WebCore {

class IntSize;

inline bool webkitGstCheckVersion(guint major, guint minor, guint micro)
{
    guint currentMajor, currentMinor, currentMicro, currentNano;
    gst_version(&currentMajor, &currentMinor, &currentMicro, &currentNano);

    if (currentMajor < major)
        return false;
    if (currentMajor > major)
        return true;

    if (currentMinor < minor)
        return false;
    if (currentMinor > minor)
        return true;

    if (currentMicro < micro)
        return false;

    return true;
}

#define GST_VIDEO_CAPS_TYPE_PREFIX  "video/"
#define GST_AUDIO_CAPS_TYPE_PREFIX  "audio/"
#define GST_TEXT_CAPS_TYPE_PREFIX   "text/"

GstPad* webkitGstGhostPadFromStaticTemplate(GstStaticPadTemplate*, const gchar* name, GstPad* target);
#if ENABLE(VIDEO)
bool getVideoSizeAndFormatFromCaps(GstCaps*, WebCore::IntSize&, GstVideoFormat&, int& pixelAspectRatioNumerator, int& pixelAspectRatioDenominator, int& stride);
std::optional<FloatSize> getVideoResolutionFromCaps(const GstCaps*);
bool getSampleVideoInfo(GstSample*, GstVideoInfo&);
#endif
GstBuffer* createGstBuffer(GstBuffer*);
GstBuffer* createGstBufferForData(const char* data, int length);
char* getGstBufferDataPointer(GstBuffer*);
const char* capsMediaType(const GstCaps*);
bool doCapsHaveType(const GstCaps*, const char*);
bool areEncryptedCaps(const GstCaps*);
void mapGstBuffer(GstBuffer*, uint32_t);
void unmapGstBuffer(GstBuffer*);
Vector<String> extractGStreamerOptionsFromCommandLine();
bool initializeGStreamer(std::optional<Vector<String>>&& = std::nullopt);
unsigned getGstPlayFlag(const char* nick);
uint64_t toGstUnsigned64Time(const MediaTime&);

inline GstClockTime toGstClockTime(const MediaTime &mediaTime)
{
    return static_cast<GstClockTime>(toGstUnsigned64Time(mediaTime));
}

class GstMappedBuffer {
    WTF_MAKE_NONCOPYABLE(GstMappedBuffer);
public:
    explicit GstMappedBuffer(GstBuffer* buffer, GstMapFlags flags)
        : m_buffer(buffer)
    {
        m_isValid = gst_buffer_map(m_buffer, &m_info, flags);
    }
    // Unfortunately, GST_MAP_READWRITE is defined out of line from the MapFlags
    // enum as an int, and C++ is careful to not implicity convert it to an enum.
    explicit GstMappedBuffer(GstBuffer* buffer, int flags)
        : GstMappedBuffer(buffer, static_cast<GstMapFlags>(flags)) { }

    ~GstMappedBuffer()
    {
        if (m_isValid)
            gst_buffer_unmap(m_buffer, &m_info);
    }

    uint8_t* data() { ASSERT(m_isValid); return static_cast<uint8_t*>(m_info.data); }
    size_t size() const { ASSERT(m_isValid); return static_cast<size_t>(m_info.size); }

    explicit operator bool() const { return m_isValid; }
private:
    GstBuffer* m_buffer;
    GstMapInfo m_info;
    bool m_isValid { false };
};

class GstMappedFrame {
    WTF_MAKE_NONCOPYABLE(GstMappedFrame);
public:

    GstMappedFrame(GstBuffer* buffer, GstVideoInfo info, GstMapFlags flags)
    {
        m_isValid = gst_video_frame_map(&m_frame, &info, buffer, flags);
    }

    GstMappedFrame(GRefPtr<GstSample> sample, GstMapFlags flags)
    {
        GstVideoInfo info;

        if (!gst_video_info_from_caps(&info, gst_sample_get_caps(sample.get()))) {
            m_isValid = false;
            return;
        }

        m_isValid = gst_video_frame_map(&m_frame, &info, gst_sample_get_buffer(sample.get()), flags);
    }

    GstVideoFrame* get()
    {
        if (!m_isValid) {
            GST_INFO("Invalid frame, returning NULL");

            return nullptr;
        }

        return &m_frame;
    }

    uint8_t* ComponentData(int comp)
    {
        return GST_VIDEO_FRAME_COMP_DATA(&m_frame, comp);
    }

    int ComponentStride(int stride)
    {
        return GST_VIDEO_FRAME_COMP_STRIDE(&m_frame, stride);
    }

    GstVideoInfo* info()
    {
        if (!m_isValid) {
            GST_INFO("Invalid frame, returning NULL");

            return nullptr;
        }

        return &m_frame.info;
    }

    int width()
    {
        return m_isValid ? GST_VIDEO_FRAME_WIDTH(&m_frame) : -1;
    }

    int height()
    {
        return m_isValid ? GST_VIDEO_FRAME_HEIGHT(&m_frame) : -1;
    }

    int format()
    {
        return m_isValid ? GST_VIDEO_FRAME_FORMAT(&m_frame) : GST_VIDEO_FORMAT_UNKNOWN;
    }

    ~GstMappedFrame()
    {
        if (m_isValid)
            gst_video_frame_unmap(&m_frame);
        m_isValid = false;
    }

    explicit operator bool() const { return m_isValid; }

private:
    GstVideoFrame m_frame;
    bool m_isValid { false };
};


bool gstRegistryHasElementForMediaType(GList* elementFactories, const char* capsString);
void connectSimpleBusMessageCallback(GstElement* pipeline);
void disconnectSimpleBusMessageCallback(GstElement* pipeline);

}

#endif // USE(GSTREAMER)
