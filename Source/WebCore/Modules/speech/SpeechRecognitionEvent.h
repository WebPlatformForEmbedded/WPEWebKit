
#ifndef SpeechRecognitionEvent_h
#define SpeechRecognitionEvent_h

#if ENABLE(SPEECH_RECOGNITION)
#include "Event.h"
#include "EventNames.h"
#include "Logging.h"
#include "SpeechRecognitionResult.h"
#include "SpeechRecognitionResultList.h"
#include "wtf/RefPtr.h"

namespace WebCore {

class Document;

struct SpeechRecognitionEventInit : public EventInit {
    SpeechRecognitionEventInit();

    unsigned long resultIndex;
    RefPtr<SpeechRecognitionResultList> results;
};

class SpeechRecognitionEvent : public Event {
public:
    static PassRefPtr<SpeechRecognitionEvent> create();
    static PassRefPtr<SpeechRecognitionEvent> create(const AtomicString&, const SpeechRecognitionEventInit&);
    virtual ~SpeechRecognitionEvent() {}

    static Ref<SpeechRecognitionEvent> createResult(unsigned long resultIndex, const Vector<RefPtr<SpeechRecognitionResult> >& results);
    static SpeechRecognitionEvent& createNoMatch(PassRefPtr<SpeechRecognitionResult>);

    unsigned long resultIndex() const { return m_resultIndex; }
    SpeechRecognitionResultList* results() const { return m_results.get(); }

    // These two methods are here to satisfy the specification which requires these attrubutes to exist.
    Document* interpretation() { return 0; }
    Document* emma() { return 0; }

    // Event
    virtual EventInterface eventInterface() const override;

private:
    SpeechRecognitionEvent();
    SpeechRecognitionEvent(const AtomicString&, const SpeechRecognitionEventInit&);
    SpeechRecognitionEvent(const AtomicString& eventName, unsigned long resultIndex, PassRefPtr<SpeechRecognitionResultList> results);

    unsigned long m_resultIndex;
    RefPtr<SpeechRecognitionResultList> m_results;
};

} // namespace WebCore

#endif // ENABLE(SPEECH_RECOGNITION)

#endif // SpeechRecognitionEvent_h
