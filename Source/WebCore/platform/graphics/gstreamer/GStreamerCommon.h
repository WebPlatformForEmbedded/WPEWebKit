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
#include "GRefPtrGStreamer.h"
#include "GUniquePtrGStreamer.h"
#include <gst/gst.h>
#include <gst/video/video-format.h>
#include <gst/video/video-info.h>
#include <wtf/MediaTime.h>
#include <wtf/Seconds.h>

namespace WebCore {

using GstEventSeqNum = uint32_t;
class IntSize;
// NOTE: YouTube 2018 has tests that expect this to be >= 5s.
const WTF::Seconds GST_EME_LICENSE_KEY_RESPONSE_TIMEOUT = WTF::Seconds(6);

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

GstPad* webkitGstGhostPadFromStaticTemplate(GstStaticPadTemplate*, const gchar* name, GstPad* target);
#if ENABLE(VIDEO)
bool getVideoSizeAndFormatFromCaps(GstCaps*, WebCore::IntSize&, GstVideoFormat&, int& pixelAspectRatioNumerator, int& pixelAspectRatioDenominator, int& stride);
bool getSampleVideoInfo(GstSample*, GstVideoInfo&);
#endif
GstBuffer* createGstBuffer(GstBuffer*);
GstBuffer* createGstBufferForData(const char* data, int length);
char* getGstBufferDataPointer(GstBuffer*);
void mapGstBuffer(GstBuffer*, uint32_t);
void unmapGstBuffer(GstBuffer*);
bool initializeGStreamer();
unsigned getGstPlayFlag(const char* nick);
uint64_t toGstUnsigned64Time(const MediaTime&);

inline GstClockTime toGstClockTime(const MediaTime &mediaTime)
{
    return static_cast<GstClockTime>(toGstUnsigned64Time(mediaTime));
}

bool gstRegistryHasElementForMediaType(GList* elementFactories, const char* capsString);
}

#endif // USE(GSTREAMER)
