#include "TelemetryReport.h"
#include <map>
#include <string>
#include <cstdarg>

#ifdef USE_RDK_TELEMETRY
#include "odhott/av_pipeline.h"
#include "odhott_wl.h"
#include "odherr_ctx.h"
#endif

namespace Telemetry
{

#ifdef USE_RDK_TELEMETRY

namespace AVP = OttReports::AvPipeline;

static AVP::AvPipeline AvPipelineReport(OttReports::Owner::type::Wpe);

static std::map<avpipeline_state_t, AVP::State::type> pipelineState {
    {avpipeline_state_t::CREATE, AVP::State::type::Create},
    {avpipeline_state_t::PLAY, AVP::State::type::Play},
    {avpipeline_state_t::PAUSE, AVP::State::type::Pause},
    {avpipeline_state_t::STOP, AVP::State::type::Stop},
    {avpipeline_state_t::DESTROY, AVP::State::type::Destroy},
    {avpipeline_state_t::FIRST_FRAME_DECODED, AVP::State::type::FirstFrameDecoded},
    {avpipeline_state_t::END_OF_STREAM, AVP::State::type::EndOfStream},
    {avpipeline_state_t::DECRYPT_ERROR, AVP::State::type::DecryptError},
    {avpipeline_state_t::PLAYBACK_ERROR, AVP::State::type::PlaybackError},
    {avpipeline_state_t::DRM_ERROR, AVP::State::type::DrmError},
    {avpipeline_state_t::ERROR, AVP::State::type::Error},
    {avpipeline_state_t::SEEK_START, AVP::State::type::SeekStart},
    {avpipeline_state_t::SEEK_DONE, AVP::State::type::SeekDone},
    {avpipeline_state_t::VIDEO_RESOLUTION_CHANGED, AVP::State::type::VideoResolutionChanged},
    {avpipeline_state_t::UNKNOWN,AVP::State::type::Unknown}
};

static std::map<drm_type_t, AVP::Drm::type> drmType {
    {drm_type_t::PLAYREADY, AVP::Drm::type::Playready},
    {drm_type_t::WIDEVINE, AVP::Drm::type::Widevine},
    {drm_type_t::NONE, AVP::Drm::type::None},
    {drm_type_t::UNKNOWN, AVP::Drm::type::Unknown},
};

static std::map<wayland_action_t, odh_report_wayland_action_t> waylandAction {
    {wayland_action_t::INIT_GFX , odh_report_wayland_action_t::ODH_REPORT_WAYLAND_ACTION_INIT_GFX},
    {wayland_action_t::DEINIT_GFX, odh_report_wayland_action_t::ODH_REPORT_WAYLAND_ACTION_DEINIT_GFX},
    {wayland_action_t::INIT_INPUTS, odh_report_wayland_action_t::ODH_REPORT_WAYLAND_ACTION_INIT_INPUTS},
    {wayland_action_t::DEINIT_INPUTS, odh_report_wayland_action_t::ODH_REPORT_WAYLAND_ACTION_DEINIT_INPUTS}
};

static std::map<wayland_graphics_state_t, bool> waylandGraphicsState {
    {wayland_graphics_state_t::GFX_NOT_INITIALIZED, false},
    {wayland_graphics_state_t::GFX_INITIALIZED, true}
};

static std::map<wayland_inputs_state_t, bool> waylandInputsState {
    {wayland_inputs_state_t::INPUTS_NOT_INITIALIZED, false},
    {wayland_inputs_state_t::INPUTS_INITIALIZED, true}
};

void init(const std::string &name)
{
    odh_error_report_init(name.c_str());
}

void deinit()
{
    odh_error_report_deinit(ODH_ERROR_REPORT_DEINIT_MODE_DEFERRED);
}

void reportPlaybackState(avpipeline_state_t state, const std::string &additional_info, media_type_t media)
{
    // TODO: Set source type (audio codec or video codec) depending on media input parameter
    AvPipelineReport.setSource(AVP::Source::type::Unknown);
    AvPipelineReport.setAdditionalInfo(additional_info.empty() ? "" : additional_info);
    AvPipelineReport.send(pipelineState[state]);
}

void reportDrmInfo(const drm_type_t drm, const std::string &additional_info)
{
    AvPipelineReport.setDrm(drmType[drm]);
    AvPipelineReport.setSource(AVP::Source::type::Unknown);
    AvPipelineReport.setAdditionalInfo(additional_info.empty() ? "" : additional_info);
    AvPipelineReport.send(AVP::State::type::Unknown);
}

void reportWaylandInfo(const WaylandInfoGetter &getter, wayland_action_t action, wayland_graphics_state_t gfx_state, wayland_inputs_state_t inputs_state)
{
    odh_ott_wayland_report(reinterpret_cast<const WaylandContextInfoGetter&>(getter), ODH_REPORT_WAYLAND_OWNER_WPE,
            waylandAction[action], waylandGraphicsState[gfx_state], waylandInputsState[inputs_state]);
}

void reportErrorV(const char* file, int line, const char* function, const char* format, va_list args)
{
    int length = vsnprintf(NULL, 0, format, args);
    if (length < 0) return;

    char *msg = (char*)malloc(length + 1);
    if (!msg) return;

    vsnprintf(msg, length + 1, format, args);

    char* backtrace = odh_error_report_sprintf("%s:%d %s", file, line, function);
    char* ctx = odh_ctx_create_json("wpe", "ss",
                                    "function", function,
                                     "file", file);
    odh_error_report_send_v3(ODH_ERROR_REPORT_SENSITIVITY_NONSENSITIVE,
                             ODH_ERROR_REPORT_LEVEL_ERROR,
                             "WPE0050",
                             nullptr,
                             msg,
                             ctx,
                             backtrace,
                             "browser");
    free(ctx);
    free(backtrace);
    free(msg);
}

void reportError(const char* file, int line, const char* function, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    reportErrorV(file, line, function, format, args);
    va_end(args);
}

#else

void init(const std::string &name)
{
}

void deinit()
{
}

void reportPlaybackState(avpipeline_state_t state, const std::string &additional_info, media_type_t media)
{
}

void reportDrmInfo(drm_type_t drm, const std::string &additional_info)
{
}

void reportWaylandInfo(const WaylandInfoGetter &getter, wayland_action_t action, wayland_graphics_state_t gfx_state, wayland_inputs_state_t inputs_state)
{
}

void reportErrorV(const char* file, int line, const char* function, const char* format, va_list args)
{
}

void reportError(const char* file, int line, const char* function, const char* format, ...)
{
}

#endif // USE(RDK_TELEMETRY)

} // namespace Telemetry
