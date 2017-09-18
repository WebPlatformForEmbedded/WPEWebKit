/*
 * Copyright (C) 2014 Igalia S.L.
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
#include "WebPageProxy.h"

#include "WebsiteDataStore.h"
#include <WebCore/NotImplemented.h>


#include "NotImplemented.h"
#include "PageClientImpl.h"
//#include "WebKitWebViewBasePrivate.h"
#include "WebPageMessages.h"

//#include "WebPasteboardProxy.h"
#include "WebProcessProxy.h"
#include "WebsiteDataStore.h"
#include <WebCore/UserAgent.h>
#include <wtf/NeverDestroyed.h>
#include <syslog.h>



namespace WebKit {

void WebPageProxy::platformInitialize()
{
}

//  GtkWidget* WebPageProxy::viewWidget()
//{
//    return static_cast<PageClientImpl&>(m_pageClient).viewWidget();
//}

String WebPageProxy::standardUserAgent(const String& applicationNameForUserAgent)
{
	return "Mozilla/5.0 (Linux; x86_64 GNU/Linux) AppleWebKit/601.1 (KHTML, like Gecko) Version/8.0 Safari/601.1 WPE";
}

//void WebPageProxy::bindAccessibilityTree(const String& plugID)
//{
//    m_accessibilityPlugID = plugID;
//}

void WebPageProxy::saveRecentSearches(const String&, const Vector<WebCore::RecentSearch>&)
{
    notImplemented();
}

void WebPageProxy::loadRecentSearches(const String&, Vector<WebCore::RecentSearch>&)
{
    notImplemented();
}

void WebsiteDataStore::platformRemoveRecentSearches(std::chrono::system_clock::time_point oldestTimeToRemove)
{
    notImplemented();
}

void WebPageProxy::editorStateChanged(const EditorState& editorState)
{
//    m_editorState = editorState;
//
//    if (editorState.shouldIgnoreCompositionSelectionChange)
//        return;
//    if (m_editorState.selectionIsRange)
//        WebPasteboardProxy::singleton().setPrimarySelectionOwner(focusedFrame());
//    m_pageClient.selectionDidChange();
}

#if PLUGIN_ARCHITECTURE(X11) ||PLUGIN_ARCHITECTURE(WayLand)
//typedef HashMap<uint64_t, GtkWidget* > PluginWindowMap;
typedef HashMap<uint64_t, WebCore::Widget* > PluginWindowMap;
static PluginWindowMap& pluginWindowMap()
{
    static NeverDestroyed<PluginWindowMap> map;
    return map;
}

//static gboolean pluginContainerPlugRemoved(GtkSocket* socket)
//{
//  //  uint64_t windowID = static_cast<uint64_t>(gtk_socket_get_id(socket));
//   // pluginWindowMap().remove(windowID);
//    return FALSE;
//}
void WebPageProxy::createPluginContainer(uint64_t& windowID)
//void WebPageProxy::createPluginContainer()
{
	syslog(LOG_INFO, "createpluincontainer");
    static int id = 0;
    id += 1023;
    windowID = id;

   // pluginWindowMap().set(windowID,socket);
      //notImplemented();

//    GtkWidget* socket = gtk_socket_new();
//    g_signal_connect(socket, "plug-removed", G_CALLBACK(pluginContainerPlugRemoved), 0);
//    gtk_container_add(GTK_CONTAINER(viewWidget()), socket);
//
//    windowID = static_cast<uint64_t>(gtk_socket_get_id(GTK_SOCKET(socket)));
    //pluginWindowMap().set(windowID, socket);
}

//void WebPageProxy::windowedPluginGeometryDidChange(const WebCore::IntRect& frameRect, const WebCore::IntRect& clipRect, uint64_t windowID)
//{
//    GtkWidget* plugin = pluginWindowMplugin.contrap().get(windowID);
//    if (!plugin)
//        return;
//
//    if (gtk_widget_get_realized(plugin)) {
//        GdkRectangle clip = clipRect;
//        cairo_region_t* clipRegion = cairo_region_create_rectangle(&clip);
//        gdk_window_shape_combine_region(gtk_widget_get_window(plugin), clipRegion, 0, 0);
//        cairo_region_destroy(clipRegion);
//    }
//
//    webkitWebViewBaseChildMoveResize(WEBKIT_WEB_VIEW_BASE(viewWidget()), plugin, frameRect);
//}
//
//void WebPageProxy::windowedPluginVisibilityDidChange(bool isVisible, uint64_t windowID)
//{
//    GtkWidget* plugin = pluginWindowMap().get(windowID);
//    if (!plugin)
//        return;
//
//    if (isVisible)
//        gtk_widget_show(plugin);
//    else
//        gtk_widget_hide(plugin);
//}
#endif // PLUGIN_ARCHITECTURE(X11) ||PLUGIN_ARCHITECTURE(WayLand)



} // namespace WebKit




