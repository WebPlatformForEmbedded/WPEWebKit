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

#include "MediaStream.h"
#include "MediaStreamCreationClient.h"
#include "MediaStreamPrivate.h"
#include "MediaStreamTrack.h"
#include "MediaStreamTrackSourcesRequestClient.h"

#include <condition_variable>

#include "webrtc/base/nullsocketserver.h"

#include "webrtc/api/mediaconstraintsinterface.h"
#include "webrtc/api/peerconnectioninterface.h"

#include "webrtc/media/engine/webrtcvideocapturerfactory.h"

#define NOTREACHED() ASSERT(false)

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

namespace
{
int generateNextId()
{
    static int gRequestId = 0;
    if (gRequestId == std::numeric_limits<int>::max()) {
        gRequestId = 0;
    }
    return ++gRequestId;
}

struct DummySocketServer : public rtc::NullSocketServer
{
    bool Wait(int, bool) override { return true; }
    void WakeUp() override { }
};

class SignalingThreadWrapper : public rtc::Thread
{
    using MessagesQueue = std::unordered_map<uint32_t, rtc::Message>;

    std::mutex m_mutex;
    std::condition_variable m_sendCondition;
    MessagesQueue m_pendingMessages;
    uint32_t m_lastTaskId {0};

    uint32_t addPendingMessage(rtc::MessageHandler* handler,
                               uint32_t message_id, rtc::MessageData* data)
    {
        rtc::Message message;
        message.phandler = handler;
        message.message_id = message_id;
        message.pdata = data;
        std::lock_guard<std::mutex> lock(m_mutex);
        uint32_t id = ++m_lastTaskId;
        m_pendingMessages[id] = message;
        return id;
    }

    void invokeMessageHandler(rtc::Message message)
    {
        ASSERT(IsCurrent());
        if (message.message_id == rtc::MQID_DISPOSE) {
            ASSERT(message.pdata != nullptr);
            delete message.pdata;
        } else {
            ASSERT(message.phandler != nullptr);
            message.phandler->OnMessage(&message);
        }
    }

    void handlePendingMessage(uint32_t id)
    {
        rtc::Message message;
        bool haveMessage = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_pendingMessages.find(id);
            if (it != m_pendingMessages.end()) {
                message = it->second;
                haveMessage = true;
                m_pendingMessages.erase(it);
            }
        }
        if (haveMessage) {
            invokeMessageHandler(message);
        }
    }

    void handleSend(uint32_t id)
    {
        handlePendingMessage(id);
        m_sendCondition.notify_one();
    }

    void postInternal(int delay_ms, rtc::MessageHandler* handler,
                      uint32_t message_id, rtc::MessageData* data)
    {
        uint32_t id = addPendingMessage(handler, message_id, data);
        // TODO: Replace with integration to WTF's main RunLoop
        using TaskInfo = std::pair<SignalingThreadWrapper*, uint32_t>;
        auto info = new TaskInfo(this, id);
        g_timeout_add(delay_ms, [](gpointer data) -> gboolean {
            std::unique_ptr<TaskInfo> info(reinterpret_cast<TaskInfo*>(data));
            info->first->handlePendingMessage(info->second);
            return G_SOURCE_REMOVE;
        }, info);
    }

    void sendInternal(rtc::MessageHandler* handler, uint32_t message_id,
                      rtc::MessageData* data)
    {
        if (IsCurrent()) {
            rtc::Message message;
            message.phandler = handler;
            message.message_id = message_id;
            message.pdata = data;
            invokeMessageHandler(message);
            return;
        }
        uint32_t id = addPendingMessage(handler, message_id, data);
        // TODO: Replace with integration to WTF's main RunLoop
        using TaskInfo = std::pair<SignalingThreadWrapper*, uint32_t>;
        auto info = new TaskInfo(this, id);
        g_timeout_add(0, [](gpointer data) -> gboolean {
            std::unique_ptr<TaskInfo> info(reinterpret_cast<TaskInfo*>(data));
            info->first->handleSend(info->second);
            return G_SOURCE_REMOVE;
        }, info);
        std::unique_lock<std::mutex> lock(m_mutex);
        m_sendCondition.wait(lock);
    }

public:
    SignalingThreadWrapper()
        : rtc::Thread(new DummySocketServer())
    {
        if (!rtc::MessageQueueManager::IsInitialized())  {
            rtc::MessageQueueManager::Add(this);
        }
        rtc::ThreadManager::Instance()->SetCurrentThread(this);
    }

    void Post(rtc::MessageHandler* handler, uint32_t message_id,
              rtc::MessageData* data, bool /*time_sensitive*/) override
    {
        postInternal(0, handler, message_id, data);
    }

    void PostDelayed(int delay_ms, rtc::MessageHandler* handler,
                     uint32_t message_id, rtc::MessageData* data) override
    {
        postInternal(delay_ms, handler, message_id, data);
    }

    void Send(rtc::MessageHandler *handler, uint32_t id, rtc::MessageData *data) override
    {
        sendInternal(handler, id, data);
    }

    void Clear(rtc::MessageHandler* handler, uint32_t id, rtc::MessageList* removed) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_pendingMessages.begin(); it != m_pendingMessages.end();) {
            const rtc::Message& message = it->second;
            if (message.Match(handler, id)) {
                if (removed != nullptr) {
                    removed->push_back(message);
                } else {
                    delete message.pdata;
                }
                it = m_pendingMessages.erase(it);
            } else {
                ++it;
            }
        }
    }

    void Quit() override
    {
        NOTREACHED();
    }

    bool IsQuitting() override
    {
        NOTREACHED();
        return false;
    }

    void Restart() override
    {
        NOTREACHED();
    }

    bool Get(rtc::Message*, int, bool) override
    {
        NOTREACHED();
        return false;
    }

    bool Peek(rtc::Message*, int) override
    {
        NOTREACHED();
        return false;
    }

    void PostAt(uint32_t, rtc::MessageHandler*, uint32_t, rtc::MessageData*) override
    {
        NOTREACHED();
    }

    void Dispatch(rtc::Message*) override
    {
        NOTREACHED();
    }

    void ReceiveSends() override
    {
        // NOTE: it is called by the worker thread, but it shouldn't do anything
    }

    int GetDelay() override
    {
        NOTREACHED();
        return 0;
    }

    void Stop() override
    {
        NOTREACHED();
    }

    void Run() override
    {
        NOTREACHED();
    }
};

class MockMediaConstraints : public webrtc::MediaConstraintsInterface
{
    webrtc::MediaConstraintsInterface::Constraints m_mandatory;
    webrtc::MediaConstraintsInterface::Constraints m_optional;
public:
    explicit MockMediaConstraints(const WRTCInt::RTCOfferAnswerOptions &options)
    {
        for(const auto& c: options) {
            std::string key = c.first;
            std::string value = c.second ? std::string("true") : std::string("false");
            m_mandatory.push_back(webrtc::MediaConstraintsInterface::Constraint(key, value));
        }
    }

    explicit MockMediaConstraints(const WRTCInt::RTCMediaConstraints& constraints)
    {
        for(const auto& c: constraints) {
            m_optional.push_back(webrtc::MediaConstraintsInterface::Constraint(c.first, c.second));
        }
    }

    const MediaConstraintsInterface::Constraints& GetMandatory() const override
    {
        return m_mandatory;
    }

    const MediaConstraintsInterface::Constraints& GetOptional() const override
    {
        return m_optional;
    }
};

class MockMediaStream : public WRTCInt::RTCMediaStream
{
    rtc::scoped_refptr<webrtc::MediaStreamInterface> m_stream;
    std::string m_label;
public:
    MockMediaStream(webrtc::MediaStreamInterface* stream, std::string label)
        : m_stream(stream)
        , m_label(std::move(label))
    {  }

    ~MockMediaStream() override
    {
        if (m_stream) {
            webrtc::AudioTrackVector audioTracks = m_stream->GetAudioTracks();
            for (auto &track : audioTracks) {
                track->set_enabled(false);
                m_stream->RemoveTrack(track);
            }
            webrtc::VideoTrackVector videoTracks = m_stream->GetVideoTracks();
            for (auto &track : videoTracks) {
                track->set_enabled(false);
                if (track->GetSource()) {
                    track->GetSource()->Stop();
                }
                m_stream->RemoveTrack(track);
            }
        }
    }

    webrtc::MediaStreamInterface* stream() const
    {
        return m_stream.get();
    }

    std::string id() const override
    {
        return m_label;
    }
};

class MockRTCDataChannel /*: public WRTCInt::RTCDataChannel*/
{
    class MockDataChannelObserver: public webrtc::DataChannelObserver, public rtc::RefCountInterface
    {
        MockRTCDataChannel* m_channel;
    public:
        explicit MockDataChannelObserver(MockRTCDataChannel* channel)
            : m_channel(channel)
        { }
        void OnStateChange() override
        {
            m_channel->onStateChange();
        }
        void OnMessage(const webrtc::DataBuffer& buffer) override
        {
            m_channel->onMessage(buffer);
        }
    };

    /*WRTCInt::RTCDataChannelClient*/ WebCore::RTCDataChannelHandlerWebRtcOrg* m_client;
    rtc::scoped_refptr<webrtc::DataChannelInterface> m_dataChannel;
    rtc::scoped_refptr<MockDataChannelObserver> m_observer;
public:
    MockRTCDataChannel()
        : m_client(nullptr)
        , m_dataChannel()
        , m_observer(new rtc::RefCountedObject<MockDataChannelObserver>(this))
    {
    }

    ~MockRTCDataChannel()
    {
        closeDataChannel();
    }

    // WRTCInt::RTCDataChannel
    std::string label() const
    {
        return m_dataChannel->label();
    }
    bool ordered() const
    {
        return m_dataChannel->ordered();
    }
    unsigned short maxRetransmitTime() const
    {
        return m_dataChannel->maxRetransmitTime();
    }
    unsigned short maxRetransmits() const
    {
        return m_dataChannel->maxRetransmits();
    }
    std::string protocol() const
    {
        return m_dataChannel->protocol();
    }
    bool negotiated() const
    {
        return m_dataChannel->negotiated();
    }
    unsigned short id()
    {
        return m_dataChannel->id();
    }
    unsigned long bufferedAmount()
    {
        return m_dataChannel->buffered_amount();
    }
    bool sendStringData(const std::string& str)
    {
        rtc::CopyOnWriteBuffer buffer(str.c_str(), str.size());
        return m_dataChannel->Send(webrtc::DataBuffer(buffer, false));
    }
    bool sendRawData(const char* data, size_t sz)
    {
        rtc::CopyOnWriteBuffer buffer(data, sz);
        return m_dataChannel->Send(webrtc::DataBuffer(buffer, true));
    }
    void close()
    {
        closeDataChannel();
    }
    void setClient(/*WRTCInt::RTCDataChannelClient*/ WebCore::RTCDataChannelHandlerWebRtcOrg* client)
    {
        m_client = client;
        if (m_client != nullptr) {
            m_dataChannel->RegisterObserver(m_observer.get());
        } else {
            m_dataChannel->UnregisterObserver();
        }
    }

    // Helpers
    void setDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> dataChannel)
    {
        m_dataChannel = dataChannel;
    }
    void onStateChange()
    {
        ASSERT(m_client);
        webrtc::DataChannelInterface::DataState state = m_dataChannel->state();
        //LOG(LS_INFO) << "DataChannel id=" << m_dataChannel->id() << " state=" << webrtc::DataChannelInterface::DataStateString(state);
        m_client->didChangeReadyState(static_cast<WRTCInt::DataChannelState>(state));
    }
    void onMessage(const webrtc::DataBuffer& buffer)
    {
        ASSERT(m_client);
        const char* data = buffer.data.data<char>();
        size_t sz = buffer.data.size();
        if (buffer.binary) {
            m_client->didReceiveRawData(data, sz);
        } else {
            m_client->didReceiveStringData(std::string(data, sz));
        }
    }
    void closeDataChannel()
    {
        if (m_dataChannel) {
            m_dataChannel->UnregisterObserver();
            m_dataChannel->Close();
            m_dataChannel = nullptr;
        }
    }
};

class MockRTCPeerConnection;

class MockCreateSessionDescriptionObserver : public webrtc::CreateSessionDescriptionObserver
{
    /*WRTCInt::RTCPeerConnection*/ MockRTCPeerConnection *m_backend;
    int m_requestId;
public:
    MockCreateSessionDescriptionObserver(/*WRTCInt::RTCPeerConnection*/ MockRTCPeerConnection *backend, int id)
        : m_backend(backend)
        , m_requestId(id)
    { }

    void OnSuccess(webrtc::SessionDescriptionInterface* desc) override;
    void OnFailure(const std::string& error) override;
};

class MockSetSessionDescriptionObserver : public webrtc::SetSessionDescriptionObserver
{
    /*WRTCInt::RTCPeerConnection*/ MockRTCPeerConnection *m_backend;
    int m_requestId;
public:
    MockSetSessionDescriptionObserver(/*WRTCInt::RTCPeerConnection*/ MockRTCPeerConnection *backend, int id)
        : m_backend(backend)
        , m_requestId(id)
    { }

    void OnSuccess() override;
    void OnFailure(const std::string& error) override;
};

class MockStatsObserver : public webrtc::StatsObserver
{
    /*WRTCInt::RTCPeerConnection*/ MockRTCPeerConnection *m_backend;
    unsigned int m_requestId;
public:
    MockStatsObserver(/*WRTCInt::RTCPeerConnection*/ MockRTCPeerConnection *backend, unsigned int id)
        : m_backend(backend)
        , m_requestId(id)
    { }

    void OnComplete(const webrtc::StatsReports& reports) override;
};

class MockRTCPeerConnection : /*public WRTCInt::RTCPeerConnection,*/ public webrtc::PeerConnectionObserver
{
    /*WRTCInt::RTCPeerConnectionClient*/ WebCore::PeerConnectionBackendWebRtcOrg* m_client;
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> m_peerConnection;
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> m_peerConnectionFactory;
    std::set<std::uintptr_t> m_addedStreams;

    void OnAddStream(webrtc::MediaStreamInterface* stream) override
    {
        std::vector<std::string> audioSources;
        for (const auto& track: stream->GetAudioTracks()) {
            audioSources.push_back(track->id());
        }
        std::vector<std::string> videoSources;
        for (const auto& track: stream->GetVideoTracks()) {
            videoSources.push_back(track->id());
        }
        m_client->didAddRemoteStream(new MockMediaStream(stream, stream->label()), audioSources, videoSources);
    }

    void OnRemoveStream(webrtc::MediaStreamInterface* /*stream*/) override
    {
        //LOG(LS_WARNING) << "Not Implemented";
    }

    void OnRenegotiationNeeded() override
    {
        m_client->negotiationNeeded();
    }

    void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState state) override
    {
        m_client->didChangeIceConnectionState(static_cast<WRTCInt::IceConnectionState>(state));
    }

    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState state) override
    {
        m_client->didChangeIceGatheringState(static_cast<WRTCInt::IceGatheringState>(state));
    }

    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState state) override
    {
        m_client->didChangeSignalingState(static_cast<WRTCInt::SignalingState>(state));
    }

    void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override
    {
        std::string sdpStr;
        if (candidate->ToString(&sdpStr)) {
            WRTCInt::RTCIceCandidate rtcCandidate;
            rtcCandidate.sdp = sdpStr;
            rtcCandidate.sdpMid = candidate->sdp_mid();
            rtcCandidate.sdpMLineIndex = candidate->sdp_mline_index();
            m_client->didGenerateIceCandidate(rtcCandidate);
        }
    }

    void OnDataChannel(webrtc::DataChannelInterface* remoteDataChannel) override
    {
        auto rtcChannel = new MockRTCDataChannel;
        rtcChannel->setDataChannel(remoteDataChannel);
        m_client->didAddRemoteDataChannel(rtcChannel);
    }

public:
    MockRTCPeerConnection(/*WRTCInt::RTCPeerConnectionClient*/ WebCore::PeerConnectionBackendWebRtcOrg* client, webrtc::PeerConnectionFactoryInterface* factory)
        : m_client(client)
        , m_peerConnectionFactory(factory)
    { }

    bool setConfiguration(const WRTCInt::RTCConfiguration& rtcConfig, const WRTCInt::RTCMediaConstraints& rtcConstraints)
    {
        webrtc::PeerConnectionInterface::IceServers servers;
        for (const auto& iceServer : rtcConfig.iceServers) {
            for (const auto& url : iceServer.urls) {
                webrtc::PeerConnectionInterface::IceServer server;
                server.uri = url;
                server.username = iceServer.username;
                server.password = iceServer.credential;
                servers.push_back(server);
            }
        }
        std::unique_ptr<MockMediaConstraints> constraints(new MockMediaConstraints(rtcConstraints));
        if (!m_peerConnection) {
            webrtc::PeerConnectionInterface::RTCConfiguration configuration;
            configuration.servers = servers;
            m_peerConnection = m_peerConnectionFactory->CreatePeerConnection(
                configuration, constraints.get(), nullptr, nullptr, this);
        } else {
            m_peerConnection->UpdateIce(servers, constraints.get());
        }
        return !!m_peerConnection;
    }

    int createOffer(const WRTCInt::RTCOfferAnswerOptions &options)
    {
        int requestId = generateNextId();
        std::unique_ptr<MockMediaConstraints> constrains(new MockMediaConstraints(options));
        webrtc::CreateSessionDescriptionObserver* observer =
            new rtc::RefCountedObject<MockCreateSessionDescriptionObserver>(this, requestId);
        m_peerConnection->CreateOffer(observer, constrains.get());
        return requestId;
    }

    int createAnswer(const WRTCInt::RTCOfferAnswerOptions &options)
    {
        int requestId = generateNextId();
        std::unique_ptr<MockMediaConstraints> constrains(new MockMediaConstraints(options));
        webrtc::CreateSessionDescriptionObserver* observer =
            new rtc::RefCountedObject<MockCreateSessionDescriptionObserver>(this, requestId);
        m_peerConnection->CreateAnswer(observer, constrains.get());
        return requestId;
    }

    bool localDescription(WRTCInt::RTCSessionDescription& desc)
    {
        const webrtc::SessionDescriptionInterface* sessionDescription =
            m_peerConnection->local_description();
        if (sessionDescription && sessionDescription->ToString(&desc.sdp)) {
            desc.sdp = sessionDescription->type();
            return true;
        }
        return false;
    }

    int setLocalDescription(const WRTCInt::RTCSessionDescription& desc)
    {
        int requestId = generateNextId();
        webrtc::SdpParseError error;
        webrtc::SessionDescriptionInterface* sessionDescription =
            webrtc::CreateSessionDescription(desc.type, desc.sdp, &error);
        if (!sessionDescription) {
            //LOG(LS_ERROR) << "Failed to create session description, error=" << error.description << " line=" << error.line;
            return WRTCInt::InvalidRequestId;
        }
        webrtc::SetSessionDescriptionObserver* observer =
            new rtc::RefCountedObject<MockSetSessionDescriptionObserver>(this, requestId);
        m_peerConnection->SetLocalDescription(observer, sessionDescription);
        return requestId;
    }

    bool remoteDescription(WRTCInt::RTCSessionDescription& desc)
    {
        const webrtc::SessionDescriptionInterface* sessionDescription =
            m_peerConnection->remote_description();
        if (sessionDescription && sessionDescription->ToString(&desc.sdp)) {
            desc.sdp = sessionDescription->type();
            return true;
        }
        return false;
    }

    int setRemoteDescription(const WRTCInt::RTCSessionDescription& desc)
    {
        int requestId = generateNextId();
        webrtc::SdpParseError error;
        webrtc::SessionDescriptionInterface* sessionDescription =
            webrtc::CreateSessionDescription(desc.type, desc.sdp, &error);
        if (!sessionDescription) {
            //LOG(LS_ERROR) << "Failed to create session description, error=" << error.description << " line=" << error.line;
            return WRTCInt::InvalidRequestId;
        }
        webrtc::SetSessionDescriptionObserver* observer =
            new rtc::RefCountedObject<MockSetSessionDescriptionObserver>(this, requestId);
        m_peerConnection->SetRemoteDescription(observer, sessionDescription);
        return requestId;
    }

    bool addIceCandidate(const WRTCInt::RTCIceCandidate& rtcCandidate)
    {
        webrtc::SdpParseError error;
        std::unique_ptr<webrtc::IceCandidateInterface> candidate(
            webrtc::CreateIceCandidate(rtcCandidate.sdpMid,
                                       rtcCandidate.sdpMLineIndex,
                                       rtcCandidate.sdp,
                                       &error));
        if (!candidate) {
            //LOG(LS_ERROR) << "Failed to add ICE candidate, error=" << error.description << " line=" << error.line;
            return false;
        }
        return m_peerConnection->AddIceCandidate(candidate.get());
    }

    bool addStream(WRTCInt::RTCMediaStream* rtcStream)
    {
        if (rtcStream == nullptr) {
            return false;
        }
        uintptr_t streamPtr = reinterpret_cast<std::uintptr_t>(rtcStream);
        if(m_addedStreams.end() != m_addedStreams.find(streamPtr)) {
            return true;
        }
        bool ret = m_peerConnection->AddStream(static_cast<MockMediaStream*>(rtcStream)->stream());
        if (ret) {
            m_addedStreams.insert(streamPtr);
        }
        return ret;
    }

    bool removeStream(WRTCInt::RTCMediaStream* rtcStream)
    {
        if (rtcStream == nullptr) {
            return false;
        }
        m_addedStreams.erase(reinterpret_cast<std::uintptr_t>(rtcStream));
        m_peerConnection->RemoveStream(static_cast<MockMediaStream*>(rtcStream)->stream());
        return true;
    }

    int getStats()
    {
        int requestId = generateNextId();
        rtc::scoped_refptr<MockStatsObserver> observer(
            new rtc::RefCountedObject<MockStatsObserver>(this, requestId));
        webrtc::PeerConnectionInterface::StatsOutputLevel level =
            webrtc::PeerConnectionInterface::kStatsOutputLevelStandard;
        bool rc = m_peerConnection->GetStats(observer, nullptr, level);
        if (rc) {
            return requestId;
        }
        return WRTCInt::InvalidRequestId;
    }

    void stop()
    {
        m_peerConnection = nullptr;
    }

    /*WRTCInt::RTCPeerConnectionClient*/ WebCore::PeerConnectionBackendWebRtcOrg* client()
    {
        return m_client;
    }

    /*WRTCInt::RTCDataChannel*/ MockRTCDataChannel* createDataChannel(const std::string &label, const WRTCInt::DataChannelInit& initData)
    {
        webrtc::DataChannelInit config;
        config.id = initData.id;
        config.ordered = initData.ordered;
        config.negotiated = initData.negotiated;
        if (initData.maxRetransmits > 0) {
            config.maxRetransmits = initData.maxRetransmits;
        } else {
            config.maxRetransmitTime = initData.maxRetransmitTime;
        }
        config.protocol = initData.protocol;
        rtc::scoped_refptr<webrtc::DataChannelInterface> dataChannel =
            m_peerConnection->CreateDataChannel(label, &config);
        if (dataChannel) {
            auto channel = new MockRTCDataChannel;
            channel->setDataChannel(dataChannel);
            return channel;
        }
        return nullptr;
    }
};

class MockRTCStatsReport : public WRTCInt::RTCStatsReport
{
    const webrtc::StatsReport* m_report;
public:
    explicit MockRTCStatsReport(const webrtc::StatsReport* report)
        : m_report(report)
    {}

    double timestamp() const override
    {
        return m_report->timestamp();
    }

    std::string id() const override
    {
        return m_report->id()->ToString();
    }

    std::string type() const override
    {
        return m_report->TypeToString();
    }

    std::vector<WRTCInt::RTCStatsReport::Value> values() const override
    {
        std::vector<WRTCInt::RTCStatsReport::Value> ret;
        ret.reserve(m_report->values().size());
        for(const auto& p : m_report->values()) {
            const webrtc::StatsReport::ValuePtr& valPtr = p.second;
            std::string name = valPtr->display_name();
            std::string valueStr = valPtr->ToString();
            ret.emplace_back(name, valueStr);
        }
        return ret;
    }
};

inline void MockCreateSessionDescriptionObserver::OnSuccess(webrtc::SessionDescriptionInterface* desc)
{
    std::unique_ptr<webrtc::SessionDescriptionInterface> holder(desc);
    std::string descStr;
    if (!desc->ToString(&descStr)) {
        std::string error = "Failed to get session description string";
        OnFailure(error);
        return;
    }
    WRTCInt::RTCSessionDescription sessionDescription;
    sessionDescription.type = desc->type();
    sessionDescription.sdp = descStr;
    m_backend->client()->requestSucceeded(m_requestId, sessionDescription);
}

inline void MockCreateSessionDescriptionObserver::OnFailure(const std::string& error)
{
    //LOG(LS_ERROR) << error;
    m_backend->client()->requestFailed(m_requestId, error);
}

inline void MockSetSessionDescriptionObserver::OnSuccess()
{
    m_backend->client()->requestSucceeded(m_requestId);
}

inline void MockSetSessionDescriptionObserver::OnFailure(const std::string& error)
{
    //LOG(LS_ERROR) << error;
    m_backend->client()->requestFailed(m_requestId, error);
}

inline void MockStatsObserver::OnComplete(const webrtc::StatsReports& reports)
{
    std::vector<std::unique_ptr<WRTCInt::RTCStatsReport>> rtcReports;
    rtcReports.reserve(reports.size());
    for(auto& r : reports) {
        rtcReports.emplace_back(new MockRTCStatsReport(r));
    }
    m_backend->client()->requestSucceeded(m_requestId, rtcReports);
}

// TODO: Implement a hole puncher "renderer"
class MockVideoRenderer : public WRTCInt::RTCVideoRenderer
                        , public rtc::VideoSinkInterface<cricket::VideoFrame>

{
    WRTCInt::RTCVideoRendererClient* m_client;
    rtc::scoped_refptr<webrtc::VideoTrackInterface> m_renderedTrack;
    std::unique_ptr<uint8_t[]> m_imageBuffer;
    int m_width {0};
    int m_height {0};
public:
    MockVideoRenderer(MockMediaStream* stream, WRTCInt::RTCVideoRendererClient* client)
        : m_client(client)
    {
        ASSERT(stream != nullptr);
        ASSERT(stream->stream() != nullptr);
        if (!stream->stream()->GetVideoTracks().empty()) {
            m_renderedTrack = stream->stream()->GetVideoTracks().at(0);
        }
        if (m_renderedTrack) {
            m_renderedTrack->AddOrUpdateSink(this, rtc::VideoSinkWants());
        }
    }

    ~MockVideoRenderer() override
    {
        if (m_renderedTrack) {
            m_renderedTrack->RemoveSink(this);
            m_renderedTrack = nullptr;
        }
    }

    void OnFrame(const cricket::VideoFrame& frame) override
    {
        if (m_client == nullptr) {
            return;
        }
        if (frame.width() <= 0 || frame.height() <= 0) {
            m_width = m_height = 0;
            m_imageBuffer.reset();
            return;
        }
        if (frame.width() != m_width || frame.height() != m_height) {
            m_width = frame.width();
            m_height = frame.height();
            m_imageBuffer.reset(new uint8_t[m_width * m_width * 4]);
        }
        size_t size = m_width * m_width * 4;
        size_t converted_size = frame.ConvertToRgbBuffer(
            cricket::FOURCC_ARGB, m_imageBuffer.get(), size, m_width * 4);
        if (converted_size != 0 && size >= converted_size) {
            m_client->renderFrame(m_imageBuffer.get(), size, m_width, m_height);
        } else {
            //LOG(LS_ERROR) << "Failed to convert to RGB buffer";
        }
    }

    void setVideoRectangle(int, int, int, int) override
    { }
};

class MockRealtimeMediaSourceCenter : public WRTCInt::RTCMediaSourceCenter
{
    std::unique_ptr<rtc::Thread> m_workerThread;
    std::unique_ptr<rtc::Thread> m_signalingThread;
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> m_peerConnectionFactory;
public:
    WRTCInt::RTCMediaStream* createMediaStream(const std::string& audioDeviceID, const std::string& videoDeviceID) override
    {
        ensurePeerConnectionFactory();
        // TODO: generate unique ids using WebCore's UUID
        const std::string streamLabel = "ToDo-Generate-Stream-UUID";
        rtc::scoped_refptr<webrtc::MediaStreamInterface> stream =
            m_peerConnectionFactory->CreateLocalMediaStream(streamLabel);
        if (!audioDeviceID.empty()) {
            const std::string audioLabel = "ToDo-Generate-Audio-UUID";
            rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track(
                m_peerConnectionFactory->CreateAudioTrack(
                    audioLabel, m_peerConnectionFactory->CreateAudioSource(nullptr)));
            stream->AddTrack(audio_track);
        }
        if (!videoDeviceID.empty()) {
            const std::string videoLabel = "ToDo-Generate-Video-UUID";
            cricket::WebRtcVideoDeviceCapturerFactory factory;
            cricket::VideoCapturer* videoCapturer =
                factory.Create(cricket::Device(videoDeviceID, videoDeviceID));
            rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track(
                m_peerConnectionFactory->CreateVideoTrack(
                    videoLabel, m_peerConnectionFactory->CreateVideoSource(videoCapturer, nullptr)));
            stream->AddTrack(video_track);
        }
        return new MockMediaStream(stream.get(), stream->label());
    }

    /*WRTCInt::RTCVideoRenderer**/ MockVideoRenderer* createVideoRenderer(WRTCInt::RTCMediaStream* stream, WRTCInt::RTCVideoRendererClient* client) override
    {
        return new MockVideoRenderer(static_cast<MockMediaStream*>(stream), client);
    }

    /*WRTCInt::RTCPeerConnection**/ MockRTCPeerConnection* createPeerConnection(/*WRTCInt::RTCPeerConnectionClient*/ WebCore::PeerConnectionBackendWebRtcOrg* client) override
    {
        ensurePeerConnectionFactory();
        return new MockRTCPeerConnection(client, m_peerConnectionFactory.get());
    }

    void ensurePeerConnectionFactory()
    {
        if (m_peerConnectionFactory != nullptr) {
            return;
        }

        m_workerThread.reset(new rtc::Thread);
        m_workerThread->SetName("rtc-worker", this);
        m_workerThread->Start();

        m_signalingThread.reset(new SignalingThreadWrapper);

        m_peerConnectionFactory = webrtc::CreatePeerConnectionFactory(
            m_workerThread.get(),
            m_signalingThread.get(),
            CreateAudioDeviceModule(0),
            CreateWebRtcVideoEncoderFactory(),
            CreateWebRtcVideoDecoderFactory());

        ASSERT(m_peerConnectionFactory);
    }
};
}

namespace WRTCInt
{
WRTCInt::RTCMediaSourceCenter* createRTCMediaSourceCenter()
{
    return new MockRealtimeMediaSourceCenter;
}
}

namespace WebCore {

using namespace PeerConnection;

static std::unique_ptr<PeerConnectionBackend> createPeerConnectionBackendWebRtcOrg(PeerConnectionBackendClient* client)
{
    WRTCInt::init();
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
    m_rtcConnection.reset(getRTCMediaSourceCenter().createPeerConnection(this));
}

void PeerConnectionBackendWebRtcOrg::createOffer(RTCOfferOptions& options, PeerConnection::SessionDescriptionPromise&& promise)
{
    ASSERT(WRTCInt::InvalidRequestId == m_sessionDescriptionRequestId);
    ASSERT(!m_sessionDescriptionPromise);

    WRTCInt::RTCOfferAnswerOptions rtcOptions;
    rtcOptions[WRTCInt::kOfferToReceiveAudio] = !!options.offerToReceiveAudio();
    rtcOptions[WRTCInt::kOfferToReceiveVideo] = !!options.offerToReceiveVideo();
    rtcOptions[WRTCInt::kIceRestart] = options.iceRestart();
    rtcOptions[WRTCInt::kVoiceActivityDetection] = options.voiceActivityDetection();

    int id = m_rtcConnection->createOffer(rtcOptions);
    if (WRTCInt::InvalidRequestId != id) {
        m_sessionDescriptionRequestId = id;
        m_sessionDescriptionPromise = WTFMove(promise);
    } else {
        promise.reject(DOMError::create("Failed to create offer"));
    }
}

void PeerConnectionBackendWebRtcOrg::createAnswer(RTCAnswerOptions& options, PeerConnection::SessionDescriptionPromise&& promise)
{
    ASSERT(WRTCInt::InvalidRequestId == m_sessionDescriptionRequestId);
    ASSERT(!m_sessionDescriptionPromise);

    WRTCInt::RTCOfferAnswerOptions rtcOptions;
    rtcOptions[WRTCInt::kVoiceActivityDetection] = options.voiceActivityDetection();

    int id = m_rtcConnection->createAnswer(rtcOptions);
    if (WRTCInt::InvalidRequestId != id) {
        m_sessionDescriptionRequestId = id;
        m_sessionDescriptionPromise = WTFMove(promise);
    } else {
        promise.reject(DOMError::create("Failed to create answer"));
    }
}

void PeerConnectionBackendWebRtcOrg::setLocalDescription(RTCSessionDescription& desc, PeerConnection::VoidPromise&& promise)
{
    ASSERT(WRTCInt::InvalidRequestId == m_voidRequestId);
    ASSERT(!m_voidPromise);

    WRTCInt::RTCSessionDescription localDesc;
    localDesc.type = desc.type().utf8().data();
    localDesc.sdp = desc.sdp().utf8().data();

    int id = m_rtcConnection->setLocalDescription(localDesc);
    if (WRTCInt::InvalidRequestId != id) {
        m_voidRequestId = id;
        m_voidPromise = WTFMove(promise);
    } else {
        promise.reject(DOMError::create("Failed to parse local description"));
    }
}
RefPtr<RTCSessionDescription> PeerConnectionBackendWebRtcOrg::localDescription() const
{
    // TODO: pendingLocalDescription/currentLocalDescription
    WRTCInt::RTCSessionDescription localDesc;
    m_rtcConnection->localDescription(localDesc);
    String type = localDesc.type.c_str();
    String sdp = localDesc.sdp.c_str();
    return RTCSessionDescription::create(type, sdp);
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
    ASSERT(WRTCInt::InvalidRequestId == m_voidRequestId);
    ASSERT(!m_voidPromise);

    WRTCInt::RTCSessionDescription remoteDesc;
    remoteDesc.type = desc.type().utf8().data();
    remoteDesc.sdp = desc.sdp().utf8().data();

    int id = m_rtcConnection->setRemoteDescription(remoteDesc);
    if (WRTCInt::InvalidRequestId != id) {
        m_voidRequestId = id;
        m_voidPromise = WTFMove(promise);
    } else {
        promise.reject(DOMError::create("Failed to parse remote description"));
    }
}
RefPtr<RTCSessionDescription> PeerConnectionBackendWebRtcOrg::remoteDescription() const
{
    // TODO: pendingRemoteDescription/currentRemoteDescription
    WRTCInt::RTCSessionDescription remoteDesc;
    m_rtcConnection->remoteDescription(remoteDesc);
    String type = remoteDesc.type.c_str();
    String sdp = remoteDesc.sdp.c_str();
    return RTCSessionDescription::create(type, sdp);
}
RefPtr<RTCSessionDescription> PeerConnectionBackendWebRtcOrg::currentRemoteDescription() const
{
    return remoteDescription();
}
RefPtr<RTCSessionDescription> PeerConnectionBackendWebRtcOrg::pendingRemoteDescription() const
{
    return RefPtr<RTCSessionDescription>();
}

void PeerConnectionBackendWebRtcOrg::setConfiguration(RTCConfiguration& config, const MediaConstraints& constraints)
{
    WRTCInt::RTCConfiguration wrtcConfig;
    for(auto& server : config.iceServers()) {
        WRTCInt::RTCIceServer wrtcICEServer;
        wrtcICEServer.credential = server->credential().utf8().data();
        wrtcICEServer.username = server->username().utf8().data();
        for(auto& url : server->urls()) {
            wrtcICEServer.urls.push_back(url.utf8().data());
        }
        wrtcConfig.iceServers.push_back(wrtcICEServer);
    }

    WRTCInt::RTCMediaConstraints wrtcConstraints;
    Vector<MediaConstraint> mediaConstraints;
    constraints.getMandatoryConstraints(mediaConstraints);
    for (auto& c : mediaConstraints) {
        std::string name = c.m_name.utf8().data();
        std::string value = c.m_value.utf8().data();
        wrtcConstraints[name] = value;
    }
    mediaConstraints.clear();
    constraints.getOptionalConstraints(mediaConstraints);
    for (auto& c : mediaConstraints) {
        std::string name = c.m_name.utf8().data();
        std::string value = c.m_value.utf8().data();
        wrtcConstraints[name] = value;
    }

    m_rtcConnection->setConfiguration(wrtcConfig, wrtcConstraints);
}

void PeerConnectionBackendWebRtcOrg::addIceCandidate(RTCIceCandidate& candidate, PeerConnection::VoidPromise&& promise)
{
    WRTCInt::RTCIceCandidate iceCandidate;
    iceCandidate.sdp = candidate.candidate().utf8().data();
    iceCandidate.sdpMid = candidate.sdpMid().utf8().data();
    iceCandidate.sdpMLineIndex = candidate.sdpMLineIndex().valueOr(0);
    bool rc = m_rtcConnection->addIceCandidate(iceCandidate);
    if (rc) {
        promise.resolve(nullptr);
    } else {
        promise.reject(DOMError::create("Failed to add ICECandidate"));
    }
}

void PeerConnectionBackendWebRtcOrg::getStats(MediaStreamTrack*, PeerConnection::StatsPromise&& promise)
{
    int id = m_rtcConnection->getStats();
    if (WRTCInt::InvalidRequestId != id) {
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
    m_rtcConnection->stop();
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
        WRTCInt::RTCMediaStream* stream = static_cast<RealtimeMediaSourceWebRtcOrg&>(source).rtcStream();
        if (stream) {
            m_rtcConnection->addStream(stream);
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
    WRTCInt::DataChannelInit initData;
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
    /*WRTCInt::RTCDataChannel*/ MockRTCDataChannel* channel = m_rtcConnection->createDataChannel(label.utf8().data(), initData);
    return channel
        ? std::make_unique<RTCDataChannelHandlerWebRtcOrg>(channel)
        : nullptr;
}

// ===========  /*WRTCInt::RTCPeerConnectionClient*/ WebCore::PeerConnectionBackendWebRtcOrg ==========

void PeerConnectionBackendWebRtcOrg::requestSucceeded(int id, const WRTCInt::RTCSessionDescription& desc)
{
    ASSERT(id == m_sessionDescriptionRequestId);
    ASSERT(m_sessionDescriptionPromise);

    // printf("%p:%s: %d, type=%s sdp=\n%s\n", this, __func__, id, desc.type.c_str(), desc.sdp.c_str());

    String type = desc.type.c_str();
    String sdp = desc.sdp.c_str();

    RefPtr<RTCSessionDescription> sessionDesc(RTCSessionDescription::create(type, sdp));
    m_sessionDescriptionPromise->resolve(sessionDesc);

    m_sessionDescriptionRequestId = WRTCInt::InvalidRequestId;
    m_sessionDescriptionPromise = WTF::Nullopt;
}

void PeerConnectionBackendWebRtcOrg::requestSucceeded(int id, const std::vector<std::unique_ptr<WRTCInt::RTCStatsReport>>& reports)
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

    m_voidRequestId = WRTCInt::InvalidRequestId;
    m_voidPromise = WTF::Nullopt;
}

void PeerConnectionBackendWebRtcOrg::requestFailed(int id, const std::string& error)
{
    if (id == m_voidRequestId) {
        ASSERT(m_voidPromise);
        ASSERT(!m_sessionDescriptionPromise);
        m_voidPromise->reject(DOMError::create(error.c_str()));
        m_voidPromise = WTF::Nullopt;
        m_voidRequestId = WRTCInt::InvalidRequestId;
    } else if (id == m_sessionDescriptionRequestId) {
        ASSERT(m_sessionDescriptionPromise);
        ASSERT(!m_voidPromise);
        m_sessionDescriptionPromise->reject(DOMError::create(error.c_str()));
        m_sessionDescriptionPromise = WTF::Nullopt;
        m_sessionDescriptionRequestId = WRTCInt::InvalidRequestId;
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
    WRTCInt::RTCMediaStream *stream,
    const std::vector<std::string> &audioDevices,
    const std::vector<std::string> &videoDevices)
{
    ASSERT(m_client);

    std::shared_ptr<WRTCInt::RTCMediaStream> rtcStream;
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

void PeerConnectionBackendWebRtcOrg::didGenerateIceCandidate(const WRTCInt::RTCIceCandidate& iceCandidate)
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

void PeerConnectionBackendWebRtcOrg::didChangeSignalingState(WRTCInt::SignalingState state)
{
    ASSERT(m_client);
    PeerConnectionStates::SignalingState signalingState = PeerConnectionStates::SignalingState::Stable;
    switch(state)
    {
        case WRTCInt::Stable:
            signalingState = PeerConnectionStates::SignalingState::Stable;
            break;
        case WRTCInt::HaveLocalOffer:
            signalingState = PeerConnectionStates::SignalingState::HaveLocalOffer;
            break;
        case WRTCInt::HaveRemoteOffer:
            signalingState = PeerConnectionStates::SignalingState::HaveRemoteOffer;
            break;
        case WRTCInt::HaveLocalPrAnswer:
            signalingState = PeerConnectionStates::SignalingState::HaveLocalPrAnswer;
            break;
        case WRTCInt::HaveRemotePrAnswer:
            signalingState = PeerConnectionStates::SignalingState::HaveRemotePrAnswer;
            break;
        case WRTCInt::Closed:
            signalingState = PeerConnectionStates::SignalingState::Closed;
            break;
        default:
            return;
    }
    m_client->setSignalingState(signalingState);
}

void PeerConnectionBackendWebRtcOrg::didChangeIceGatheringState(WRTCInt::IceGatheringState state)
{
    ASSERT(m_client);
    PeerConnectionStates::IceGatheringState iceGatheringState = PeerConnectionStates::IceGatheringState::New;
    switch(state)
    {
        case WRTCInt::IceGatheringNew:
            iceGatheringState = PeerConnectionStates::IceGatheringState::New;
            break;
        case WRTCInt::IceGatheringGathering:
            iceGatheringState = PeerConnectionStates::IceGatheringState::Gathering;
            break;
        case WRTCInt::IceGatheringComplete:
            iceGatheringState = PeerConnectionStates::IceGatheringState::Complete;
            break;
        default:
            return;
    }
    m_client->updateIceGatheringState(iceGatheringState);
}

void PeerConnectionBackendWebRtcOrg::didChangeIceConnectionState(WRTCInt::IceConnectionState state)
{
    ASSERT(m_client);
    PeerConnectionStates::IceConnectionState iceConnectionState = PeerConnectionStates::IceConnectionState::New;
    switch(state)
    {
        case WRTCInt::IceConnectionNew:
            iceConnectionState = PeerConnectionStates::IceConnectionState::New;
            break;
        case WRTCInt::IceConnectionChecking:
            iceConnectionState = PeerConnectionStates::IceConnectionState::Checking;
            break;
        case WRTCInt::IceConnectionConnected:
            iceConnectionState = PeerConnectionStates::IceConnectionState::Connected;
            break;
        case WRTCInt::IceConnectionCompleted:
            iceConnectionState = PeerConnectionStates::IceConnectionState::Completed;
            break;
        case WRTCInt::IceConnectionFailed:
            iceConnectionState = PeerConnectionStates::IceConnectionState::Failed;
            break;
        case WRTCInt::IceConnectionDisconnected:
            iceConnectionState = PeerConnectionStates::IceConnectionState::Disconnected;
            break;
        case WRTCInt::IceConnectionClosed:
            iceConnectionState = PeerConnectionStates::IceConnectionState::Closed;
            break;
        default:
            return;
    }
    m_client->updateIceConnectionState(iceConnectionState);
}

void PeerConnectionBackendWebRtcOrg::didAddRemoteDataChannel(/*WRTCInt::RTCDataChannel*/ MockRTCDataChannel* channel)
{
    std::unique_ptr<RTCDataChannelHandler> handler = std::make_unique<RTCDataChannelHandlerWebRtcOrg>(channel);
    m_client->addRemoteDataChannel(WTFMove(handler));
}

RTCDataChannelHandlerWebRtcOrg::RTCDataChannelHandlerWebRtcOrg(/*WRTCInt::RTCDataChannel*/ MockRTCDataChannel* dataChannel)
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
    return m_rtcDataChannel->label().c_str();
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
    return m_rtcDataChannel->protocol().c_str();
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

void RTCDataChannelHandlerWebRtcOrg::didChangeReadyState(WRTCInt::DataChannelState state)
{
    RTCDataChannelHandlerClient::ReadyState readyState = RTCDataChannelHandlerClient::ReadyStateConnecting;
    switch(state) {
        case WRTCInt::DataChannelConnecting:
            readyState = RTCDataChannelHandlerClient::ReadyStateConnecting;
            break;
        case WRTCInt::DataChannelOpen:
            readyState = RTCDataChannelHandlerClient::ReadyStateOpen;
            break;
        case WRTCInt::DataChannelClosing:
            readyState = RTCDataChannelHandlerClient::ReadyStateClosing;
            break;
        case WRTCInt::DataChannelClosed:
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
