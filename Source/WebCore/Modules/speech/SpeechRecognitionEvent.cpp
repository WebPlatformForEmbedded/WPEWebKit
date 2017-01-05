
#include "config.h"

#if ENABLE(SPEECH_RECOGNITION)
#include "SpeechRecognitionEvent.h"

namespace WebCore {

SpeechRecognitionEventInit::SpeechRecognitionEventInit()
    : resultIndex(0)
{
}

PassRefPtr<SpeechRecognitionEvent> SpeechRecognitionEvent::create()
{
    return adoptRef(new SpeechRecognitionEvent());
}

PassRefPtr<SpeechRecognitionEvent> SpeechRecognitionEvent::create(const AtomicString& eventName, const SpeechRecognitionEventInit& initializer)
{
    LOG(SpeechRecognition, "%s:%s:%d \n\n", __FILE__, __func__, __LINE__);

    return adoptRef(new SpeechRecognitionEvent(eventName, initializer));
}

Ref<SpeechRecognitionEvent> SpeechRecognitionEvent::createResult(unsigned long resultIndex, const Vector<RefPtr<SpeechRecognitionResult> >& results)
{
    return adoptRef(*new SpeechRecognitionEvent(eventNames().resultEvent, resultIndex, SpeechRecognitionResultList::create(results)));
}

SpeechRecognitionEvent& SpeechRecognitionEvent::createNoMatch(PassRefPtr<SpeechRecognitionResult> result)
{
    LOG(SpeechRecognition, "%s:%s:%d \n\n", __FILE__, __func__, __LINE__);

    if (result) {
        Vector<RefPtr<SpeechRecognitionResult> > results;
        results.append(result);
        return (*new SpeechRecognitionEvent(eventNames().nomatchEvent, 0, SpeechRecognitionResultList::create(results)));
    }

    return (*new SpeechRecognitionEvent(eventNames().nomatchEvent, 0, 0 ));
}

EventInterface SpeechRecognitionEvent::eventInterface() const
{
    return SpeechRecognitionEventInterfaceType;
}

SpeechRecognitionEvent::SpeechRecognitionEvent()
    : m_resultIndex(0)
{
    LOG(SpeechRecognition, "%s:%s:%d \n\n", __FILE__, __func__, __LINE__);

}

SpeechRecognitionEvent::SpeechRecognitionEvent(const AtomicString& eventName, const SpeechRecognitionEventInit& initializer)
    : Event(eventName, initializer)
    , m_resultIndex(initializer.resultIndex)
    , m_results(initializer.results)
{
    LOG(SpeechRecognition, "%s:%s:%d \n\n", __FILE__, __func__, __LINE__);

}

SpeechRecognitionEvent::SpeechRecognitionEvent(const AtomicString& eventName, unsigned long resultIndex, PassRefPtr<SpeechRecognitionResultList> results)
    : Event(eventName, /*canBubble=*/false, /*cancelable=*/false)
    , m_resultIndex(resultIndex)
    , m_results(results)
{
    LOG(SpeechRecognition, "%s:%s:%d \n\n", __FILE__, __func__, __LINE__);

}

} // namespace WebCore
#endif //ENABLE(SPEECH_RECOGNITION)
