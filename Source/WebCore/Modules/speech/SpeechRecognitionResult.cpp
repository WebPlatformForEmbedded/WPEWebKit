
#include "config.h"

#if ENABLE(SPEECH_RECOGNITION)
#include "SpeechRecognitionResult.h"

namespace WebCore {

PassRefPtr<SpeechRecognitionResult> SpeechRecognitionResult::create(const Vector<RefPtr<SpeechRecognitionAlternative> >& alternatives, bool final)
{
    LOG(SpeechRecognition, "%s:%s:%d\n", __FILE__, __func__, __LINE__); 

    return adoptRef(new SpeechRecognitionResult(alternatives, final));
}

SpeechRecognitionAlternative* SpeechRecognitionResult::item(unsigned long index)
{
    if (index >= m_alternatives.size())
        return 0;

    return m_alternatives[index].get();
}

SpeechRecognitionResult::SpeechRecognitionResult(const Vector<RefPtr<SpeechRecognitionAlternative> >& alternatives, bool final)
    : m_final(final)
    , m_alternatives(alternatives)
{
    LOG(SpeechRecognition, "%s:%s:%d\n", __FILE__, __func__, __LINE__); 
}

} // namespace WebCore

#endif //ENABLE(SPEECH_RECOGNITION)
