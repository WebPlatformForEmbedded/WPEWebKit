/* Copyright (C) 2019 RDK Management.  All rights reserved.
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
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
* PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDERS. OR
* CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
* EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
* PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
* OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "config.h"
#include "PlatformSpeechSynthesizerTTSClient.h"
#include "PlatformSpeechSynthesisUtterance.h"
#include "Logging.h"
#include "Page.h"

#include <sys/types.h>
#include <unistd.h>

#if ENABLE(SPEECH_SYNTHESIS)

#define MAX_ALLOWED_TEXT_LENGTH 1024

#define CHECK_TTS_SESSION(utterance) do {\
    if(!m_ttsConnected) {\
        LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, Speech Synthesis: Connection with TTS is not established", __FUNCTION__);\
        notifyClient(utterance, SpeechErrorSynthesisUnavailable);\
        return;\
        }\
    if(!m_ttsClient || !m_ttsSessionId) {\
        notifyClient(utterance, SpeechErrorNotAllowed);\
        return;\
    }} while(0)

namespace WebCore {

// PlatformSpeechSynthesizer
WEBCORE_EXPORT std::unique_ptr<PlatformSpeechSynthesizer> PlatformSpeechSynthesizer::create(PlatformSpeechSynthesizerClient *client)
{
    return std::make_unique<PlatformSpeechSynthesizerTTSClient>(client);
}

WEBCORE_EXPORT PlatformSpeechSynthesizer::PlatformSpeechSynthesizer(PlatformSpeechSynthesizerClient* client) : m_speechSynthesizerClient(client) {}
WEBCORE_EXPORT PlatformSpeechSynthesizer::~PlatformSpeechSynthesizer() {}

void PlatformSpeechSynthesizer::speak(RefPtr<PlatformSpeechSynthesisUtterance>&&) {}
void PlatformSpeechSynthesizer::pause() {}
void PlatformSpeechSynthesizer::resume() {}
void PlatformSpeechSynthesizer::cancel() {}
void PlatformSpeechSynthesizer::initializeVoiceList() {}

// PlatformSpeechSynthesizerTTSClient
double PlatformSpeechSynthesizerTTSClient::m_TTSVolume = 0.0;
double PlatformSpeechSynthesizerTTSClient::m_TTSRate = 0.0;
static bool bSpeechSynthOverrideSysTTSConfig = getenv("SPEECH_SYNTHESIS_OVERRIDE_SYSTEM_TTS_CONFIG");

PlatformSpeechSynthesizerTTSClient::PlatformSpeechSynthesizerTTSClient(PlatformSpeechSynthesizerClient* client)
    : PlatformSpeechSynthesizer(client), m_shouldCacheUtterance(true), m_ttsSessionId(0), m_ttsEnabled(false), m_ttsConnected(false)
{
    m_ttsClient = TTS::TTSClient::create(this);

    auto ttsConnectionTO = 10_s;
    RunLoop::main().dispatchAfter(ttsConnectionTO, [weakThis = m_weakPtrFactory.createWeakPtr(*this)] () {
        if (!weakThis) {
            LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, invalid this pointer", __FUNCTION__);
            return;
        }

        weakThis->m_shouldCacheUtterance = false;
        if(weakThis->m_firstUtterance.get()) {
            if(weakThis->m_ttsSessionId == 0)
                weakThis->notifyClient(weakThis->m_firstUtterance, SpeechErrorSynthesisUnavailable);
            LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, clearing cached utterace=%p", __FUNCTION__, weakThis->m_firstUtterance.get());
            weakThis->m_firstUtterance = nullptr;
        }
    });
}

PlatformSpeechSynthesizerTTSClient::~PlatformSpeechSynthesizerTTSClient()
{
    if(m_ttsClient) {
        m_ttsSessionId = 0;
        m_ttsEnabled = 0;
        delete m_ttsClient;
    }
}

void PlatformSpeechSynthesizerTTSClient::initializeVoiceList()
{
    if(m_ttsClient) {
        const char *v = NULL;
        TTS::Configuration config;
        std::vector<std::string> voices;

        TTS::TTS_Error err = m_ttsClient->getTTSConfiguration(config);
        m_ttsClient->listVoices(config.language, voices);
        for(unsigned int i = 0; i < voices.size(); i++) {
            v = voices[i].c_str();
            m_voiceList.append(PlatformSpeechSynthesisVoice::create(
                        String(v), String(v), String((err == TTS::TTS_OK) ? config.language.c_str() : "en-US"), true, true));
        }

        m_TTSVolume = config.volume;
        m_TTSRate = config.rate;

        onVoiceChanged("");
    }
}

const Vector<RefPtr<PlatformSpeechSynthesisVoice>>& PlatformSpeechSynthesizerTTSClient::voiceList() const
{
    if(!m_voiceListIsInitialized)
        const_cast<PlatformSpeechSynthesizerTTSClient*>(this)->initializeVoiceList();
    const_cast<PlatformSpeechSynthesizerTTSClient*>(this)->m_voiceListIsInitialized = !m_voiceList.isEmpty();
    return m_voiceList;
}

void PlatformSpeechSynthesizerTTSClient::setPageMediaVolume(float volume)
{
    double readVolume = client()->getPageMediaVolume();
    LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, read MediaVolume : %lf, volume to be set : %lf", __FUNCTION__, readVolume, volume);
    if(volume == readVolume)
        return;

    LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, set MediaVolume to : %lf", __FUNCTION__, volume);
    client()->setPageMediaVolume(volume);
}

void PlatformSpeechSynthesizerTTSClient::speak(RefPtr<PlatformSpeechSynthesisUtterance>&& u)
{
    RefPtr<PlatformSpeechSynthesisUtterance> utterance = WTFMove(u);
    LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, utterance=%p, progressing=%d", __FUNCTION__, utterance.get(), m_utterancesInProgress.size());

    if(m_shouldCacheUtterance) {
        if(m_firstUtterance.get())
            notifyClient(m_firstUtterance, SpeechErrorSynthesisUnavailable);
        m_firstUtterance = utterance;
        LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, cached utterance=%p", __FUNCTION__, m_firstUtterance.get());
        return;
    }

    CHECK_TTS_SESSION(utterance);

    if(utterance->text().isEmpty() || utterance->text().length() < 1) {
        notifyClient(utterance, SpeechErrorInvalidArgument);
        return;
    } else if(utterance->text().length() > MAX_ALLOWED_TEXT_LENGTH) {
        notifyClient(utterance, SpeechErrorTextTooLong);
        return;
    }

    if(bSpeechSynthOverrideSysTTSConfig)
    {
        TTS::Configuration config;
        config.volume = utterance->volume() * 100;
        config.rate = (utterance->rate() <= 1.0 ? 50.0 : (utterance->rate() <= 5.0 ? 75.0 : 100.0));

        if((int)m_TTSVolume != (int)config.volume || (int)m_TTSRate != (int)config.rate) {
            if(m_ttsClient->setTTSConfiguration(config) != TTS::TTS_OK) {
                notifyClient(utterance, SpeechErrorSynthesisFailed);
                return;
            }
            m_TTSVolume = config.volume;
            m_TTSRate = config.rate;
        }
    }


    TTS::SpeechData sdata;
    sdata.text = utterance->text().utf8().data();
    sdata.id = (uint32_t)utterance.get();
    TTS::TTS_Error err = m_ttsClient->speak(m_ttsSessionId, sdata);
    if(err != TTS::TTS_OK) {
        if(err == TTS::TTS_RESOURCE_BUSY || err == TTS::TTS_SESSION_NOT_ACTIVE)
            notifyClient(utterance, SpeechErrorAudioBusy);
        else
            notifyClient(utterance, SpeechErrorSynthesisFailed);
    } else {
        m_utterancesInProgress.append(utterance);
        m_currentUtterance = utterance;
        if(utterance == m_firstUtterance)
            m_firstUtterance = nullptr;
        setPageMediaVolume(0.25);
    }
}

void PlatformSpeechSynthesizerTTSClient::cancel()
{
    LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, utterance=%p, progressing=%d", __FUNCTION__, m_currentUtterance.get(), m_utterancesInProgress.size());

    if(m_shouldCacheUtterance && m_firstUtterance.get()) {
        notifyClient(m_firstUtterance, SpeechErrorCanceled);
        m_firstUtterance = nullptr;
    }

    CHECK_TTS_SESSION(m_currentUtterance);

    if(m_currentUtterance.get() && m_ttsClient->isSpeaking(m_ttsSessionId)) {
        m_ttsClient->abort(m_ttsSessionId);
        speakingFinished((uint32_t)m_currentUtterance.get(), SpeechErrorInterrupted);
    }
}

void PlatformSpeechSynthesizerTTSClient::pause()
{
    LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, utterance=%p, progressing=%d", __FUNCTION__, m_currentUtterance.get(), m_utterancesInProgress.size());
    CHECK_TTS_SESSION(m_currentUtterance);

    if(m_currentUtterance.get() && m_ttsClient && m_ttsClient->isSpeaking(m_ttsSessionId)) {
        m_ttsClient->pause(m_ttsSessionId, (uint32_t)m_currentUtterance.get());
    }
}

void PlatformSpeechSynthesizerTTSClient::resume()
{
    LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, utterance=%p, progressing=%d", __FUNCTION__, m_currentUtterance.get(), m_utterancesInProgress.size());
    CHECK_TTS_SESSION(m_currentUtterance);

    if(m_currentUtterance.get() && m_ttsClient && m_ttsClient->isSpeaking(m_ttsSessionId)) {
        m_ttsClient->resume(m_ttsSessionId, (uint32_t)m_currentUtterance.get());
    }
}

// TTSConnectionCallback
void PlatformSpeechSynthesizerTTSClient::onTTSServerConnected()
{
    LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, Speech Synthesis: Connection with TTS is established", __FUNCTION__);

    RunLoop::main().dispatch([weakThis = m_weakPtrFactory.createWeakPtr(*this)] () {
        if (!weakThis) {
            LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, invalid this pointer", __FUNCTION__);
            return;
        }
        weakThis->m_ttsConnected = true;
        weakThis->m_shouldCacheUtterance = false;

        if(weakThis->m_ttsClient) {
            if(weakThis->m_ttsSessionId == 0) {
                weakThis->m_ttsSessionId = weakThis->m_ttsClient->createSession((uint32_t)getpid(), "WPE", weakThis.get());
                weakThis->m_ttsClient->requestExtendedEvents(weakThis->m_ttsSessionId, 0xFFFF);
            }
            weakThis->onVoiceChanged("");

            if(weakThis->m_firstUtterance.get()) {
                LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, starting cached utterace=%p", __FUNCTION__, weakThis->m_firstUtterance.get());
                weakThis->speak(weakThis->m_firstUtterance.get());
            }
        }
    });
}

void PlatformSpeechSynthesizerTTSClient::onTTSServerClosed()
{
    LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, Speech Synthesis: Connection with TTS is closed", __FUNCTION__);
    m_ttsConnected = false;
    m_ttsSessionId = 0;

    if(m_currentUtterance.get())
        speakingFinished((uint32_t)m_currentUtterance.get(), SpeechErrorInterrupted);
}

void PlatformSpeechSynthesizerTTSClient::onTTSStateChanged(bool enabled)
{
    m_ttsEnabled = enabled;
}

void PlatformSpeechSynthesizerTTSClient::onVoiceChanged(std::string)
{
    RunLoop::main().dispatch([weakThis = m_weakPtrFactory.createWeakPtr(*this)] () {
        if (!weakThis) {
            LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, invalid this pointer", __FUNCTION__);
            return;
        }

        weakThis->client()->voicesDidChange();
    });
}

// TTSSessionCallback
void PlatformSpeechSynthesizerTTSClient::onTTSSessionCreated(uint32_t, uint32_t) {}
void PlatformSpeechSynthesizerTTSClient::onResourceAcquired(uint32_t, uint32_t) {}
void PlatformSpeechSynthesizerTTSClient::onResourceReleased(uint32_t, uint32_t) {}

void PlatformSpeechSynthesizerTTSClient::onWillSpeak(uint32_t, uint32_t, TTS::SpeechData &speechData)
{
    LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, speechId=%u(%p), progressing=%d", __FUNCTION__, speechData.id, (void*)speechData.id, m_utterancesInProgress.size());
    RunLoop::main().dispatch([weakThis = m_weakPtrFactory.createWeakPtr(*this)] () {
        if (!weakThis) {
            LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, invalid this pointer", __FUNCTION__);
            return;
        }

        weakThis->setPageMediaVolume(0.25);
    });
}

void PlatformSpeechSynthesizerTTSClient::onSpeechStart(uint32_t, uint32_t, TTS::SpeechData &speechData)
{
    LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, speechId=%u(%p), progressing=%d", __FUNCTION__, speechData.id, (void*)speechData.id, m_utterancesInProgress.size());
    RunLoop::main().dispatch([weakThis = m_weakPtrFactory.createWeakPtr(*this), speechId = speechData.id] () {
        if (!weakThis) {
            LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, invalid this pointer", __FUNCTION__);
            return;
        }

        size_t index = weakThis->m_utterancesInProgress.find((PlatformSpeechSynthesisUtterance*)speechId);
        if(index != WTF::notFound) {
            weakThis->client()->didStartSpeaking(*weakThis->m_utterancesInProgress.at(index));
        }
    });
}

void PlatformSpeechSynthesizerTTSClient::onSpeechPause(uint32_t, uint32_t, uint32_t speechId)
{
    LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, speechId=%u(%p), progressing=%d", __FUNCTION__, speechId, (void*)speechId, m_utterancesInProgress.size());
    RunLoop::main().dispatch([weakThis = m_weakPtrFactory.createWeakPtr(*this), speechId] () {
        if (!weakThis) {
            LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, invalid this pointer", __FUNCTION__);
            return;
        }
        weakThis->setPageMediaVolume(1);

        size_t index = weakThis->m_utterancesInProgress.find((PlatformSpeechSynthesisUtterance*)speechId);
        if(index != WTF::notFound) {
            weakThis->client()->didPauseSpeaking(*weakThis->m_utterancesInProgress.at(index));
        }
    });
}

void PlatformSpeechSynthesizerTTSClient::onSpeechResume(uint32_t, uint32_t, uint32_t speechId)
{
    LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, speechId=%u(%p), progressing=%d", __FUNCTION__, speechId, (void*)speechId, m_utterancesInProgress.size());
    RunLoop::main().dispatch([weakThis = m_weakPtrFactory.createWeakPtr(*this), speechId] () {
        if (!weakThis) {
            LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, invalid this pointer", __FUNCTION__);
            return;
        }
        weakThis->setPageMediaVolume(0.25);

        size_t index = weakThis->m_utterancesInProgress.find((PlatformSpeechSynthesisUtterance*)speechId);
        if(index != WTF::notFound) {
            weakThis->client()->didResumeSpeaking(*weakThis->m_utterancesInProgress.at(index));
        }
    });
}

void PlatformSpeechSynthesizerTTSClient::onSpeechCancelled(uint32_t, uint32_t, uint32_t speechId)
{
    speakingFinished(speechId, SpeechErrorCanceled);
}

void PlatformSpeechSynthesizerTTSClient::onSpeechInterrupted(uint32_t, uint32_t, uint32_t speechId)
{
    speakingFinished(speechId, SpeechErrorInterrupted);
}

void PlatformSpeechSynthesizerTTSClient::onNetworkError(uint32_t, uint32_t, uint32_t speechId)
{
    speakingFinished(speechId, SpeechErrorNetwork);
}

void PlatformSpeechSynthesizerTTSClient::onPlaybackError(uint32_t, uint32_t, uint32_t speechId)
{
    speakingFinished(speechId, SpeechErrorSynthesisFailed);
}

void PlatformSpeechSynthesizerTTSClient::onSpeechComplete(uint32_t, uint32_t, TTS::SpeechData &speechData)
{
    speakingFinished(speechData.id, SpeechErrorNone);
}

void PlatformSpeechSynthesizerTTSClient::speakingFinished(uint32_t speechId, SpeechError error)
{
    LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, Error=%d, speechId=%u(%p), progressing=%d", __FUNCTION__, error, speechId, (void*)speechId, m_utterancesInProgress.size());
    auto speakingFinishedInternal = [weakThis = m_weakPtrFactory.createWeakPtr(*this), speechId, error] () {
        if (!weakThis) {
            LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, invalid this pointer", __FUNCTION__);
            return;
        }

        size_t index = weakThis->m_utterancesInProgress.find((PlatformSpeechSynthesisUtterance*)speechId);
        if(index != WTF::notFound) {
            weakThis->notifyClient(weakThis->m_utterancesInProgress.at(index), error);
            weakThis->m_utterancesInProgress.remove(index);
        } else if(!weakThis->m_currentUtterance) {
            LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, Resetting media volume", __FUNCTION__);
            weakThis->setPageMediaVolume(1);
        }
    };

    if(RunLoop::isMain()) {
        LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, Main Loop cancellation", __FUNCTION__);
        speakingFinishedInternal();
    } else {
        LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, Dispatching on Main Loop", __FUNCTION__);
        RunLoop::main().dispatch(WTFMove(speakingFinishedInternal));
    }
}

void PlatformSpeechSynthesizerTTSClient::notifyClient(RefPtr<PlatformSpeechSynthesisUtterance> utterance, SpeechError error)
{
    LOG(SpeechSynthesis, "PlatformSpeechSynthesizerTTSClient::%s, Error=%d, currentUtterance=%p, utterance=%p, progressing=%d, lastoccurance=%d",
            __FUNCTION__, error, m_currentUtterance.get(), utterance.get(), m_utterancesInProgress.size(), (m_currentUtterance == utterance));
    if(!utterance) {
        LOG_ERROR("not firing event as no utterance is attached");
        return;
    }

    if(m_currentUtterance == utterance)
        m_currentUtterance = nullptr;

    if(!m_currentUtterance)
        setPageMediaVolume(1);

    if(error == SpeechErrorNone)
        client()->didFinishSpeaking(*utterance);
    else
        client()->speakingErrorOccurred(*utterance, error);
}

} // namespace WebCore

#endif // ENABLE(SPEECH_SYNTHESIS)
