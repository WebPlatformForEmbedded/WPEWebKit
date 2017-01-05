
#include "config.h"

#if ENABLE(SPEECH_RECOGNITION)
#include "SpeechRecognitionAlternative.h"

namespace WebCore {

PassRefPtr<SpeechRecognitionAlternative>  SpeechRecognitionAlternative::create(const String& transcript, double confidence)
{
    
    LOG(SpeechRecognition, "%s:%s:%d   \n", __FILE__, __func__, __LINE__); 

    return adoptRef(new SpeechRecognitionAlternative(transcript, confidence));
}

SpeechRecognitionAlternative::SpeechRecognitionAlternative(const String& transcript, double confidence)
    : m_transcript(transcript)
    , m_confidence(confidence)
{
    LOG(SpeechRecognition, "%s:%s:%d\n", __FILE__, __func__, __LINE__); 
}

} // namespace WebCore
#endif //ENABLE(SPEECH_RECOGNITION)
