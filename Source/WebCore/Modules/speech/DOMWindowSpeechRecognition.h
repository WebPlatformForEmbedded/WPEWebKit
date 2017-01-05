
#ifndef DOMWindowSpeechRecognition_h
#define DOMWindowSpeechRecognition_h

#if ENABLE(SPEECH_RECOGNITION)

#include "DOMWindowProperty.h"
#include "Logging.h"
#include "Frame.h"
#include "SpeechRecognition.h"
#include "Supplementable.h"

namespace WebCore {
    
class DOMWindow;

class DOMWindowSpeechRecognition : public Supplement<DOMWindow>, public DOMWindowProperty {
public:
    explicit DOMWindowSpeechRecognition(DOMWindow*);
    virtual ~DOMWindowSpeechRecognition();
    
    WEBCORE_EXPORT static SpeechRecognition* speechRecognition(DOMWindow&);
    static DOMWindowSpeechRecognition* from(DOMWindow*);
    
private:
    SpeechRecognition* speechRecognition(ScriptExecutionContext*);
    static const char* supplementName();
    
    RefPtr<SpeechRecognition> m_speechRecognition;
};

} // namespace WebCore

#endif // ENABLE(SPEECH_RECOGNITION)

#endif // DOMWindowSpeechRecognition_h
