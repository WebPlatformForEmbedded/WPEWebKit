
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
    printf ("This is line %d of file %s (function %s)\n",__LINE__, __FILE__, __func__);
}

PlatformSpeechRecognizer::~PlatformSpeechRecognizer()
{
}

void PlatformSpeechRecognizer::start()
{
    printf("Inside:%s:%s:%d\n", __FILE__, __func__, __LINE__);
    ASSERT(m_platformSpeechWrapper);
    m_platformSpeechWrapper->start();
}

void PlatformSpeechRecognizer::abort()
{
    printf("Inside:%s:%s:%d\n", __FILE__, __func__, __LINE__);
    ASSERT(m_platformSpeechWrapper);
    m_platformSpeechWrapper->abort();
} 

void PlatformSpeechRecognizer::stop()
{
    printf("Inside:%s:%s:%d\n", __FILE__, __func__, __LINE__);
    ASSERT(m_platformSpeechWrapper);
    m_platformSpeechWrapper->stop();
}

void PlatformSpeechRecognizer::setContinuous(bool continuous)
{
    printf("Inside:%s:%s:%d\n", __FILE__, __func__, __LINE__);
    fflush(stdout);
    m_platformSpeechWrapper->setContinuous(continuous);
}

void PlatformSpeechRecognizer::setInterimResults(bool interimResults)
{
    printf("Inside:%s:%s:%d\n", __FILE__, __func__, __LINE__);
    fflush(stdout);
    m_platformSpeechWrapper->setInterimResults(interimResults);
}

} //WebCore

#endif //SPEECH_RECOGNITION
