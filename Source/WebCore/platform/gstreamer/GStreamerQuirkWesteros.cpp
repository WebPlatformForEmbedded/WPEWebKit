/*
 * Copyright (C) 2024 Igalia S.L
 * Copyright (C) 2024 Metrological Group B.V.
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
#include "GStreamerQuirkWesteros.h"

#if USE(GSTREAMER)

#include "GStreamerCommon.h"
#include "MediaPlayerPrivateGStreamer.h"
#include <wtf/OptionSet.h>

namespace WebCore {

GST_DEBUG_CATEGORY_STATIC(webkit_westeros_quirks_debug);
#define GST_CAT_DEFAULT webkit_westeros_quirks_debug

GStreamerQuirkWesteros::GStreamerQuirkWesteros()
{
    GST_DEBUG_CATEGORY_INIT(webkit_westeros_quirks_debug, "webkitquirkswesteros", 0, "WebKit Westeros Quirks");

    auto westerosFactory = adoptGRef(gst_element_factory_find("westerossink"));
    if (UNLIKELY(!westerosFactory))
        return;

    gst_object_unref(gst_plugin_feature_load(GST_PLUGIN_FEATURE(westerosFactory.get())));
    for (auto* t = gst_element_factory_get_static_pad_templates(westerosFactory.get()); t; t = g_list_next(t)) {
        auto* padtemplate = static_cast<GstStaticPadTemplate*>(t->data);
        if (padtemplate->direction != GST_PAD_SINK)
            continue;
        if (m_sinkCaps)
            m_sinkCaps = adoptGRef(gst_caps_merge(m_sinkCaps.leakRef(), gst_static_caps_get(&padtemplate->static_caps)));
        else
            m_sinkCaps = adoptGRef(gst_static_caps_get(&padtemplate->static_caps));
    }
}

bool GStreamerQuirkWesteros::isPlatformSupported() const
{
    return adoptGRef(gst_element_factory_find("westerossink"));
}

void GStreamerQuirkWesteros::configureElement(GstElement* element, const OptionSet<ElementRuntimeCharacteristics>& characteristics)
{
    if (!characteristics.contains(ElementRuntimeCharacteristics::IsMediaStream))
        return;

    if (!g_strcmp0(G_OBJECT_TYPE_NAME(G_OBJECT(element)), "GstWesterosSink") && gstObjectHasProperty(element, "immediate-output")) {
        GST_INFO("Enable 'immediate-output' in WesterosSink");
        g_object_set(element, "immediate-output", TRUE, nullptr);
    }
}

std::optional<bool> GStreamerQuirkWesteros::isHardwareAccelerated(GstElementFactory* factory)
{
    if (g_str_has_prefix(GST_OBJECT_NAME(factory), "westeros"))
        return true;

    return std::nullopt;
}

bool GStreamerQuirkWesteros::setupDecoderVideoSinkDecodingErrorNotification(MediaPlayerPrivateGStreamer* playerPrivate, GstElement* videoSink) const
{
    if (g_strcmp0(G_OBJECT_TYPE_NAME(G_OBJECT(videoSink)), "GstWesterosSink")) {
        GST_WARNING("The supplied video sink isn't a GstWesterosSink");
        return false;
    }

    if (!gstObjectHasProperty(videoSink, "report-decode-errors")) {
        GST_WARNING("WesterosSink doesn't have a report-decode-errors property");
        return false;
    }

    if (!g_signal_lookup("decode-error-callback", G_OBJECT_TYPE(G_OBJECT(videoSink)))) {
        GST_WARNING("WesterosSink doesn't have a decode-error-callback signal");
        return false;
    }

    GST_INFO("Enable 'report-decode-errors' in WesterosSink and listen to the decode-error-callback signal");
    g_object_set(videoSink, "report-decode-errors", TRUE, nullptr);
    g_signal_connect_swapped(videoSink, "decode-error-callback", G_CALLBACK(+[](MediaPlayerPrivateGStreamer* player) {
        GST_INFO("!!! Decoding error detected, notifying player");
        player->notifyVideoDecodingError();
    }), playerPrivate);
    return true;
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // USE(GSTREAMER)
