
#include "config.h"

#if ENABLE(SPEECH_RECOGNITION)

#include "PlatformSpeechRecognizer.h"
#include "PlatformSpeechRecognitionProviderWPE.h"

using namespace WebCore;
using namespace std;

namespace WebCore {

PlatformSpeechRecognizer::PlatformSpeechRecognizer(PlatformSpeechRecognizerClient* client)
    : m_speechRecognizerClient(client)
    , m_platformSpeechWrapper(std::make_unique<PlatformSpeechRecognitionProviderWPE>(this))
{
    printf ("%s:%s:%d\n", __FILE__, __func__, __LINE__);
}

PlatformSpeechRecognizer::~PlatformSpeechRecognizer()
{
}

void PlatformSpeechRecognizer::start()
{
    LOG(SpeechRecognition, "%s:%s:%d\n", __FILE__, __func__, __LINE__);
    ASSERT(m_platformSpeechWrapper);
    m_platformSpeechWrapper->start();
}

void PlatformSpeechRecognizer::abort()
{
    LOG(SpeechRecognition, "%s:%s:%d\n", __FILE__, __func__, __LINE__);
    ASSERT(m_platformSpeechWrapper);
    m_platformSpeechWrapper->abort();
} 

void PlatformSpeechRecognizer::stop()
{
    LOG(SpeechRecognition, "%s:%s:%d\n", __FILE__, __func__, __LINE__);
    ASSERT(m_platformSpeechWrapper);
    m_platformSpeechWrapper->stop();
}

void PlatformSpeechRecognizer::setContinuous(bool continuous)
{
    LOG(SpeechRecognition, "%s:%s:%d\n", __FILE__, __func__, __LINE__);
    fflush(stdout);
    m_platformSpeechWrapper->setContinuous(continuous);
}

void PlatformSpeechRecognizer::setInterimResults(bool interimResults)
{
    LOG(SpeechRecognition, "%s:%s:%d\n", __FILE__, __func__, __LINE__);
    fflush(stdout);
    m_platformSpeechWrapper->setInterimResults(interimResults);
}

} //WebCore

#endif //SPEECH_RECOGNITION
