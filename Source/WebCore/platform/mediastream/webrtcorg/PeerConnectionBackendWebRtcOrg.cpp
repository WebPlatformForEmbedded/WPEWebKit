#include "config.h"
#include "PeerConnectionBackendWebRtcOrg.h"

#include "ScriptExecutionContext.h"
#include "UUID.h"

#include "DOMError.h"
#include "JSDOMError.h"
#include "JSRTCSessionDescription.h"
#include "JSRTCStatsResponse.h"
#include "RTCConfiguration.h"
#include "RTCDataChannelHandlerClient.h"
#include "RTCIceCandidate.h"
#include "RTCIceCandidateEvent.h"
#include "RTCOfferAnswerOptions.h"
#include "RTCRtpSender.h"
#include "RTCSessionDescription.h"
#include "RTCStatsResponse.h"
#include "RTCStatsReport.h"

#include "MediaStream.h"
#include "MediaStreamCreationClient.h"
#include "MediaStreamPrivate.h"
#include "MediaStreamTrack.h"
#include "MediaStreamTrackSourcesRequestClient.h"

#include "RTCOfferAnswerOptions.h"

#include "webrtc/api/peerconnectioninterface.h"
#include "webrtc/api/datachannelinterface.h"

#include "webrtc/media/base/videocapturerfactory.h"

#include "WebRtcOrgUtils.h"

namespace WebCore {

using namespace PeerConnection;
using namespace webrtc;

// mediaconstraintsinterface.h

static std::unique_ptr<PeerConnectionBackend> createPeerConnectionBackendWebRtcOrg(PeerConnectionBackendClient* client)
{
    initializeWebRtcOrg();
    return std::unique_ptr<PeerConnectionBackend>(new PeerConnectionBackendWebRtcOrg(client));
}

CreatePeerConnectionBackend PeerConnectionBackend::create = createPeerConnectionBackendWebRtcOrg;

void enableWebRtcOrgPeerConnectionBackend()
{
    PeerConnectionBackend::create = createPeerConnectionBackendWebRtcOrg;
}

PeerConnectionBackendWebRtcOrg::PeerConnectionBackendWebRtcOrg(PeerConnectionBackendClient* client)
    : m_client(client)
{
	createPeerConnection();
}

// TODO: Make it possible to overload/override device specific hooks
webrtc::AudioDeviceModule* CreateAudioDeviceModule(int32_t) {
    return nullptr;
}

cricket::WebRtcVideoEncoderFactory* CreateWebRtcVideoEncoderFactory() {
    return nullptr;
}
cricket::WebRtcVideoDecoderFactory* CreateWebRtcVideoDecoderFactory() {
    return nullptr;
}
cricket::VideoDeviceCapturerFactory* CreateVideoDeviceCapturerFactory() {
    return nullptr;
}

void PeerConnectionBackendWebRtcOrg::ensurePeerConnectionFactory()
{
	if (m_peerConnectionFactory != nullptr) {
    	return;
    }

    m_workerThread.reset(new rtc::Thread);
    m_workerThread->SetName("rtc-worker", this);
    m_workerThread->Start();

    m_signalingThread.reset(new rtc::Thread);

    m_peerConnectionFactory = webrtc::CreatePeerConnectionFactory(
            m_workerThread.get(),
            m_signalingThread.get(),
            CreateAudioDeviceModule(0),
            CreateWebRtcVideoEncoderFactory(),
            CreateWebRtcVideoDecoderFactory());

	ASSERT(m_peerConnectionFactory);
}

void PeerConnectionBackendWebRtcOrg::createPeerConnection()
{
	ensurePeerConnectionFactory();
    if (!m_peerConnection) {
    	webrtc::PeerConnectionInterface::RTCConfiguration configuration;
        configuration.servers = m_servers;
        m_peerConnection = m_peerConnectionFactory->CreatePeerConnection(
        			     configuration, &m_constraints, nullptr, nullptr, this);
	} else {
     	m_peerConnection->UpdateIce(m_servers, &m_constraints);
	}
}
void PeerConnectionBackendWebRtcOrg::createOffer(RTCOfferOptions& options, PeerConnection::SessionDescriptionPromise&& promise)
{
    ASSERT(InvalidRequestId == m_sessionDescriptionRequestId);
    ASSERT(!m_sessionDescriptionPromise);

    int id = m_peerConnection->createOffer(options);
    if (InvalidRequestId != id) {
        m_sessionDescriptionRequestId = id;
        m_sessionDescriptionPromise = WTFMove(promise);
    } else {
        promise.reject(DOMError::create("Failed to create offer"));
    }
}

void PeerConnectionBackendWebRtcOrg::createAnswer(RTCAnswerOptions& options, PeerConnection::SessionDescriptionPromise&& promise)
{
    ASSERT(InvalidRequestId == m_sessionDescriptionRequestId);
    ASSERT(!m_sessionDescriptionPromise);

    int id = m_peerConnection->createAnswer(options);
    if (InvalidRequestId != id) {
        m_sessionDescriptionRequestId = id;
        m_sessionDescriptionPromise = WTFMove(promise);
    } else {
        promise.reject(DOMError::create("Failed to create answer"));
    }
}

void PeerConnectionBackendWebRtcOrg::setLocalDescription(RTCSessionDescription& desc, PeerConnection::VoidPromise&& promise)
{
    ASSERT(InvalidRequestId == m_voidRequestId);
    ASSERT(!m_voidPromise);

    int id = m_peerConnection->setLocalDescription(desc);
    if (InvalidRequestId != id) {
        m_voidRequestId = id;
        m_voidPromise = WTFMove(promise);
    } else {
        promise.reject(DOMError::create("Failed to parse local description"));
    }
}
RefPtr<RTCSessionDescription> PeerConnectionBackendWebRtcOrg::localDescription() const
{
    // TODO: pendingLocalDescription/currentLocalDescription
    RefPtr<RTCSessionDescription> localDesc;
    m_peerConnection->localDescription(localDesc);
    return RTCSessionDescription::create(localDesc->type(), localDesc->sdp());
}
RefPtr<RTCSessionDescription> PeerConnectionBackendWebRtcOrg::currentLocalDescription() const
{
    return localDescription();
}
RefPtr<RTCSessionDescription> PeerConnectionBackendWebRtcOrg::pendingLocalDescription() const
{
    return RefPtr<RTCSessionDescription>();
}

void PeerConnectionBackendWebRtcOrg::setRemoteDescription(RTCSessionDescription& desc, PeerConnection::VoidPromise&& promise)
{
    ASSERT(InvalidRequestId == m_voidRequestId);
    ASSERT(!m_voidPromise);

    int id = m_peerConnection->setRemoteDescription(desc);
    if (InvalidRequestId != id) {
        m_voidRequestId = id;
        m_voidPromise = WTFMove(promise);
    } else {
        promise.reject(DOMError::create("Failed to parse remote description"));
    }
}
RefPtr<RTCSessionDescription> PeerConnectionBackendWebRtcOrg::remoteDescription() const
{
    // TODO: pendingRemoteDescription/currentRemoteDescription
    RefPtr<RTCSessionDescription> remoteDesc;
    m_peerConnection->remoteDescription(remoteDesc);
    return RTCSessionDescription::create(remoteDesc->type(), remoteDesc->sdp());
}
RefPtr<RTCSessionDescription> PeerConnectionBackendWebRtcOrg::currentRemoteDescription() const
{
    return remoteDescription();
}
RefPtr<RTCSessionDescription> PeerConnectionBackendWebRtcOrg::pendingRemoteDescription() const
{
    return RefPtr<RTCSessionDescription>();
}

void PeerConnectionBackendWebRtcOrg::setConfiguration(RTCConfiguration& rtcConfig, const MediaConstraints& constraints)
{
	m_constraints = constraints;

	for (const auto& iceServer : rtcConfig.iceServers) {
    	for (const auto& url : iceServer.urls) {
    	    webrtc::PeerConnectionInterface::IceServer server;
            server.uri = url;
            server.username = iceServer.username;
            server.password = iceServer.credential;
            m_servers.push_back(server);
        }
	}

	if (!m_peerConnection) {
    	webrtc::PeerConnectionInterface::RTCConfiguration configuration;
        configuration.servers = servers;
        m_peerConnection = m_peerConnectionFactory->CreatePeerConnection(
        	 configuration, constraints, nullptr, nullptr, this);
    } else {
        m_peerConnection->UpdateIce(servers, constraints);
    }
}

void PeerConnectionBackendWebRtcOrg::addIceCandidate(RTCIceCandidate& candidate, PeerConnection::VoidPromise&& promise)
{
    bool rc = m_peerConnection->addIceCandidate(candidate);
    if (rc) {
        promise.resolve(nullptr);
    } else {
        promise.reject(DOMError::create("Failed to add ICECandidate"));
    }
}

void PeerConnectionBackendWebRtcOrg::getStats(MediaStreamTrack*, PeerConnection::StatsPromise&& promise)
{
    int id = m_peerConnection->getStats();
    if (InvalidRequestId != id) {
        m_statsPromises.add(id, WTFMove(promise));
    } else {
        promise.reject(DOMError::create("Failed to get stats"));
    }
}

void PeerConnectionBackendWebRtcOrg::replaceTrack(RTCRtpSender&, MediaStreamTrack&, PeerConnection::VoidPromise&& promise)
{
    notImplemented();
    promise.reject(DOMError::create("NotSupportedError"));
}

void PeerConnectionBackendWebRtcOrg::stop()
{
    m_rtcConnection->close();
}

bool PeerConnectionBackendWebRtcOrg::isNegotiationNeeded() const
{
    return m_isNegotiationNeeded;
}

void PeerConnectionBackendWebRtcOrg::markAsNeedingNegotiation()
{
    Vector<RefPtr<RTCRtpSender>> senders = m_client->getSenders();
    for(auto &sender : senders) {
        RealtimeMediaSource& source = sender->track().source();
        webrtc::MediaStreamInterface* stream = static_cast<RealtimeMediaSourceWebRtcOrg&>(source).rtcStream();
        if (stream) {
            WebCore::ExceptionCode x;
            m_rtcConnection->addStream(Ref<MediaStream>(*stream), x);
            break;
        }
    }
}

void PeerConnectionBackendWebRtcOrg::clearNegotiationNeededState()
{
    m_isNegotiationNeeded = false;
}

std::unique_ptr<RTCDataChannelHandler> PeerConnectionBackendWebRtcOrg::createDataChannel(const String& label, const Dictionary& options)
{
    webrtc::DataChannelInit initData;
    String maxRetransmitsStr;
    String maxRetransmitTimeStr;
    String protocolStr;
    options.get("ordered", initData.ordered);
    options.get("negotiated", initData.negotiated);
    options.get("id", initData.id);
    options.get("maxRetransmits", maxRetransmitsStr);
    options.get("maxRetransmitTime", maxRetransmitTimeStr);
    options.get("protocol", protocolStr);
    initData.protocol = protocolStr.utf8().data();
    bool maxRetransmitsConversion;
    bool maxRetransmitTimeConversion;
    initData.maxRetransmits = maxRetransmitsStr.toUIntStrict(&maxRetransmitsConversion);
    initData.maxRetransmitTime = maxRetransmitTimeStr.toUIntStrict(&maxRetransmitTimeConversion);
    if (maxRetransmitsConversion && maxRetransmitTimeConversion) {
        return nullptr;
    }
    WebCore::ExceptionCode x;
    WebCore::RTCDataChannel* channel = m_rtcConnection->createDataChannel(label, options, x).release().get();
    return channel
        ? std::make_unique<RTCDataChannelHandlerWebRtcOrg>(channel)
        : nullptr;
}

// ===========  WRTCInt::RTCPeerConnectionClient ==========

#if 1
void PeerConnectionBackendWebRtcOrg::requestSucceeded(int id, const WebCore::RTCSessionDescription& desc)
{
    ASSERT(id == m_sessionDescriptionRequestId);
    ASSERT(m_sessionDescriptionPromise);

    // printf("%p:%s: %d, type=%s sdp=\n%s\n", this, __func__, id, desc.type.c_str(), desc.sdp.c_str());

    String type = desc.type();
    String sdp = desc.sdp();

    RefPtr<RTCSessionDescription> sessionDesc(RTCSessionDescription::create(type, sdp));
    m_sessionDescriptionPromise->resolve(sessionDesc);

    m_sessionDescriptionRequestId = WebCore::InvalidRequestId;
    m_sessionDescriptionPromise = WTF::Nullopt;
}

void PeerConnectionBackendWebRtcOrg::requestSucceeded(int id, const std::vector<std::unique_ptr<WebCore::RTCStatsReport>>& reports)
{
    Optional<PeerConnection::StatsPromise> statsPromise = m_statsPromises.take(id);
    if (!statsPromise) {
        printf("***Error: couldn't find promise for stats request: %d\n", id);
        return;
    }

    Ref<RTCStatsResponse> response = RTCStatsResponse::create();
    for(auto& r : reports)
    {
        String id = r->id().c_str();
        String type = r->type().c_str();
        double timestamp = r->timestamp();
        size_t idx = response->addReport(id, type, timestamp);
        for(auto& v : r->values())
        {
            response->addStatistic(idx, v.first.c_str(), v.second.c_str());
        }
    }

    statsPromise->resolve(WTFMove(response));
}

void PeerConnectionBackendWebRtcOrg::requestSucceeded(int id)
{
    ASSERT(id == m_voidRequestId);
    ASSERT(m_voidPromise);

    m_voidPromise->resolve(nullptr);

    m_voidRequestId = WebCore::InvalidRequestId;
    m_voidPromise = WTF::Nullopt;
}

void PeerConnectionBackendWebRtcOrg::requestFailed(int id, const std::string& error)
{
    if (id == m_voidRequestId) {
        ASSERT(m_voidPromise);
        ASSERT(!m_sessionDescriptionPromise);
        m_voidPromise->reject(DOMError::create(error.c_str()));
        m_voidPromise = WTF::Nullopt;
        m_voidRequestId = WebCore::InvalidRequestId;
    } else if (id == m_sessionDescriptionRequestId) {
        ASSERT(m_sessionDescriptionPromise);
        ASSERT(!m_voidPromise);
        m_sessionDescriptionPromise->reject(DOMError::create(error.c_str()));
        m_sessionDescriptionPromise = WTF::Nullopt;
        m_sessionDescriptionRequestId = WebCore::InvalidRequestId;
    } else {
        ASSERT_NOT_REACHED();
    }
}

void PeerConnectionBackendWebRtcOrg::negotiationNeeded()
{
    m_isNegotiationNeeded = true;
    m_client->scheduleNegotiationNeededEvent();
}

void PeerConnectionBackendWebRtcOrg::didAddRemoteStream(
    WebCore::RTCMediaStream *stream,
    const std::vector<std::string> &audioDevices,
    const std::vector<std::string> &videoDevices)
{
    ASSERT(m_client);

    std::shared_ptr<WebCore::RTCMediaStream> rtcStream;
    rtcStream.reset(stream);

    Vector<RefPtr<RealtimeMediaSource>> audioSources;
    Vector<RefPtr<RealtimeMediaSource>> videoSources;

    for (auto& device : audioDevices) {
        String name(device.c_str());
        String id(createCanonicalUUIDString());
        RefPtr<RealtimeMediaSourceWebRtcOrg> audioSource = adoptRef(new RealtimeAudioSourceWebRtcOrg(id, name));
        audioSource->setRTCStream(rtcStream);
        audioSources.append(audioSource.release());
    }
    for (auto& device : videoDevices) {
        String name(device.c_str());
        String id(createCanonicalUUIDString());
        RefPtr<RealtimeMediaSourceWebRtcOrg> videoSource = adoptRef(new RealtimeVideoSourceWebRtcOrg(id, name));
        videoSource->setRTCStream(rtcStream);
        videoSources.append(videoSource.release());
    }
    String id = rtcStream->id().c_str();
    RefPtr<MediaStreamPrivate> privateStream = MediaStreamPrivate::create(id, audioSources, videoSources);
    RefPtr<MediaStream> mediaStream = MediaStream::create(*m_client->scriptExecutionContext(), privateStream.copyRef());
    privateStream->startProducingData();
    m_client->addRemoteStream(WTFMove(mediaStream));
}

void PeerConnectionBackendWebRtcOrg::didGenerateIceCandidate(const WebCore::RTCIceCandidate& iceCandidate)
{
    ASSERT(m_client);
    String sdp = iceCandidate.sdp.c_str();
    String sdpMid = iceCandidate.sdpMid.c_str();
    Optional<unsigned short> sdpMLineIndex = iceCandidate.sdpMLineIndex;
    RefPtr<RTCIceCandidate> candidate = RTCIceCandidate::create(sdp, sdpMid, sdpMLineIndex);
    m_client->scriptExecutionContext()->postTask([this, candidate] (ScriptExecutionContext&) {
        m_client->fireEvent(RTCIceCandidateEvent::create(false, false, candidate.copyRef()));
    });
}

void PeerConnectionBackendWebRtcOrg::didChangeSignalingState(webrtc::PeerConnectionInterface::SignalingState state)
{
    ASSERT(m_client);
    PeerConnectionStates::SignalingState signalingState = PeerConnectionStates::SignalingState::Stable;
    switch(state)
    {
        case webrtc::PeerConnectionInterface::kStable:
            signalingState = PeerConnectionStates::SignalingState::Stable;
            break;
        case webrtc::PeerConnectionInterface::kHaveLocalOffer:
            signalingState = PeerConnectionStates::SignalingState::HaveLocalOffer;
            break;
        case webrtc::PeerConnectionInterface::kHaveRemoteOffer:
            signalingState = PeerConnectionStates::SignalingState::HaveRemoteOffer;
            break;
        case webrtc::PeerConnectionInterface::kHaveLocalPrAnswer:
            signalingState = PeerConnectionStates::SignalingState::HaveLocalPrAnswer;
            break;
        case webrtc::PeerConnectionInterface::kHaveRemotePrAnswer:
            signalingState = PeerConnectionStates::SignalingState::HaveRemotePrAnswer;
            break;
        case webrtc::PeerConnectionInterface::kClosed:
            signalingState = PeerConnectionStates::SignalingState::Closed;
            break;
        default:
            return;
    }
    m_client->setSignalingState(signalingState);
}

void PeerConnectionBackendWebRtcOrg::didChangeIceGatheringState(webrtc::PeerConnectionInterface::IceGatheringState state)
{
    ASSERT(m_client);
    PeerConnectionStates::IceGatheringState iceGatheringState = PeerConnectionStates::IceGatheringState::New;
    switch(state)
    {
        case webrtc::PeerConnectionInterface::kIceGatheringNew:
            iceGatheringState = PeerConnectionStates::IceGatheringState::New;
            break;
        case webrtc::PeerConnectionInterface::kIceGatheringGathering:
            iceGatheringState = PeerConnectionStates::IceGatheringState::Gathering;
            break;
        case webrtc::PeerConnectionInterface::kIceGatheringComplete:
            iceGatheringState = PeerConnectionStates::IceGatheringState::Complete;
            break;
        default:
            return;
    }
    m_client->updateIceGatheringState(iceGatheringState);
}

void PeerConnectionBackendWebRtcOrg::didChangeIceConnectionState(webrtc::PeerConnectionInterface::IceConnectionState state)
{
    ASSERT(m_client);
    PeerConnectionStates::IceConnectionState iceConnectionState = PeerConnectionStates::IceConnectionState::New;
    switch(state)
    {
        case webrtc::PeerConnectionInterface::kIceConnectionNew:
            iceConnectionState = PeerConnectionStates::IceConnectionState::New;
            break;
        case webrtc::PeerConnectionInterface::kIceConnectionChecking:
            iceConnectionState = PeerConnectionStates::IceConnectionState::Checking;
            break;
        case webrtc::PeerConnectionInterface::kIceConnectionConnected:
            iceConnectionState = PeerConnectionStates::IceConnectionState::Connected;
            break;
        case webrtc::PeerConnectionInterface::kIceConnectionCompleted:
            iceConnectionState = PeerConnectionStates::IceConnectionState::Completed;
            break;
        case webrtc::PeerConnectionInterface::kIceConnectionFailed:
            iceConnectionState = PeerConnectionStates::IceConnectionState::Failed;
            break;
        case webrtc::PeerConnectionInterface::kIceConnectionDisconnected:
            iceConnectionState = PeerConnectionStates::IceConnectionState::Disconnected;
            break;
        case webrtc::PeerConnectionInterface::kIceConnectionClosed:
            iceConnectionState = PeerConnectionStates::IceConnectionState::Closed;
            break;
        default:
            return;
    }
    m_client->updateIceConnectionState(iceConnectionState);
}

void PeerConnectionBackendWebRtcOrg::didAddRemoteDataChannel(WebCore::RTCDataChannel* channel)
{
    std::unique_ptr<RTCDataChannelHandler> handler = std::make_unique<RTCDataChannelHandlerWebRtcOrg>(channel);
    m_client->addRemoteDataChannel(WTFMove(handler));
}
#endif

RTCDataChannelHandlerWebRtcOrg::RTCDataChannelHandlerWebRtcOrg(WebCore::RTCDataChannel* dataChannel)
    : m_rtcDataChannel(dataChannel)
    , m_client(nullptr)
{
}

void RTCDataChannelHandlerWebRtcOrg::setClient(RTCDataChannelHandlerClient* client)
{
    if (m_client == client)
        return;

    m_client = client;

    if (m_client)
        m_rtcDataChannel->setClient(this);
}

String RTCDataChannelHandlerWebRtcOrg::label()
{
    return m_rtcDataChannel->label();
}

bool RTCDataChannelHandlerWebRtcOrg::ordered()
{
    return m_rtcDataChannel->ordered();
}

unsigned short RTCDataChannelHandlerWebRtcOrg::maxRetransmitTime()
{
    return m_rtcDataChannel->maxRetransmitTime();
}

unsigned short RTCDataChannelHandlerWebRtcOrg::maxRetransmits()
{
    return m_rtcDataChannel->maxRetransmits();
}

String RTCDataChannelHandlerWebRtcOrg::protocol()
{
    return m_rtcDataChannel->protocol();
}

bool RTCDataChannelHandlerWebRtcOrg::negotiated()
{
    return m_rtcDataChannel->negotiated();
}

unsigned short RTCDataChannelHandlerWebRtcOrg::id()
{
    return m_rtcDataChannel->id();
}

unsigned long RTCDataChannelHandlerWebRtcOrg::bufferedAmount()
{
    return m_rtcDataChannel->bufferedAmount();
}

bool RTCDataChannelHandlerWebRtcOrg::sendStringData(const String& str)
{
    return m_rtcDataChannel->sendStringData(str.utf8().data());
}

bool RTCDataChannelHandlerWebRtcOrg::sendRawData(const char* data, size_t size)
{
    return m_rtcDataChannel->sendRawData(data, size);
}

void RTCDataChannelHandlerWebRtcOrg::close()
{
    m_rtcDataChannel->close();
}

void RTCDataChannelHandlerWebRtcOrg::didChangeReadyState(DataChannelInterface::DataState state)
{
    RTCDataChannelHandlerClient::ReadyState readyState = RTCDataChannelHandlerClient::ReadyStateConnecting;
    switch(state) {
        case DataChannelInterface::kConnecting:
            readyState = RTCDataChannelHandlerClient::ReadyStateConnecting;
            break;
        case DataChannelInterface::kOpen:
            readyState = RTCDataChannelHandlerClient::ReadyStateOpen;
            break;
        case DataChannelInterface::kClosing:
            readyState = RTCDataChannelHandlerClient::ReadyStateClosing;
            break;
        case DataChannelInterface::kClosed:
            readyState = RTCDataChannelHandlerClient::ReadyStateClosed;
            break;
        default:
            break;
    };
    m_client->didChangeReadyState(readyState);
}

void RTCDataChannelHandlerWebRtcOrg::didReceiveStringData(const std::string& str)
{
    m_client->didReceiveStringData(str.c_str());
}

void RTCDataChannelHandlerWebRtcOrg::didReceiveRawData(const char* data, size_t sz)
{
    m_client->didReceiveRawData(data, sz);
}

}
