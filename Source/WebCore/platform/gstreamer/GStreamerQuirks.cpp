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
#include "GStreamerQuirks.h"

#if USE(GSTREAMER)

#include "GStreamerCommon.h"
#include "GStreamerHolePunchQuirkBcmNexus.h"
#include "GStreamerHolePunchQuirkFake.h"
#include "GStreamerHolePunchQuirkRialto.h"
#include "GStreamerHolePunchQuirkWesteros.h"
#include "GStreamerQuirkAmLogic.h"
#include "GStreamerQuirkBcmNexus.h"
#include "GStreamerQuirkBroadcom.h"
#include "GStreamerQuirkRealtek.h"
#include "GStreamerQuirkRialto.h"
#include "GStreamerQuirkWesteros.h"
#include "GStreamerQuirkRaspberryPi.h"
#include <wtf/NeverDestroyed.h>
#include <wtf/OptionSet.h>
#include <wtf/text/StringView.h>
#include <wtf/FileSystem.h>

namespace WebCore {

GST_DEBUG_CATEGORY_STATIC(webkit_quirks_debug);
#define GST_CAT_DEFAULT webkit_quirks_debug

GStreamerQuirksManager& GStreamerQuirksManager::singleton()
{
    static NeverDestroyed<GStreamerQuirksManager> sharedInstance(false, true);
    return sharedInstance;
}

GStreamerQuirksManager::GStreamerQuirksManager(bool isForTesting, bool loadQuirksFromEnvironment)
    : m_isForTesting(isForTesting)
{
    static std::once_flag debugRegisteredFlag;
    std::call_once(debugRegisteredFlag, [] {
        GST_DEBUG_CATEGORY_INIT(webkit_quirks_debug, "webkitquirks", 0, "WebKit Quirks");
    });

    // For the time being keep this disabled on non-WPE platforms. GTK on desktop shouldn't require
    // quirks, for instance.
#if !PLATFORM(WPE)
    return;
#endif

    GST_DEBUG("Quirk manager created%s", m_isForTesting ? " for testing." : ".");
    if (!loadQuirksFromEnvironment)
        return;

    const char* quirksListFromEnvironment = g_getenv("WEBKIT_GST_QUIRKS");
    StringBuilder quirksListBuilder;
    if (quirksListFromEnvironment)
        quirksListBuilder.append(quirksListFromEnvironment);
    else {
#if PLATFORM(AMLOGIC)
        quirksListBuilder.append("amlogic,");
#endif
#if PLATFORM(BROADCOM)
        quirksListBuilder.append("broadcom,");
#endif
#if PLATFORM(BCM_NEXUS)
        quirksListBuilder.append("bcmnexus,");
#endif
#if PLATFORM(REALTEK)
        quirksListBuilder.append("realtek,");
#endif
#if PLATFORM(WESTEROS)
        quirksListBuilder.append("westeros");
#endif
#if PLATFORM(RPI)
        quirksListBuilder.append("raspberrypi");
#endif
    }
    auto quirks = quirksListBuilder.toString();
    GST_DEBUG("Attempting to parse requested quirks: %s", quirks.ascii().data());
    if (!quirks.isEmpty()) {
        if (WTF::equalLettersIgnoringASCIICase(quirks, "help"_s)) {
            WTFLogAlways("Supported quirks for WEBKIT_GST_QUIRKS are: amlogic, broadcom, bcmnexus, realtek, westeros, raspberrypi");
            return;
        }

        for (const auto& identifier : quirks.split(',')) {
            std::unique_ptr<GStreamerQuirk> quirk;
            if (WTF::equalLettersIgnoringASCIICase(identifier, "amlogic"_s))
                quirk = WTF::makeUnique<GStreamerQuirkAmLogic>();
            else if (WTF::equalLettersIgnoringASCIICase(identifier, "broadcom"_s))
                quirk = WTF::makeUnique<GStreamerQuirkBroadcom>();
            else if (WTF::equalLettersIgnoringASCIICase(identifier, "bcmnexus"_s))
                quirk = WTF::makeUnique<GStreamerQuirkBcmNexus>();
            else if (WTF::equalLettersIgnoringASCIICase(identifier, "realtek"_s))
                quirk = WTF::makeUnique<GStreamerQuirkRealtek>();
            else if (WTF::equalLettersIgnoringASCIICase(identifier, "rialto"_s))
                quirk = WTF::makeUnique<GStreamerQuirkRialto>();
            else if (WTF::equalLettersIgnoringASCIICase(identifier, "westeros"_s))
                quirk = WTF::makeUnique<GStreamerQuirkWesteros>();
            else if (WTF::equalLettersIgnoringASCIICase(identifier, "raspberrypi"_s))
                quirk = WTF::makeUnique<GStreamerQuirkRaspberryPi>();
            else {
                GST_WARNING("Unknown quirk requested: %s. Skipping", identifier.ascii().data());
                continue;
            }

            if (!quirk->isPlatformSupported()) {
                GST_WARNING("Quirk %s was requested but is not supported on this platform. Skipping", quirk->identifier().characters());
                continue;
            }
            m_quirks.append(WTFMove(quirk));
        }
    }

    const char* holePunchQuirkFromEnvironment = g_getenv("WEBKIT_GST_HOLE_PUNCH_QUIRK");
    String holePunchQuirk;
    if (holePunchQuirkFromEnvironment)
        holePunchQuirk = String::fromUTF8(holePunchQuirkFromEnvironment);
    else {
#if USE(WESTEROS_SINK)
        holePunchQuirk = "westeros"_s;
#elif PLATFORM(BCM_NEXUS)
        holePunchQuirk = "bcmnexus"_s;
#endif
    }
    GST_DEBUG("Attempting to parse requested hole-punch quirk: %s", holePunchQuirk.ascii().data());
    if (holePunchQuirk.isEmpty())
        return;

    if (WTF::equalLettersIgnoringASCIICase(holePunchQuirk, "help"_s)) {
        WTFLogAlways("Supported quirks for WEBKIT_GST_HOLE_PUNCH_QUIRK are: fake, westeros, bcmnexus");
        return;
    }

    // TODO: Maybe check this is coherent (somehow) with the quirk(s) selected above.
    if (WTF::equalLettersIgnoringASCIICase(holePunchQuirk, "bcmnexus"_s))
        m_holePunchQuirk = WTF::makeUnique<GStreamerHolePunchQuirkBcmNexus>();
    else if (WTF::equalLettersIgnoringASCIICase(holePunchQuirk, "rialto"_s))
        m_holePunchQuirk = WTF::makeUnique<GStreamerHolePunchQuirkRialto>();
    else if (WTF::equalLettersIgnoringASCIICase(holePunchQuirk, "westeros"_s))
        m_holePunchQuirk = WTF::makeUnique<GStreamerHolePunchQuirkWesteros>();
    else if (WTF::equalLettersIgnoringASCIICase(holePunchQuirk, "fake"_s))
        m_holePunchQuirk = WTF::makeUnique<GStreamerHolePunchQuirkFake>();
    else
        GST_WARNING("HolePunch quirk %s un-supported.", holePunchQuirk.ascii().data());
}

bool GStreamerQuirksManager::isEnabled() const
{
    return !m_quirks.isEmpty();
}

GstElement* GStreamerQuirksManager::createAudioSink()
{
    for (const auto& quirk : m_quirks) {
        auto* sink = quirk->createAudioSink();
        if (sink) {
            GST_DEBUG("Using AudioSink from quirk %s : %" GST_PTR_FORMAT, quirk->identifier().characters(), sink);
            return sink;
        }
    }

    return nullptr;
}

GstElement* GStreamerQuirksManager::createWebAudioSink()
{
    for (const auto& quirk : m_quirks) {
        auto* sink = quirk->createWebAudioSink();
        if (!sink)
            continue;

        GST_DEBUG("Using WebAudioSink from quirk %s : %" GST_PTR_FORMAT, quirk->identifier().characters(), sink);
        return sink;
    }

    GST_DEBUG("Quirks didn't specify a WebAudioSink, falling back to default sink");
    return createPlatformAudioSink("music"_s);
}

GstElement* GStreamerQuirksManager::createHolePunchVideoSink(bool isLegacyPlaybin, const MediaPlayer* player)
{
    if (!m_holePunchQuirk) {
        GST_DEBUG("None of the quirks requested a HolePunchSink");
        return nullptr;
    }
    auto sink = m_holePunchQuirk->createHolePunchVideoSink(isLegacyPlaybin, player);
    GST_DEBUG("Using HolePunchSink from quirk %s : %" GST_PTR_FORMAT, m_holePunchQuirk->identifier().characters(), sink);
    return sink;
}

void GStreamerQuirksManager::setHolePunchVideoRectangle(GstElement* videoSink, const IntRect& rect)
{
    if (!m_holePunchQuirk) {
        GST_DEBUG("None of the quirks requested a HolePunchSink");
        return;
    }

    if (!m_holePunchQuirk->setHolePunchVideoRectangle(videoSink, rect))
        GST_WARNING("Hole punch video rectangle configuration failed.");
}

bool GStreamerQuirksManager::sinksRequireClockSynchronization() const
{
    if (!m_holePunchQuirk)
        return true;

    return m_holePunchQuirk->requiresClockSynchronization();
}

void GStreamerQuirksManager::configureElement(GstElement* element, OptionSet<ElementRuntimeCharacteristics>&& characteristics)
{
    GST_DEBUG("Configuring element %" GST_PTR_FORMAT, element);
    for (const auto& quirk : m_quirks)
        quirk->configureElement(element, characteristics);
}

std::optional<bool> GStreamerQuirksManager::isHardwareAccelerated(GstElementFactory* factory) const
{
    for (const auto& quirk : m_quirks) {
        auto result = quirk->isHardwareAccelerated(factory);
        if (!result)
            continue;

        GST_DEBUG("Setting %" GST_PTR_FORMAT " as %s accelerated from quirk %s", factory, quirk->identifier().characters(), *result ? "hardware" : "software");
        return *result;
    }

    return std::nullopt;
}

bool GStreamerQuirksManager::supportsVideoHolePunchRendering() const
{
    return m_holePunchQuirk.get();
}

GstElementFactoryListType GStreamerQuirksManager::audioVideoDecoderFactoryListType() const
{
    for (const auto& quirk : m_quirks) {
        auto result = quirk->audioVideoDecoderFactoryListType();
        if (!result)
            continue;

        GST_DEBUG("Quirk %s requests audio/video decoder factory list override to %" G_GUINT32_FORMAT, quirk->identifier().characters(), static_cast<uint32_t>(*result));
        return *result;
    }

    return GST_ELEMENT_FACTORY_TYPE_DECODER;
}

Vector<String> GStreamerQuirksManager::disallowedWebAudioDecoders() const
{
    Vector<String> result;
    for (const auto& quirk : m_quirks)
        result.appendVector(quirk->disallowedWebAudioDecoders());

    return result;
}

void GStreamerQuirksManager::setHolePunchEnabledForTesting(bool enabled)
{
    if (enabled)
        m_holePunchQuirk = WTF::makeUnique<GStreamerHolePunchQuirkFake>();
    else
        m_holePunchQuirk = nullptr;
}

unsigned GStreamerQuirksManager::getAdditionalPlaybinFlags() const
{
    unsigned flags = 0;
#if USE(GSTREAMER_NATIVE_VIDEO)
    flags |= getGstPlayFlag("native-video");
#else
    flags |= getGstPlayFlag("soft-colorbalance");
#endif
#if USE(GSTREAMER_NATIVE_AUDIO)
    flags |= getGstPlayFlag("native-audio");
#endif
#if USE(GSTREAMER_TEXT_SINK)
    flags |= getGstPlayFlag("text");
#endif
    for (const auto& quirk : m_quirks) {
        if (auto additionalFlags = quirk->getAdditionalPlaybinFlags()) {
            GST_DEBUG("Quirk %s requests these playbin flags: %u", quirk->identifier().characters(), additionalFlags);
            flags |= additionalFlags;
        }
    }

    return flags;
}

bool GStreamerQuirksManager::shouldParseIncomingLibWebRTCBitStream() const
{
    for (auto& quirk : m_quirks) {
        if (!quirk->shouldParseIncomingLibWebRTCBitStream())
            return false;
    }
    return true;
}

bool GStreamerQuirksManager::needsBufferingPercentageCorrection() const
{
    for (auto& quirk : m_quirks) {
        if (quirk->needsBufferingPercentageCorrection())
            return true;
    }
    return false;
}

ASCIILiteral GStreamerQuirksManager::queryBufferingPercentage(MediaPlayerPrivateGStreamer* mediaPlayerPrivate, const GRefPtr<GstQuery>& query) const
{
    // Only the first quirk that needs percentage correction must operate. We're assuming that the m_quirks Vector
    // preserves its order among calls to the percentage correction family of methods.
    for (auto& quirk : m_quirks) {
        if (quirk->needsBufferingPercentageCorrection())
            return quirk->queryBufferingPercentage(mediaPlayerPrivate, query);
    }
    return ASCIILiteral();
}

int GStreamerQuirksManager::correctBufferingPercentage(MediaPlayerPrivateGStreamer* playerPrivate, int originalBufferingPercentage, GstBufferingMode mode) const
{
    // Only the first quirk that needs percentage correction must operate. We're assuming that the m_quirks Vector
    // preserves its order among calls to the percentage correction family of methods.
    for (auto& quirk : m_quirks) {
        if (quirk->needsBufferingPercentageCorrection())
            return quirk->correctBufferingPercentage(playerPrivate, originalBufferingPercentage, mode);
    }
    return originalBufferingPercentage;
}

void GStreamerQuirksManager::resetBufferingPercentage(MediaPlayerPrivateGStreamer* playerPrivate, int bufferingPercentage) const
{
    // Only the first quirk that needs percentage correction must operate. We're assuming that the m_quirks Vector
    // preserves its order among calls to the percentage correction family of methods.
    for (auto& quirk : m_quirks) {
        if (quirk->needsBufferingPercentageCorrection()) {
            quirk->resetBufferingPercentage(playerPrivate, bufferingPercentage);
            return;
        }
    }
}

void GStreamerQuirksManager::setupBufferingPercentageCorrection(MediaPlayerPrivateGStreamer* playerPrivate, GstState currentState, GstState newState, GRefPtr<GstElement>&& element) const
{
    // Only the first quirk that needs percentage correction must operate. We're assuming that the m_quirks Vector
    // preserves its order among calls to the percentage correction family of methods.
    for (auto& quirk : m_quirks) {
        if (quirk->needsBufferingPercentageCorrection()) {
            // We're moving the element to the inner method. If this loop ever needs to call the method twice,
            // think about a solution to avoid passing a dummy element (after first move) to the method the second
            // time it's called.
            quirk->setupBufferingPercentageCorrection(playerPrivate, currentState, newState, WTFMove(element));
            return;
        }
    }
}

void GStreamerQuirksManager::loadExtraSystemPlugins() const
{
    constexpr auto DEFAULT_SYSTEM_PLUGIN_PATH = "/usr/lib/gstreamer-1.0";
    static std::once_flag onceFlag;
    std::call_once(onceFlag, [&]() {
        auto systemPluginsPathEnv = g_getenv("GST_PLUGIN_SYSTEM_PATH_1_0");
        if (!systemPluginsPathEnv)
            systemPluginsPathEnv = g_getenv("GST_PLUGIN_SYSTEM_PATH");
        if (!systemPluginsPathEnv) {
            GST_WARNING("No GST_PLUGIN_SYSTEM_PATH_1_0 or GST_PLUGIN_SYSTEM_PATH set, attempting to load extra system plugins from %s", DEFAULT_SYSTEM_PLUGIN_PATH);
            systemPluginsPathEnv = DEFAULT_SYSTEM_PLUGIN_PATH;
        }

        auto systemPluginsPaths = makeString(systemPluginsPathEnv).split(':');

        auto loadPlugin = [&](const String& pluginName) {
            auto plugin = adoptGRef(gst_registry_find_plugin(gst_registry_get(), pluginName.utf8().data()));
            if (plugin) {
                GST_ERROR("Plugin %s already in the registry. Will not load a duplicate.", pluginName.utf8().data());
                return;
            }

            // FIXME: It would be nice to have some helpers from GStreamer here to avoid doing this, but those that are
            // there rely on using the registry, and we can't enable the registry because it loads duplicated plugins
            // which results in errors with duplicated GTypes.
            #if !OS(LINUX)
                static_assert(false, "Extra loading of plugins only tested on Linux");
            #endif
            auto sharedLibraryFilename = makeString("lib"_s, pluginName, ".so"_s);

            for (const auto& path : systemPluginsPaths) {
                auto pluginPath = FileSystem::pathByAppendingComponent(path, sharedLibraryFilename);
                GST_DEBUG("Trying to load %s", pluginPath.utf8().data());

                GUniqueOutPtr<GError> error;
                plugin = adoptGRef(gst_plugin_load_file(pluginPath.utf8().data(), &error.outPtr()));
                if (plugin) {
                    GST_INFO("Loaded %s successfully.", pluginPath.utf8().data());
                    return;
                }
            }
            GST_ERROR("Failed to load plugin %s. Check whether it's installed in GST_PLUGIN_SYSTEM_PATH_1_0 or GST_PLUGIN_SYSTEM_PATH.", pluginName.utf8().data());
        };

        for (auto& quirk : m_quirks) {
            for (auto& pluginName : quirk->extraSystemPlugins())
                loadPlugin(pluginName);
        }
    });
}

#undef GST_CAT_DEFAULT

} // namespace WebCore

#endif // USE(GSTREAMER)
