
#include "config.h"
#include "DOMWindowSpeechRecognition.h"

#if ENABLE(SPEECH_RECOGNITION)

#include "DOMWindow.h"
#include <wtf/PassRefPtr.h>

namespace WebCore {
    
DOMWindowSpeechRecognition::DOMWindowSpeechRecognition(DOMWindow* window)
    : DOMWindowProperty(window->frame())
{
}

DOMWindowSpeechRecognition::~DOMWindowSpeechRecognition()
{
}

const char* DOMWindowSpeechRecognition::supplementName()
{
    return "DOMWindowSpeechRecognition";
}

// static
DOMWindowSpeechRecognition* DOMWindowSpeechRecognition::from(DOMWindow* window)
{
    DOMWindowSpeechRecognition* supplement = static_cast<DOMWindowSpeechRecognition*>(Supplement<DOMWindow>::from(window, supplementName()));
    if (!supplement) {
        auto newSupplement = std::make_unique<DOMWindowSpeechRecognition>(window);
        supplement = newSupplement.get();
        provideTo(window, supplementName(), WTFMove(newSupplement));
    }
    return supplement;
}

// static
SpeechRecognition* DOMWindowSpeechRecognition::speechRecognition(DOMWindow& window)
{
    LOG(SpeechRecognition, "%s:%s:%d\n", __FILE__, __func__, __LINE__);
    return DOMWindowSpeechRecognition::from(&window)->speechRecognition(window.scriptExecutionContext());
}

SpeechRecognition* DOMWindowSpeechRecognition::speechRecognition(ScriptExecutionContext* context)
{
    LOG(SpeechRecognition, "%s:%s:%d\n", __FILE__, __func__, __LINE__);

    if (!m_speechRecognition && frame())
        m_speechRecognition = SpeechRecognition::create(*context);
    return m_speechRecognition.get();
}
    
} // namespace WebCore

#endif // ENABLE(SPEECH_RECOGNITION)
