#ifndef _PEERCONNECTIONBACKENDWEBRTCORG_H_
#define _PEERCONNECTIONBACKENDWEBRTCORG_H_

#include "PeerConnectionBackend.h"
#include "NotImplemented.h"

#include "RealtimeMediaSourceCenterWebRtcOrg.h"

#include "RTCPeerConnection.h"
#include "RTCDataChannelHandlerClient.h"
#include "RTCDataChannelHandler.h"
#include "RTCDataChannel.h"
#include "RTCDataChannelHandlerClient.h"

#include "webrtc/api/peerconnectioninterface.h"
#include "webrtc/api/datachannelinterface.h"
#include "webrtc/api/mediaconstraintsinterface.h"
#include "webrtc/api/mediastreaminterface.h"
#include "webrtc/api/peerconnectionfactory.h"

#include <wtf/HashMap.h>
#include <wtf/RefPtr.h>

namespace WebCore {

enum {
    InvalidRequestId = -1
};

class PeerConnectionBackendClient;

class PeerConnectionBackendWebRtcOrg : public PeerConnectionBackend, public webrtc::PeerConnectionObserver {
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
#if 1
    // WRTCInt::RTCPeerConnectionClient
    virtual void requestSucceeded(int id, const RTCSessionDescription& desc);
    virtual void requestSucceeded(int id, const std::vector<std::unique_ptr<RTCStatsReport>>& reports);
    virtual void requestSucceeded(int id);
    virtual void requestFailed(int id, const std::string& error);
    virtual void negotiationNeeded();
    virtual void didAddRemoteStream(WebCore::RTCMediaStream *stream,
                                    const std::vector<std::string> &audioSources,
                                    const std::vector<std::string> &videoSources);
    virtual void didGenerateIceCandidate(const WebCore::RTCIceCandidate& candidate);

    virtual void didChangeSignalingState(webrtc::PeerConnectionInterface::SignalingState state);
    virtual void didChangeIceGatheringState(webrtc::PeerConnectionInterface::IceGatheringState state);
    virtual void didChangeIceConnectionState(webrtc::PeerConnectionInterface::IceConnectionState state);
    virtual void didAddRemoteDataChannel(WebCore::RTCDataChannel* channel);
#endif
	//PeerConnectionObserver
  	// Triggered when the SignalingState changed.
  	virtual void OnSignalingChange(
    		 PeerConnectionInterface::SignalingState new_state) override;

  	// Triggered when media is received on a new stream from remote peer.
  	virtual void OnAddStream(MediaStreamInterface* stream) override;

  	// Triggered when a remote peer close a stream.
  	virtual void OnRemoveStream(MediaStreamInterface* stream) override;

  	// Triggered when a remote peer open a data channel.
  	virtual void OnDataChannel(DataChannelInterface* data_channel) override;

  	// Triggered when renegotiation is needed, for example the ICE has restarted.
  	virtual void OnRenegotiationNeeded() override;

  	// Called any time the IceConnectionState changes
  	virtual void OnIceConnectionChange(
    	  PeerConnectionInterface::IceConnectionState new_state) override;

	// Called any time the IceGatheringState changes
  	virtual void OnIceGatheringChange(
      	PeerConnectionInterface::IceGatheringState new_state) override;

  	// New Ice candidate have been found.
  	virtual void OnIceCandidate(const IceCandidateInterface* candidate) override;

	//Create webrtc::PeerConnectionInterface
	void createPeerConnection();
private:
	void ensurePeerConnectionFactory();
	rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> m_peerConnectionFactory;
    std::unique_ptr<rtc::Thread> m_workerThread;
    std::unique_ptr<rtc::Thread> m_signalingThread;
	rtc::scoped_refptr<webrtc::PeerConnectionInterface> m_peerConnection;
	webrtc::MediaConstraintsInterface* m_constraints;

	webrtc::PeerConnectionInterface::IceServers m_servers;

    PeerConnectionBackendClient* m_client;
//    std::unique_ptr<RTCPeerConnection> m_rtcConnection;

    bool m_isNegotiationNeeded { false };
    int m_sessionDescriptionRequestId { InvalidRequestId };
    Optional<PeerConnection::SessionDescriptionPromise> m_sessionDescriptionPromise;
    int m_voidRequestId { InvalidRequestId };
    Optional<PeerConnection::VoidPromise> m_voidPromise;
    HashMap<int, Optional<PeerConnection::StatsPromise>> m_statsPromises;
};

class RTCDataChannelHandlerWebRtcOrg
    : public RTCDataChannelHandler
    , public RTCDataChannelHandlerClient
{
public:
    RTCDataChannelHandlerWebRtcOrg(RTCDataChannel* dataChannel);

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

    // WRTCDataChannelClient
    void didChangeReadyState(RTCDataChannelHandlerClient::ReadyState state) override;
    void didReceiveStringData(const String& str) override;
    void didReceiveRawData(const char* data, size_t sz) override;
	void didDetectError() override;
private:
    std::unique_ptr<RTCDataChannel> m_rtcDataChannel;
    RTCDataChannelHandlerClient* m_client;
};

}
#endif  // _PEERCONNECTIONBACKENDWEBRTCORG_H_
