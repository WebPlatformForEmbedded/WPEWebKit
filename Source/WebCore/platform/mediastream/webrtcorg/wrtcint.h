#ifndef _WRTCINT_H_
#define _WRTCINT_H_

#include <string>
#include <vector>
#include <map>
#include <memory>

namespace
{
class MockRTCPeerConnection;
}

namespace WebCore
{
class PeerConnectionBackendWebRtcOrg;
}

namespace WRTCInt
{

// peerconnectioninterface.h
enum SignalingState {
    Stable,
    HaveLocalOffer,
    HaveLocalPrAnswer,
    HaveRemoteOffer,
    HaveRemotePrAnswer,
    Closed,
};
enum IceGatheringState {
    IceGatheringNew,
    IceGatheringGathering,
    IceGatheringComplete
};
enum IceConnectionState {
    IceConnectionNew,
    IceConnectionChecking,
    IceConnectionConnected,
    IceConnectionCompleted,
    IceConnectionFailed,
    IceConnectionDisconnected,
    IceConnectionClosed,
};

// mediaconstraintsinterface.h
const char* const kOfferToReceiveAudio = "OfferToReceiveAudio";
const char* const kOfferToReceiveVideo = "OfferToReceiveVideo";
const char* const kVoiceActivityDetection = "VoiceActivityDetection";
const char* const kIceRestart = "IceRestart";

// datachannelinterface.h
enum DataChannelState {
    DataChannelConnecting,
    DataChannelOpen,  // The DataChannel is ready to send data.
    DataChannelClosing,
    DataChannelClosed
};

// glue
enum DeviceType {
    AUDIO,
    VIDEO
};

enum {
    InvalidRequestId = -1
};

typedef std::map<std::string, bool> RTCOfferAnswerOptions;
typedef std::map<std::string, std::string> RTCMediaConstraints;

struct RTCSessionDescription
{
    std::string type;
    std::string sdp;
};

struct RTCIceServer
{
    std::vector<std::string> urls;
    std::string credential;
    std::string username;
};

struct RTCConfiguration
{
    std::vector<RTCIceServer> iceServers;
};

struct RTCIceCandidate
{
    std::string sdp;
    std::string sdpMid;
    unsigned short sdpMLineIndex {0};
};

struct DataChannelInit
{
    bool ordered {true};
    int maxRetransmitTime {-1};
    int maxRetransmits {-1};
    std::string protocol;
    bool negotiated {false};
    int id {-1};
};

class  RTCVideoRendererClient
{
public:
    virtual ~RTCVideoRendererClient() = default;
    virtual void renderFrame(const unsigned char *data, int byteCount, int width, int height) = 0;
    virtual void punchHole(int width, int height) = 0;
};

class  RTCVideoRenderer
{
public:
    virtual ~RTCVideoRenderer() = default;
    virtual void setVideoRectangle(int x, int y, int w, int h) = 0;
};

class RTCMediaStream;
class RTCVideoRenderer;
class RTCPeerConnection;
class RTCPeerConnectionClient;

class  RTCMediaSourceCenter
{
public:
    virtual ~RTCMediaSourceCenter() = default;
    virtual RTCMediaStream* createMediaStream(const std::string& audioDeviceID, const std::string& videoDeviceID) = 0;
    virtual RTCVideoRenderer* createVideoRenderer(RTCMediaStream* stream, RTCVideoRendererClient* client) = 0;
    virtual /*RTCPeerConnection*/ MockRTCPeerConnection* createPeerConnection(/*RTCPeerConnectionClient*/ WebCore::PeerConnectionBackendWebRtcOrg* client) = 0;
};

void init();
void shutdown();
void enumerateDevices(DeviceType type, std::vector<std::string>& devices);

}  // namespace WRTCInt

#endif  // _WRTCINT_H_
