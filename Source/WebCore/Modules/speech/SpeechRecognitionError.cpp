
#include "config.h"

#if ENABLE(SPEECH_RECOGNITION)

#include "SpeechRecognitionError.h"

namespace WebCore {
static String ErrorCodeToString(SpeechRecognitionError::ErrorCode code)
{
    switch (code) {
    case SpeechRecognitionError::ErrorCodeOther:
        return "other";
    case SpeechRecognitionError::ErrorCodeNoSpeech:
        return "no-speech";
    case SpeechRecognitionError::ErrorCodeAborted:
        return "aborted";
    case SpeechRecognitionError::ErrorCodeAudioCapture:
        return "audio-capture";
    case SpeechRecognitionError::ErrorCodeNetwork:
        return "network";
    case SpeechRecognitionError::ErrorCodeNotAllowed:
        return "not-allowed";
    case SpeechRecognitionError::ErrorCodeServiceNotAllowed:
        return "service-not-allowed";
    case SpeechRecognitionError::ErrorCodeBadGrammar:
        return "bad-grammar";
    case SpeechRecognitionError::ErrorCodeLanguageNotSupported:
        return "language-not-supported";
    }

    ASSERT_NOT_REACHED();
    return String();
}

Ref<SpeechRecognitionError> SpeechRecognitionError::create(ErrorCode code, const String& message)
{
    return adoptRef(*new SpeechRecognitionError(ErrorCodeToString(code), message));
}

PassRefPtr<SpeechRecognitionError> SpeechRecognitionError::create()
{
    return adoptRef(new SpeechRecognitionError(emptyString(), emptyString()));
}

PassRefPtr<SpeechRecognitionError> SpeechRecognitionError::create(const AtomicString& eventName, const SpeechRecognitionErrorInit& initializer)
{
    return adoptRef(new SpeechRecognitionError(eventName, initializer));
}

EventInterface SpeechRecognitionError::eventInterface() const
{
    return (EventInterface) 0; /* SpeechRecognitionErrorEventInterfaceType; */
}


SpeechRecognitionError::SpeechRecognitionError(const String& error, const String& message)
    : Event(eventNames().errorEvent, /*canBubble=*/false, /*cancelable=*/false)
    , m_error(error)
    , m_message(message)
{
}

SpeechRecognitionError::SpeechRecognitionError(const AtomicString& eventName, const SpeechRecognitionErrorInit& initializer)
    : Event(eventName, initializer)
    , m_error(initializer.error)
    , m_message(initializer.message)
{
}


SpeechRecognitionErrorInit::SpeechRecognitionErrorInit()
{
}

} // namespace WebCore

#endif //ENABLE(SPEECH_RECOGNITION)
