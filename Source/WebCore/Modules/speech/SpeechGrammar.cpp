
#include "config.h"

#if ENABLE(SPEECH_RECOGNITION)

#include "Document.h"
#include "SpeechGrammar.h"

namespace WebCore {

PassRefPtr<SpeechGrammar> SpeechGrammar::create()
{
    return adoptRef(new SpeechGrammar);
}

PassRefPtr<SpeechGrammar> SpeechGrammar::create(const URL& src, double weight)
{
    return adoptRef(new SpeechGrammar(src, weight));
}

void SpeechGrammar::setSrc(ScriptExecutionContext& executionContext, const String& src)
{
//    Document* document = downcast<Document>(&executionContext);
//    m_src = document->completeURL(src);
}

SpeechGrammar::SpeechGrammar()
    : m_weight(1.0)
{
//    ScriptWrappable::init(this);
}

SpeechGrammar::SpeechGrammar(const URL& src, double weight)
    : m_src(src)
    , m_weight(weight)
{
  //  ScriptWrappable::init(this);
}

} // namespace WebCore

#endif //ENABLE(SPEECH_RECOGNITION)
