/*
 * Copyright (C) 2015 Igalia S.L.
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
#include "NetscapePlugin.h"
#include <syslog.h>

//#if PLUGIN_ARCHITECTURE(X11)  && ENABLE(NETSCAPE_PLUGIN_API)
#if PLUGIN_ARCHITECTURE(WayLand)  && ENABLE(NETSCAPE_PLUGIN_API)
//#include "NetscapePluginX11.h"
#include "NetscapePluginWayLand.h"
#include "WebEvent.h"
#include <WebCore/NotImplemented.h>
#include <WebCore/PlatformDisplay.h>

using namespace WebCore;

namespace WebKit {

void NetscapePlugin::platformPreInitialize()
{
}

bool NetscapePlugin::platformPostInitialize()
{
    syslog(LOG_INFO, "file: %s, function: %s " , __FILE__, __func__);
#if PLATFORM(X11)
    if (PlatformDisplay::sharedDisplay().type() == PlatformDisplay::Type::X11) {
    	syslog(LOG_INFO, "file: %s, function: %s " , __FILE__, __func__);
        m_impl = NetscapePluginX11::create(*this);
        if (!m_impl)
            return false;
    }
#elif PLATFORM(WPE)
    if (PlatformDisplay::sharedDisplay().type() == PlatformDisplay::Type::WPE) {
        m_impl = NetscapePluginWayLand::create(*this);
        syslog(LOG_INFO, "file: %s, function====Skip callSetWindow: %s " , __FILE__, __func__);
        if (!m_impl)
            return false;
    }
#endif

    // Windowed plugins need a platform implementation.

    if (!m_impl)
        return !m_isWindowed;
    return true;
}

void NetscapePlugin::platformDestroy()
{
    m_impl = nullptr;
}

bool NetscapePlugin::platformInvalidate(const IntRect&)
{
    notImplemented();
    return false;
}

void NetscapePlugin::platformGeometryDidChange()
{
    if (!m_impl)
        return;
    //m_impl->geometryDidChange();
}

void NetscapePlugin::platformVisibilityDidChange()
{
    if (!m_isWindowed || !m_impl)
        return;

   // m_impl->visibilityDidChange();
}

void NetscapePlugin::platformPaint(GraphicsContext& context, const IntRect& dirtyRect, bool /*isSnapshot*/)
{
#if 0
    if (m_isWindowed || !m_impl)
        return;

    if (!m_isStarted) {
        // FIXME: we should paint a missing plugin icon.
        return;
    }

    m_impl->paint(context, dirtyRect);
#endif
}

bool NetscapePlugin::platformHandleMouseEvent(const WebMouseEvent& event)
{
#if 0 
    if (m_isWindowed || !m_impl)
        return false;

    if ((event.type() == WebEvent::MouseDown || event.type() == WebEvent::MouseUp)
        && event.button() == WebMouseEvent::RightButton
        && quirks().contains(PluginQuirks::IgnoreRightClickInWindowlessMode))
        return false;

    return m_impl->handleMouseEvent(event);
    
#endif
    return true;
}

bool NetscapePlugin::platformHandleWheelEvent(const WebWheelEvent& event)
{
#if 0
    if (m_isWindowed || !m_impl)
        return false;

    return m_impl->handleWheelEvent(event);
    #endif
    return true;
}

void NetscapePlugin::platformSetFocus(bool focusIn)
{
#if 0
    if (m_isWindowed || !m_impl)
        return;

    m_impl->setFocus(focusIn);
#endif
}

bool NetscapePlugin::wantsPluginRelativeNPWindowCoordinates()
{
    return true;
}

bool NetscapePlugin::platformHandleMouseEnterEvent(const WebMouseEvent& event)
{
#if 0
    if (m_isWindowed || !m_impl)
        return false;

    return m_impl->handleMouseEnterEvent(event);
#endif
    return true;
}

bool NetscapePlugin::platformHandleMouseLeaveEvent(const WebMouseEvent& event)
{
#if 0
    if (m_isWindowed || !m_impl)
        return false;

    return m_impl->handleMouseLeaveEvent(event);
#endif
    return true;
}

bool NetscapePlugin::platformHandleKeyboardEvent(const WebKeyboardEvent& event)
{ 
#if 0
    // We don't generate other types of keyboard events via WebEventFactory.
    ASSERT(event.type() == WebEvent::KeyDown || event.type() == WebEvent::KeyUp);

    if (m_isWindowed || !m_impl)
        return false;

    return m_impl->handleKeyboardEvent(event);
#endif
return true;
}
} // namespace WebKit

#endif // PLUGIN_ARCHITECTURE(X11) && ENABLE(NETSCAPE_PLUGIN_API)
