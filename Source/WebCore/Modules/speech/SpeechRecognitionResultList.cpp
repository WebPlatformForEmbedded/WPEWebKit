
#include "config.h"

#if ENABLE(SPEECH_RECOGNITION)

#include "SpeechRecognitionResultList.h"

namespace WebCore {
PassRefPtr<SpeechRecognitionResultList> SpeechRecognitionResultList::create(const Vector<RefPtr<SpeechRecognitionResult> >& results)
{
    LOG(SpeechRecognition, "%s:%s:%d \n\n", __FILE__, __func__, __LINE__);

    return adoptRef(new SpeechRecognitionResultList(results));
}

SpeechRecognitionResult* SpeechRecognitionResultList::item(unsigned long index)
{

    if (index >= m_results.size())
        return 0;

    return m_results[index].get();
}

SpeechRecognitionResultList::SpeechRecognitionResultList(const Vector<RefPtr<SpeechRecognitionResult> >& results)
    : m_results(results)
{
    LOG(SpeechRecognition, "%s:%s:%d \n\n", __FILE__, __func__, __LINE__);
}

} // namespace WebCore

#endif // ENABLE(SPEECH_RECOGNITION)
