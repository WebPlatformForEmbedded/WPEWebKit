#ifndef _PEERCONNECTIONBACKENDWEBRTCORG_H_
#define _PEERCONNECTIONBACKENDWEBRTCORG_H_

#include "PeerConnectionBackend.h"
#include "NotImplemented.h"

#include "RealtimeMediaSourceCenterWebRtcOrg.h"
#include "RTCDataChannelHandler.h"

#include <wtf/HashMap.h>
#include <wtf/RefPtr.h>

namespace WRTCInt
{
class  RTCStatsReport
{
public:
    typedef std::pair<std::string, std::string> Value;
    virtual ~RTCStatsReport() = default;
    virtual double timestamp() const = 0;
    virtual std::string id() const = 0;
    virtual std::string type() const = 0;
    virtual std::vector<Value> values() const = 0;
};

class  RTCMediaStream
{
public:
    virtual ~RTCMediaStream() = default;
    virtual std::string id() const = 0;
};

/*
class  RTCDataChannelClient
{
public:
    virtual ~RTCDataChannelClient() = default;
    virtual void didChangeReadyState(DataChannelState) = 0;
    virtual void didReceiveStringData(const std::string&) = 0;
    virtual void didReceiveRawData(const char*, size_t) = 0;
};

class  RTCDataChannel
{
public:
    virtual ~RTCDataChannel() = default;
    virtual std::string label() const = 0;
    virtual bool ordered() const = 0;
    virtual unsigned short maxRetransmitTime() const = 0;
    virtual unsigned short maxRetransmits() const  = 0;
    virtual std::string protocol() const = 0;
    virtual bool negotiated() const = 0;
    virtual unsigned short id() = 0;
    virtual unsigned long bufferedAmount() = 0;
    virtual bool sendStringData(const std::string&) = 0;
    virtual bool sendRawData(const char*, size_t) = 0;
    virtual void close() = 0;
    virtual void setClient(WRTCInt::RTCDataChannelClient* client) = 0;
};

class  RTCPeerConnectionClient
{
public:
    virtual ~RTCPeerConnectionClient() = default;
    virtual void requestSucceeded(int id, const RTCSessionDescription& desc) = 0;
    virtual void requestSucceeded(int id, const std::vector<std::unique_ptr<WRTCInt::RTCStatsReport>>& reports) = 0;
    virtual void requestSucceeded(int id) = 0;
    virtual void requestFailed(int id, const std::string& error) = 0;
    virtual void negotiationNeeded() = 0;
    virtual void didAddRemoteStream(RTCMediaStream *stream,
                                    const std::vector<std::string> &audioSources,
                                    const std::vector<std::string> &videoSources) = 0;
    virtual void didGenerateIceCandidate(const RTCIceCandidate& candidate) = 0;
    virtual void didChangeSignalingState(SignalingState state) = 0;
    virtual void didChangeIceGatheringState(IceGatheringState state) = 0;
    virtual void didChangeIceConnectionState(IceConnectionState state) = 0;
    virtual void didAddRemoteDataChannel(RTCDataChannel* channel) = 0;
};

class  RTCPeerConnection
{
public:
    virtual ~RTCPeerConnection() = default;

    virtual bool setConfiguration(const RTCConfiguration &config, const RTCMediaConstraints& constraints) = 0;

    virtual int createOffer(const RTCOfferAnswerOptions &options) = 0;
    virtual int createAnswer(const RTCOfferAnswerOptions &options) = 0;

    virtual bool localDescription(RTCSessionDescription& desc) = 0;
    virtual int setLocalDescription(const RTCSessionDescription& desc) = 0;

    virtual bool remoteDescription(RTCSessionDescription& desc) = 0;
    virtual int setRemoteDescription(const RTCSessionDescription& desc) = 0;

    virtual bool addIceCandidate(const RTCIceCandidate& candidate) = 0;
    virtual bool addStream(RTCMediaStream* stream) = 0;
    virtual bool removeStream(RTCMediaStream* stream) = 0;

    virtual int getStats() = 0;

    virtual RTCDataChannel* createDataChannel(const std::string &label, const DataChannelInit& initData) = 0;

    virtual void stop() = 0;

    virtual RTCPeerConnectionClient* client() = 0;
};
*/

RTCMediaSourceCenter* createRTCMediaSourceCenter();
}

namespace
{
class MockRTCDataChannel;
}

namespace WebCore {

class PeerConnectionBackendClient;

class PeerConnectionBackendWebRtcOrg : public PeerConnectionBackend /*, public WRTCInt::RTCPeerConnectionClient*/ {
public:
    PeerConnectionBackendWebRtcOrg(PeerConnectionBackendClient*);

    virtual void createOffer(RTCOfferOptions&, PeerConnection::SessionDescriptionPromise&&) override;
    virtual void createAnswer(RTCAnswerOptions&, PeerConnection::SessionDescriptionPromise&&) override;

    virtual void setLocalDescription(RTCSessionDescription&, PeerConnection::VoidPromise&&) override;
    virtual RefPtr<RTCSessionDescription> localDescription() const override;
    virtual RefPtr<RTCSessionDescription> currentLocalDescription() const override;
    virtual RefPtr<RTCSessionDescription> pendingLocalDescription() const override;

    virtual void setRemoteDescription(RTCSessionDescription&, PeerConnection::VoidPromise&&) override;
    virtual RefPtr<RTCSessionDescription> remoteDescription() const override;
    virtual RefPtr<RTCSessionDescription> currentRemoteDescription() const override;
    virtual RefPtr<RTCSessionDescription> pendingRemoteDescription() const override;

    virtual void setConfiguration(RTCConfiguration&, const MediaConstraints&) override;
    virtual void addIceCandidate(RTCIceCandidate&, PeerConnection::VoidPromise&&) override;

    virtual void getStats(MediaStreamTrack*, PeerConnection::StatsPromise&&) override;

    virtual void replaceTrack(RTCRtpSender&, MediaStreamTrack&, PeerConnection::VoidPromise&&) override;

    virtual void stop() override;

    virtual bool isNegotiationNeeded() const override;
    virtual void markAsNeedingNegotiation() override;
    virtual void clearNegotiationNeededState() override;

    virtual std::unique_ptr<RTCDataChannelHandler> createDataChannel(const String&, const Dictionary&);

    // WRTCInt::RTCPeerConnectionClient
    virtual void requestSucceeded(int id, const WRTCInt::RTCSessionDescription& desc);
    virtual void requestSucceeded(int id, const std::vector<std::unique_ptr<WRTCInt::RTCStatsReport>>& reports);
    virtual void requestSucceeded(int id);
    virtual void requestFailed(int id, const std::string& error);
    virtual void negotiationNeeded();
    virtual void didAddRemoteStream(WRTCInt::RTCMediaStream *stream,
                                    const std::vector<std::string> &audioSources,
                                    const std::vector<std::string> &videoSources);
    virtual void didGenerateIceCandidate(const WRTCInt::RTCIceCandidate& candidate);

    virtual void didChangeSignalingState(WRTCInt::SignalingState state);
    virtual void didChangeIceGatheringState(WRTCInt::IceGatheringState state);
    virtual void didChangeIceConnectionState(WRTCInt::IceConnectionState state);
    virtual void didAddRemoteDataChannel(/*WRTCInt::RTCDataChannel*/ MockRTCDataChannel* channel);

private:
    PeerConnectionBackendClient* m_client;
    std::unique_ptr</*WRTCInt::RTCPeerConnection*/ MockRTCPeerConnection> m_rtcConnection;

    bool m_isNegotiationNeeded { false };
    int m_sessionDescriptionRequestId { WRTCInt::InvalidRequestId };
    Optional<PeerConnection::SessionDescriptionPromise> m_sessionDescriptionPromise;
    int m_voidRequestId { WRTCInt::InvalidRequestId };
    Optional<PeerConnection::VoidPromise> m_voidPromise;
    HashMap<int, Optional<PeerConnection::StatsPromise>> m_statsPromises;
};

class RTCDataChannelHandlerWebRtcOrg
    : public RTCDataChannelHandler
    /*, public WRTCInt::RTCDataChannelClient*/
{
public:
    RTCDataChannelHandlerWebRtcOrg(/*WRTCInt::RTCDataChannel*/ MockRTCDataChannel* dataChannel);

    // RTCDataChannelHandler
    void setClient(RTCDataChannelHandlerClient*) override;
    String label() override;
    bool ordered() override;
    unsigned short maxRetransmitTime() override;
    unsigned short maxRetransmits() override;
    String protocol() override;
    bool negotiated() override;
    unsigned short id() override;
    unsigned long bufferedAmount() override;
    bool sendStringData(const String&) override;
    bool sendRawData(const char*, size_t) override;
    void close() override;

    // WRTCInt::RTCDataChannelClient
    void didChangeReadyState(WRTCInt::DataChannelState state);
    void didReceiveStringData(const std::string& str);
    void didReceiveRawData(const char* data, size_t sz);

private:
    std::unique_ptr</*WRTCInt::RTCDataChannel*/ MockRTCDataChannel> m_rtcDataChannel;
    RTCDataChannelHandlerClient* m_client;
};

}
#endif  // _PEERCONNECTIONBACKENDWEBRTCORG_H_
