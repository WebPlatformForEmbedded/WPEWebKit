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
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "PlatformDisplayWayland.h"

#if PLATFORM(WAYLAND)

#include "GLContextEGL.h"
#include "WaylandSurface.h"
#include <cstring>
#include <glib.h>
#include <wtf/Assertions.h>

//KEYBOARD SUPPORT
#if !PLATFORM(GTK)
#include <cassert>
#include <sys/mman.h>
#include <unistd.h>
#endif
//KEYBOARD SUPPORT
//MOUSE SUPPORT
#include <linux/input.h>
//MOUSE SUPPORT

namespace WebCore {
 
#if !PLATFORM(GTK)

typedef struct _GSource GSource;

class EventSource {
public:
    static GSourceFuncs sourceFuncs1;
 
    GSource source;
    GPollFD pfd;
    struct wl_display* display;
};

GSourceFuncs EventSource::sourceFuncs1 = {
    // prepare
    [](GSource* base, gint* timeout) -> gboolean
    {
        auto* source = reinterpret_cast<EventSource*>(base);
        struct wl_display* display = source->display;

        *timeout = -1;
        wl_display_flush(display);
        wl_display_dispatch_pending(display);

        return FALSE;
    },
    // check
    [](GSource* base) -> gboolean
    {
        auto* source = reinterpret_cast<EventSource*>(base);
        return !!source->pfd.revents;
    },
    // dispatch
    [](GSource* base, GSourceFunc, gpointer) -> gboolean
    {
        auto* source = reinterpret_cast<EventSource*>(base);
        struct wl_display* display = source->display;

        if (source->pfd.revents & G_IO_IN)
            wl_display_dispatch(display);

        if (source->pfd.revents & (G_IO_ERR | G_IO_HUP))
            return FALSE;

        source->pfd.revents = 0;
        return TRUE;
    },
    nullptr, // finalize
    nullptr, // closure_callback
    nullptr, // closure_marshall
};
//KEYBOARD SUPPORT
static void
handleKeyEvent(PlatformDisplayWayland::SeatData& seatData, uint32_t key, uint32_t state, uint32_t time)
{
    auto& xkb = seatData.xkb;
    uint32_t keysym = xkb_state_key_get_one_sym(xkb.state, key);
    uint32_t unicode = xkb_state_key_get_utf32(xkb.state, key);

    if (state == WL_KEYBOARD_KEY_STATE_PRESSED
        && xkb_compose_state_feed(xkb.composeState, keysym) == XKB_COMPOSE_FEED_ACCEPTED
        && xkb_compose_state_get_status(xkb.composeState) == XKB_COMPOSE_COMPOSED)
    {
        keysym = xkb_compose_state_get_one_sym(xkb.composeState);
        unicode = xkb_keysym_to_utf32(keysym);
    }
    seatData.inputHandler->handleKeyboardEvent({ time, keysym, unicode, !!state, xkb.modifiers });
}

static gboolean
repeatRateTimeout(void* data)
{
      auto& seatData = *static_cast<PlatformDisplayWayland::SeatData*>(data);
          handleKeyEvent(seatData, seatData.repeatData.key, seatData.repeatData.state, seatData.repeatData.time);
              return G_SOURCE_CONTINUE;
}

static gboolean
repeatDelayTimeout(void* data)
{
      auto& seatData = *static_cast<PlatformDisplayWayland::SeatData*>(data);
         handleKeyEvent(seatData, seatData.repeatData.key, seatData.repeatData.state, seatData.repeatData.time);
              seatData.repeatData.eventSource = g_timeout_add(seatData.repeatInfo.rate, static_cast<GSourceFunc>(repeatRateTimeout), data);
                  return G_SOURCE_REMOVE;
}

static const struct wl_keyboard_listener g_keyboardListener = {
    // keymap
    [](void* data, struct wl_keyboard*, uint32_t format, int fd, uint32_t size)
    {
        if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
            close(fd);
            return;
        }

        void* mapping = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
        if (mapping == MAP_FAILED) {
            close(fd);
            return;
        }

        auto& xkb = static_cast<PlatformDisplayWayland::SeatData*>(data)->xkb;
        xkb.keymap = xkb_keymap_new_from_string(xkb.context, static_cast<char*>(mapping),
            XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
        munmap(mapping, size);
        close(fd);

        if (!xkb.keymap)
            return;

        xkb.state = xkb_state_new(xkb.keymap);
        if (!xkb.state)
            return;

        xkb.indexes.control = xkb_keymap_mod_get_index(xkb.keymap, XKB_MOD_NAME_CTRL);
        xkb.indexes.alt = xkb_keymap_mod_get_index(xkb.keymap, XKB_MOD_NAME_ALT);
        xkb.indexes.shift = xkb_keymap_mod_get_index(xkb.keymap, XKB_MOD_NAME_SHIFT);
    },
    // enter
    [](void* data, struct wl_keyboard*, uint32_t serial, struct wl_surface* surface, struct wl_array*)
    {
        auto& seatData = *static_cast<PlatformDisplayWayland::SeatData*>(data);
        seatData.serial = serial;
        auto it = seatData.inputClients.find(surface);
        if (it != seatData.inputClients.end())
            seatData.keyboard.target = *it;
    },
    // leave
    [](void* data, struct wl_keyboard*, uint32_t serial, struct wl_surface* surface)
    {
        auto& seatData = *static_cast<PlatformDisplayWayland::SeatData*>(data);
        seatData.serial = serial;
        auto it = seatData.inputClients.find(surface);
        if (it != seatData.inputClients.end() && seatData.keyboard.target.first == it->first)
            seatData.keyboard.target = { };
    },
    // key
    [](void* data, struct wl_keyboard*, uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
    {
        // IDK.
        key += 8;

        auto& seatData = *static_cast<PlatformDisplayWayland::SeatData*>(data);
        seatData.serial = serial;
        handleKeyEvent(seatData, key, state, time);

        if (!seatData.repeatInfo.rate)
            return;

        if (state == WL_KEYBOARD_KEY_STATE_RELEASED
            && seatData.repeatData.key == key) {
            if (seatData.repeatData.eventSource)
                g_source_remove(seatData.repeatData.eventSource);
            seatData.repeatData = { 0, 0, 0, 0 };
        } else if (state == WL_KEYBOARD_KEY_STATE_PRESSED
            && xkb_keymap_key_repeats(seatData.xkb.keymap, key)) {

            if (seatData.repeatData.eventSource)
                g_source_remove(seatData.repeatData.eventSource);

            seatData.repeatData = { key, time, state, g_timeout_add(seatData.repeatInfo.delay, static_cast<GSourceFunc>(repeatDelayTimeout), data) };
        }

    },
    // modifiers
    [](void* data, struct wl_keyboard*, uint32_t serial, uint32_t depressedMods, uint32_t latchedMods, uint32_t lockedMods, uint32_t group)
    {

        static_cast<PlatformDisplayWayland::SeatData*>(data)->serial = serial;
        auto& xkb = static_cast<PlatformDisplayWayland::SeatData*>(data)->xkb;
        xkb_state_update_mask(xkb.state, depressedMods, latchedMods, lockedMods, 0, 0, group);

        auto& modifiers = xkb.modifiers;
        modifiers = 0;
        auto component = static_cast<xkb_state_component>(XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_LATCHED);
        if (xkb_state_mod_index_is_active(xkb.state, xkb.indexes.control, component))
            modifiers |= WPE::Input::KeyboardEvent::Control;
        if (xkb_state_mod_index_is_active(xkb.state, xkb.indexes.alt, component))
            modifiers |= WPE::Input::KeyboardEvent::Alt;
        if (xkb_state_mod_index_is_active(xkb.state, xkb.indexes.shift, component))
            modifiers |= WPE::Input::KeyboardEvent::Shift;

    },
    // repeat_info
    [](void* data, struct wl_keyboard*, int32_t rate, int32_t delay)
    {
        auto& repeatInfo = static_cast<PlatformDisplayWayland::SeatData*>(data)->repeatInfo;
        repeatInfo = { rate, delay };

        // A rate of zero disables any repeating.
        if (!rate) {
            auto& repeatData = static_cast<PlatformDisplayWayland::SeatData*>(data)->repeatData;
            if (repeatData.eventSource) {
                g_source_remove(repeatData.eventSource);
                repeatData = { 0, 0, 0, 0 };
            }
        }
    },
};


static const struct wl_pointer_listener g_pointerListener = {
    // enter
    [](void* data, struct wl_pointer*, uint32_t serial, struct wl_surface* surface, wl_fixed_t, wl_fixed_t)
    {
        auto& seatData = *static_cast<PlatformDisplayWayland::SeatData*>(data);
        seatData.serial = serial;
        auto it = seatData.inputClients.find(surface);
        if (it != seatData.inputClients.end())
            seatData.pointer.target = *it;
    },
    // leave
    [](void* data, struct wl_pointer*, uint32_t serial, struct wl_surface* surface)
    {
        auto& seatData = *static_cast<PlatformDisplayWayland::SeatData*>(data);
        seatData.serial = serial;
        auto it = seatData.inputClients.find(surface);
        if (it != seatData.inputClients.end() && seatData.pointer.target.first == it->first)
            seatData.pointer.target = { };
    },
    // motion
    [](void* data, struct wl_pointer*, uint32_t time, wl_fixed_t fixedX, wl_fixed_t fixedY)
    {
        auto x = wl_fixed_to_int(fixedX);
        auto y = wl_fixed_to_int(fixedY);
        auto& seatData = *static_cast<PlatformDisplayWayland::SeatData*>(data);
        seatData.pointer.coords = { x, y };
        seatData.inputHandler->handlePointerEvent({ WPE::Input::PointerEvent::Motion, time, x, y, 0, 0 });
    },
    // button
    [](void* data, struct wl_pointer*, uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
    {
	printf("PlatformDisplayWayland::button pressed\n");
        static_cast<PlatformDisplayWayland::SeatData*>(data)->serial = serial;

        if (button >= BTN_MOUSE)
            button = button - BTN_MOUSE + 1;
        else
            button = 0;

        auto& seatData = *static_cast<PlatformDisplayWayland::SeatData*>(data);
        auto& coords = seatData.pointer.coords;
        seatData.inputHandler->handlePointerEvent(
                { WPE::Input::PointerEvent::Button, time, coords.first, coords.second, button, state });
    },
    // axis
    [](void* data, struct wl_pointer*, uint32_t time, uint32_t axis, wl_fixed_t value)
    {
        auto& seatData = *static_cast<PlatformDisplayWayland::SeatData*>(data);
        auto& coords = seatData.pointer.coords;
        seatData.inputHandler->handleAxisEvent(
                { WPE::Input::AxisEvent::Motion, time, coords.first, coords.second, axis, -wl_fixed_to_int(value) });  
    },
};

static const struct wl_seat_listener g_seatListener = { 
    // capabilities
    [](void* data, struct wl_seat* seat, uint32_t capabilities)
    {   
        auto& seatData = *static_cast<PlatformDisplayWayland::SeatData*>(data);
        // WL_SEAT_CAPABILITY_POINTER
        const bool hasPointerCap = capabilities & WL_SEAT_CAPABILITY_POINTER;
        if (hasPointerCap && !seatData.pointer.object) {
            seatData.pointer.object = wl_seat_get_pointer(seat);
            wl_pointer_add_listener(seatData.pointer.object, &g_pointerListener, &seatData);
        }
        if (!hasPointerCap && seatData.pointer.object) {
            wl_pointer_destroy(seatData.pointer.object);
            seatData.pointer.object = nullptr;
        }

        // WL_SEAT_CAPABILITY_KEYBOARD
        const bool hasKeyboardCap = capabilities & WL_SEAT_CAPABILITY_KEYBOARD;
        if (hasKeyboardCap && !seatData.keyboard.object) {
            seatData.keyboard.object = wl_seat_get_keyboard(seat);
            wl_keyboard_add_listener(seatData.keyboard.object, &g_keyboardListener, &seatData);
        }
        if (!hasKeyboardCap && seatData.keyboard.object) {
            wl_keyboard_destroy(seatData.keyboard.object);
            seatData.keyboard.object = nullptr;
        }
    },  
    // name
    [](void*, struct wl_seat*, const char*) { } 
};
//KEYBOARD SUPPORT
#endif

const struct wl_registry_listener PlatformDisplayWayland::m_registryListener = {
    PlatformDisplayWayland::globalCallback,
    PlatformDisplayWayland::globalRemoveCallback
};

void PlatformDisplayWayland::globalCallback(void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t)
{
    auto display = static_cast<PlatformDisplayWayland*>(data);
    if (!std::strcmp(interface, "wl_compositor"))
        display->m_compositor = static_cast<struct wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, 1));
if (!std::strcmp(interface, "wl_shell"))
display->m_shell = static_cast<struct wl_shell*>(wl_registry_bind(registry, name, &wl_shell_interface, 1));
#if PLATFORM(GTK)
    else if (!std::strcmp(interface, "wl_webkitgtk"))
        display->m_webkitgtk = static_cast<struct wl_webkitgtk*>(wl_registry_bind(registry, name, &wl_webkitgtk_interface, 1));
#endif
//KEYBOARD SUPPORT
#if !PLATFORM(GTK)
     else if (!std::strcmp(interface, "wl_seat"))
         display->m_seat = static_cast<struct wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 4));
#endif
//KEYBOARD SUPPORT
}

//For sending pong in response to ping from server
static void
handle_ping(void *data, struct wl_shell_surface *shell_surface,
                                                       uint32_t serial)
{
       wl_shell_surface_pong(shell_surface, serial);
}

static void
handle_configure(void *data, struct wl_shell_surface *shell_surface,
                uint32_t edges, int32_t width, int32_t height)
{
}
 
static void
handle_popup_done(void *data, struct wl_shell_surface *shell_surface)
{
}

static const struct wl_shell_surface_listener shell_surface_listener = {
       handle_ping,
       handle_configure,
       handle_popup_done
};
//For sending pong in response to ping from server

void PlatformDisplayWayland::globalRemoveCallback(void*, struct wl_registry*, uint32_t)
{
    // FIXME: if this can happen without the UI Process getting shut down
    // we should probably destroy our cached display instance.
}

std::unique_ptr<PlatformDisplayWayland> PlatformDisplayWayland::create()
{
    struct wl_display* wlDisplay = wl_display_connect(nullptr);
    if (!wlDisplay) {
        WTFLogAlways("PlatformDisplayWayland initialization: failed to connect to the Wayland server socket. Check your WAYLAND_DISPLAY or WAYLAND_SOCKET environment variables.");
        return nullptr;
    }

    auto display = std::unique_ptr<PlatformDisplayWayland>(new PlatformDisplayWayland(wlDisplay));
    if (!display->isInitialized()) {
        WTFLogAlways("PlatformDisplayWayland initialization: failed to complete the initialization of the display.");
        return nullptr;
    }

    return display;
}

PlatformDisplayWayland::PlatformDisplayWayland(struct wl_display* wlDisplay)
    : m_display(wlDisplay)
    , m_registry(wl_display_get_registry(m_display))
    , m_eglConfigChosen(false)
{
    wl_registry_add_listener(m_registry, &m_registryListener, this);
    wl_display_roundtrip(m_display);

#if !PLATFORM(GTK)
    m_eventSource = g_source_new(&EventSource::sourceFuncs1, sizeof(EventSource));
    auto* source = reinterpret_cast<EventSource*>(m_eventSource);
    source->display = wlDisplay;

    source->pfd.fd = wl_display_get_fd(wlDisplay);
    source->pfd.events = G_IO_IN | G_IO_ERR | G_IO_HUP;
    source->pfd.revents = 0;
    g_source_add_poll(m_eventSource, &source->pfd);

    g_source_set_name(m_eventSource, "[WPE] PlatformDisplayWayland");
    g_source_set_priority(m_eventSource, G_PRIORITY_HIGH + 30);
    g_source_set_can_recurse(m_eventSource, TRUE);
    g_source_attach(m_eventSource, g_main_context_get_thread_default());
//KEYBOARD SUPPORT
    wl_seat_add_listener(m_seat, &g_seatListener, &m_seatData);
    m_seatData.xkb.context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    m_seatData.xkb.composeTable = xkb_compose_table_new_from_locale(m_seatData.xkb.context, setlocale(LC_CTYPE, nullptr), XKB_COMPOSE_COMPILE_NO_FLAGS);
    if (m_seatData.xkb.composeTable)
       m_seatData.xkb.composeState = xkb_compose_state_new(m_seatData.xkb.composeTable, XKB_COMPOSE_STATE_NO_FLAGS);
//KEYBOARD SUPPORT
#endif

    static const EGLint configAttributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 1,
        EGL_GREEN_SIZE, 1,
        EGL_BLUE_SIZE, 1,
        EGL_ALPHA_SIZE, 1,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    m_eglDisplay = eglGetDisplay(m_display);
    if (eglInitialize(m_eglDisplay, 0, 0) == EGL_FALSE) {return;}

    EGLint numberOfConfigs;
    if (!eglChooseConfig(m_eglDisplay, configAttributes, &m_eglConfig, 1, &numberOfConfigs) || numberOfConfigs != 1) {
        g_warning("PlatformDisplayWayland initialization: failed to find the desired EGL configuration.");
        return;
    }

    m_eglConfigChosen = true;
}

PlatformDisplayWayland::~PlatformDisplayWayland()
{
#if PLATFORM(GTK)
    if (m_webkitgtk)
        wl_webkitgtk_destroy(m_webkitgtk);
#endif
    if (m_compositor)
        wl_compositor_destroy(m_compositor);
    if (m_registry)
        wl_registry_destroy(m_registry);
    if (m_display)
        wl_display_disconnect(m_display);
#if !PLATFORM(GTK)
//KEYBOARD SUPPORT
    if (m_seat)
        wl_seat_destroy(m_seat);
    if (m_seatData.xkb.context)
        xkb_context_unref(m_seatData.xkb.context);
    if (m_seatData.xkb.keymap)
        xkb_keymap_unref(m_seatData.xkb.keymap);
    if (m_seatData.xkb.state)
       xkb_state_unref(m_seatData.xkb.state);
    if (m_seatData.xkb.composeTable)
        xkb_compose_table_unref(m_seatData.xkb.composeTable);
    if (m_seatData.xkb.composeState)
        xkb_compose_state_unref(m_seatData.xkb.composeState);
//KEYBOARD SUPPORT
#endif
}

std::unique_ptr<WaylandSurface> PlatformDisplayWayland::createSurface(const IntSize& size, int widgetId)
{
    struct wl_surface* wlSurface = wl_compositor_create_surface(m_compositor);

    struct wl_shell_surface *shell_surface;
    shell_surface = wl_shell_get_shell_surface(m_shell, wlSurface);

    if (shell_surface)
	    wl_shell_surface_add_listener(shell_surface,
		    &shell_surface_listener, NULL);
    wl_shell_surface_set_toplevel(shell_surface);

    struct wl_region *region;    
    region = wl_compositor_create_region(m_compositor);
    wl_region_add(region, 0, 0,
		  std::max(1, size.width()),
	  std::max(1, size.height()));
    wl_surface_set_opaque_region(wlSurface, region);

    // We keep the minimum size at 1x1px since Mesa returns null values in wl_egl_window_create() for zero width or height.
    EGLNativeWindowType nativeWindow = wl_egl_window_create(wlSurface, std::max(1, size.width()), std::max(1, size.height()));

#if PLATFORM(GTK)
    wl_webkitgtk_set_surface_for_widget(m_webkitgtk, wlSurface, widgetId);
#endif

    return std::make_unique<WaylandSurface>(wlSurface, nativeWindow);
}

std::unique_ptr<GLContextEGL> PlatformDisplayWayland::createSharingGLContext()
{
    class OffscreenContextData : public GLContext::Data {
    public:
        virtual ~OffscreenContextData()
        {
            wl_egl_window_destroy(nativeWindow);
            wl_surface_destroy(surface);
        }

        struct wl_surface* surface;
        EGLNativeWindowType nativeWindow;
    };

    auto contextData = std::make_unique<OffscreenContextData>();
    contextData->surface = wl_compositor_create_surface(m_compositor);
    contextData->nativeWindow = wl_egl_window_create(contextData->surface, 1, 1);

    auto nativeWindow = contextData->nativeWindow;
    return GLContextEGL::createWindowContext(nativeWindow, nullptr, WTFMove(contextData));
}

//KEYBOARD SUPPORT
#if !PLATFORM(GTK)
void PlatformDisplayWayland::registerInputClient(struct wl_surface* surface, WPE::Input::Client* client)
{
      m_seatData.inputHandler = client; 
      auto result = m_seatData.inputClients.insert({ surface, client });
      assert(result.second);
}
void PlatformDisplayWayland::unregisterInputClient(struct wl_surface* surface)
{
      auto it = m_seatData.inputClients.find(surface);
      assert(it != m_seatData.inputClients.end());

      if (m_seatData.keyboard.target.first == it->first)
        m_seatData.keyboard.target = { };
      m_seatData.inputClients.erase(it);
}
#endif
//KEYBOARD SUPPORT
} // namespace WebCore

#endif // PLATFORM(WAYLAND)
