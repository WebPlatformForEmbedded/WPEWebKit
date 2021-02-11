/*
 * Copyright (C) 2018 Metrological Group B.V.
 * Author: Thibault Saunier <tsaunier@igalia.com>
 * Author: Alejandro G. Castro  <alex@igalia.com>
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

#if ENABLE(MEDIA_STREAM) && USE(LIBWEBRTC) && USE(GSTREAMER)
#include "GStreamerVideoCapturer.h"

namespace WebCore {

GStreamerVideoCapturer::GStreamerVideoCapturer(GStreamerCaptureDevice device)
    : GStreamerCapturer(device, adoptGRef(gst_caps_new_empty_simple("video/x-raw")))
{
}

GStreamerVideoCapturer::GStreamerVideoCapturer(const char* sourceFactory)
    : GStreamerCapturer(sourceFactory, adoptGRef(gst_caps_new_empty_simple("video/x-raw")))
{
}

GstElement* GStreamerVideoCapturer::createConverter()
{
    auto converter = gst_parse_bin_from_description("videoscale ! videoconvert ! videorate", TRUE, nullptr);
#if USE(WESTEROS_SINK)
// If camera supports only x-raw, westerossink expects encoded form. So pass through an h264 encoder and parser after probing platform.
// If camera supports encoded formats, use a dummy identity element to skip converting to x-raw.
    const char* codec = getPreferredCodec().c_str();
    GST_INFO_OBJECT(m_pipeline.get(), "Preferred codec is %s ", codec);

    if (strcmp (codec, "video/x-raw") == 0)
    {
        GstElementFactory *h264encoder = getEncoder ("video/x-h264");
        GstElementFactory *h264parser = getParser ("video/x-h264");

        char *videoInput = g_strdup_printf ("videoscale ! videoconvert ! videorate ! %s ! %s", GST_ELEMENT_NAME(h264encoder), GST_ELEMENT_NAME(h264parser));
        converter = gst_parse_bin_from_description(videoInput, TRUE, nullptr);
        m_caps = adoptGRef(gst_caps_new_empty_simple("video/x-h264"));
    }
    else
    {
        converter = gst_parse_bin_from_description("identity", TRUE, nullptr);
        m_caps = adoptGRef(gst_caps_new_empty_simple(codec));
    }
#endif

    ASSERT(converter);

    return converter;
}

std::string GStreamerVideoCapturer::getPreferredCodec()
{
    GstCaps* caps = gst_device_get_caps(m_device.get());
    const std::string videoCaps = gst_caps_to_string(caps);

    if( videoCaps.find("video/x-av1") != std::string::npos )
        return "video/x-av1";
    else if( videoCaps.find("video/x-vp9") != std::string::npos )
        return "video/x-vp9";
    else if( videoCaps.find("video/x-vp8") != std::string::npos )
        return "video/x-vp8";
    else if( videoCaps.find("video/x-h265") != std::string::npos )
        return "video/x-h265";
    else if( videoCaps.find("video/x-h264") != std::string::npos )
        return "video/x-h264";
    else if( videoCaps.find("video/x-mpeg") != std::string::npos )
        return "video/x-mpeg";
    else if( videoCaps.find("video/x-raw") != std::string::npos )
        return "video/x-raw";

    return " ";
}

GstElementFactory* GStreamerVideoCapturer::getEncoder (const char* format)
{
    GList *encoder_list, *encoders;
    GstCaps *caps_str;
    GstElementFactory *encoder_factory;
    caps_str = gst_caps_from_string (format);

    encoder_list = gst_element_factory_list_get_elements (GST_ELEMENT_FACTORY_TYPE_ENCODER, GST_RANK_MARGINAL);
    encoders = gst_element_factory_list_filter(encoder_list, caps_str, GST_PAD_SRC, false);

    if (!(g_list_length(encoders)))
    {
        gst_plugin_feature_list_free (encoder_list);
        gst_plugin_feature_list_free (encoders);
        GST_ERROR("Couldnt find h264 encoder ");
        return NULL;
    }
    encoder_factory = GST_ELEMENT_FACTORY (g_list_first(encoders)->data);
    GST_INFO("Found H264 encoder : %s ", GST_ELEMENT_NAME(encoder_factory));

    gst_caps_unref (caps_str);
    gst_plugin_feature_list_free (encoder_list);
    gst_plugin_feature_list_free (encoders);

    return encoder_factory;
}

GstElementFactory* GStreamerVideoCapturer::getParser (const char* format)
{
    GList *parser_list, *parsers;
    GstCaps *caps_str;
    GstElementFactory *parser_factory;
    caps_str = gst_caps_from_string (format);

    parser_list = gst_element_factory_list_get_elements (GST_ELEMENT_FACTORY_TYPE_PARSER, GST_RANK_MARGINAL);
    parsers = gst_element_factory_list_filter(parser_list, caps_str, GST_PAD_SINK, false);

    if (!(g_list_length(parsers)))
    {
        gst_plugin_feature_list_free (parser_list);
        gst_plugin_feature_list_free (parsers);
        GST_ERROR("Couldnt find h264 parser ");
        return NULL;
    }
    parser_factory = GST_ELEMENT_FACTORY (g_list_first(parsers)->data);
    GST_INFO("Found H264 parser : %s ", GST_ELEMENT_NAME(parser_factory));

    gst_caps_unref (caps_str);
    gst_plugin_feature_list_free (parser_list);
    gst_plugin_feature_list_free (parsers);

    return parser_factory;
}

GstVideoInfo GStreamerVideoCapturer::getBestFormat()
{
    GRefPtr<GstCaps> caps = adoptGRef(gst_caps_fixate(gst_device_get_caps(m_device.get())));
    GstVideoInfo info;
    gst_video_info_from_caps(&info, caps.get());

    return info;
}

bool GStreamerVideoCapturer::setSize(int width, int height)
{
    if (!width || !height)
        return false;

    m_caps = adoptGRef(gst_caps_copy(m_caps.get()));
    gst_caps_set_simple(m_caps.get(), "width", G_TYPE_INT, width, "height", G_TYPE_INT, height, nullptr);

    if (!m_capsfilter)
        return false;

    g_object_set(m_capsfilter.get(), "caps", m_caps.get(), nullptr);

    return true;
}

bool GStreamerVideoCapturer::setFrameRate(double frameRate)
{
    int numerator, denominator;

    gst_util_double_to_fraction(frameRate, &numerator, &denominator);

    if (numerator < -G_MAXINT) {
        GST_INFO_OBJECT(m_pipeline.get(), "Framerate %f not allowed", frameRate);
        return false;
    }

    if (!numerator) {
        GST_INFO_OBJECT(m_pipeline.get(), "Do not force variable framerate");
        return false;
    }

    m_caps = adoptGRef(gst_caps_copy(m_caps.get()));
    gst_caps_set_simple(m_caps.get(), "framerate", GST_TYPE_FRACTION, numerator, denominator, nullptr);

    if (!m_capsfilter)
        return false;

    g_object_set(m_capsfilter.get(), "caps", m_caps.get(), nullptr);

    return true;
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(LIBWEBRTC) && USE(GSTREAMER)
