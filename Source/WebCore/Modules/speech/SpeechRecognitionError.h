
#ifndef SpeechRecognitionError_h
#define SpeechRecognitionError_h

#if ENABLE(SPEECH_RECOGNITION)
#include "Event.h"
#include "EventNames.h"
#include "Logging.h"
#include "EventTarget.h"
#include <wtf/PassRefPtr.h>
#include "wtf/RefCounted.h"
#include "wtf/text/WTFString.h"

namespace WebCore {

struct SpeechRecognitionErrorInit : public EventInit {
    SpeechRecognitionErrorInit();

    String error;
    String message;
};

class SpeechRecognitionError : public Event {
public:
    enum ErrorCode {
        // FIXME: This is an unspecified error and Chromium should stop using it.
        ErrorCodeOther = 0,

        ErrorCodeNoSpeech = 1,
        ErrorCodeAborted = 2,
        ErrorCodeAudioCapture = 3,
        ErrorCodeNetwork = 4,
        ErrorCodeNotAllowed = 5,
        ErrorCodeServiceNotAllowed = 6,
        ErrorCodeBadGrammar = 7,
        ErrorCodeLanguageNotSupported = 8
    };

    static Ref<SpeechRecognitionError> create(ErrorCode, const String&);
    static PassRefPtr<SpeechRecognitionError> create();
    static PassRefPtr<SpeechRecognitionError> create(const AtomicString&, const SpeechRecognitionErrorInit&);

    const String& error() { return m_error; }
    const String& message() { return m_message; }
    virtual EventInterface eventInterface() const override;

private:
    
    SpeechRecognitionError(const String&, const String&);
    SpeechRecognitionError(const AtomicString&, const SpeechRecognitionErrorInit&);

    String m_error;
    String m_message;
};

} // namespace WebCore

#endif //ENABLE(SPEECH_RECOGNITION)

#endif // SpeechRecognitionError_h
