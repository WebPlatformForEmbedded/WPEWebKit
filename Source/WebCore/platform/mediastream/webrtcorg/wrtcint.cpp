#include "wrtcint.h"

#include <glib.h>

#include <condition_variable>
#include <limits>
#include <mutex>
#include <unordered_map>

#include "webrtc/common_types.h"

#include "webrtc/base/common.h"
#include "webrtc/base/nullsocketserver.h"
#include "webrtc/base/refcount.h"
#include "webrtc/base/scoped_ref_ptr.h"
#include "webrtc/base/ssladapter.h"

#include "webrtc/api/datachannelinterface.h"
#include "webrtc/api/mediaconstraintsinterface.h"
#include "webrtc/api/mediastreaminterface.h"
#include "webrtc/api/peerconnectionfactory.h"

#include "webrtc/media/base/videocapturerfactory.h"
#include "webrtc/media/engine/webrtcvideocapturer.h"
#include "webrtc/media/engine/webrtcvideocapturerfactory.h"
#include "webrtc/modules/audio_device/include/audio_device_defines.h"
#include "webrtc/modules/video_capture/video_capture_factory.h"
#include "webrtc/voice_engine/include/voe_base.h"
#include "webrtc/voice_engine/include/voe_hardware.h"

// TODO: Make it possible to overload/override device specific hooks
static webrtc::AudioDeviceModule* CreateAudioDeviceModule(int32_t) {
    return nullptr;
}
static cricket::WebRtcVideoEncoderFactory* CreateWebRtcVideoEncoderFactory() {
    return nullptr;
}
static cricket::WebRtcVideoDecoderFactory* CreateWebRtcVideoDecoderFactory() {
    return nullptr;
}
static cricket::VideoDeviceCapturerFactory* CreateVideoDeviceCapturerFactory() {
    return nullptr;
}

namespace {

GMainLoop *gMainLoop = nullptr;

};  // namespace

void WRTCInt::enumerateDevices(DeviceType type, std::vector<std::string>& devices)
{
    if (WRTCInt::AUDIO == type) {
        webrtc::VoiceEngine* voe = webrtc::VoiceEngine::Create();
        if (!voe) {
            LOG(LS_ERROR) << "Failed to create VoiceEngine";
            return;
        }
        webrtc::VoEBase* base = webrtc::VoEBase::GetInterface(voe);
        webrtc::AudioDeviceModule* externalADM = CreateAudioDeviceModule(0);
        if (base->Init(externalADM) != 0) {
            LOG(LS_ERROR) << "Failed to init VoEBase";
            base->Release();
            webrtc::VoiceEngine::Delete(voe);
            return;
        }
        webrtc::VoEHardware* hardware = webrtc::VoEHardware::GetInterface(voe);
        if (!hardware) {
            LOG(LS_ERROR) << "Failed to get interface to VoEHardware";
            base->Terminate();
            base->Release();
            webrtc::VoiceEngine::Delete(voe);
            return;
        }
        int numOfRecordingDevices;
        if (hardware->GetNumOfRecordingDevices(numOfRecordingDevices) != -1) {
            for (int i = 0; i < numOfRecordingDevices; ++i) {
                char name[webrtc::kAdmMaxDeviceNameSize];
                char guid[webrtc::kAdmMaxGuidSize];
                if (hardware->GetRecordingDeviceName(i, name, guid) != -1) {
                    devices.push_back(name);
                }
            }
        } else {
            LOG(LS_ERROR) << "Failed to get number of recording devices";
        }
        base->Terminate();
        base->Release();
        hardware->Release();
        webrtc::VoiceEngine::Delete(voe);
        return;
    }
    ASSERT(WRTCInt::VIDEO == type);
    std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo> info(
        webrtc::VideoCaptureFactory::CreateDeviceInfo(0));
    if (!info) {
        LOG(LS_ERROR) << "Failed to get video capture device info";
        return;
    }
    std::unique_ptr<cricket::VideoDeviceCapturerFactory> factory(
       CreateVideoDeviceCapturerFactory());
    if (!factory) {
       factory.reset(new cricket::WebRtcVideoDeviceCapturerFactory);
    }
    int numOfDevices = info->NumberOfDevices();
    for (int i = 0; i < numOfDevices; ++i) {
        const uint32_t kSize = 256;
        char name[kSize] = {0};
        char id[kSize] = {0};
        if (info->GetDeviceName(i, name, kSize, id, kSize) != -1) {
            devices.push_back(name);
            break;
        } else {
            LOG(LS_ERROR) << "Failed to create capturer for: '" << name << "'";
        }
    }
}

void WRTCInt::init()
{
    // TODO: Route RTC logs to WebCore's logger
    const char* logConfig = getenv("WRTC_LOG");
    if (logConfig == nullptr) {
        logConfig = "tstamp info debug";
    }
    rtc::LogMessage::ConfigureLogging(logConfig);
    rtc::InitializeSSL();
    rtc::ThreadManager::Instance();
}

void WRTCInt::shutdown()
{
    rtc::CleanupSSL();
}
