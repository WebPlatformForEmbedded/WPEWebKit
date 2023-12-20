#pragma once

#include <stdint.h>
#include <string>
#include <cstdarg>

namespace Telemetry
{

enum class avpipeline_state_t
{
    CREATE,
    PLAY,
    PAUSE,
    STOP,
    DESTROY,
    FIRST_FRAME_DECODED,
    END_OF_STREAM,
    DECRYPT_ERROR,
    PLAYBACK_ERROR,
    DRM_ERROR,
    ERROR,
    SEEK_START,
    SEEK_DONE,
    VIDEO_RESOLUTION_CHANGED,
    UNKNOWN
};

enum class media_type_t {
    AUDIO,
    VIDEO,
    NONE
};

enum class drm_type_t {
    PLAYREADY,
    WIDEVINE,
    NONE,
    UNKNOWN
};

enum class wayland_action_t
{
    INIT_GFX,
    DEINIT_GFX,
    INIT_INPUTS,
    DEINIT_INPUTS
};

enum class wayland_graphics_state_t
{
    GFX_NOT_INITIALIZED,
    GFX_INITIALIZED
};

enum class wayland_inputs_state_t
{
    INPUTS_NOT_INITIALIZED,
    INPUTS_INITIALIZED
};

/*
 * Don't include:
 * #include <EGL/egl.h>
 * #include <EGL/eglplatform.h>
 * since there are import order issues.
 * Defining needed types as void*, like WebKit does.
*/
typedef void *EGLConfig;
typedef void *EGLContext;
typedef void *EGLDisplay;
typedef void *EGLSurface;

class WaylandInfoGetter {
public:
    virtual EGLDisplay getEGLDisplay() const = 0;
    virtual EGLConfig getEGLConfig() const = 0;
    virtual EGLSurface getEGLSurface() const = 0;
    virtual EGLContext getEGLContext() const = 0;
    virtual unsigned int getWindowWidth() const = 0;
    virtual unsigned int getWindowHeight() const = 0;
};

void init(const std::string &name=std::string("WebKitBrowser"));
void deinit();
void reportPlaybackState(avpipeline_state_t state, const std::string &additional_info=std::string(), media_type_t media=media_type_t::NONE);
void reportDrmInfo(drm_type_t drm, const std::string &additional_info=std::string());
void reportWaylandInfo(const WaylandInfoGetter &getter, wayland_action_t action, wayland_graphics_state_t gfx_state, wayland_inputs_state_t inputs_state);
void reportErrorV(const char* file, int line, const char* function, const char* format, va_list args);
void reportError(const char* file, int line, const char* function, const char* format, ...);
} //namespace telemetry
