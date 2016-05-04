
#include "config.h"

#if ENABLE(SPEECH_RECOGNITION)

#include "Document.h"
#include "SpeechGrammarList.h"

namespace WebCore {

PassRefPtr<SpeechGrammarList> SpeechGrammarList::create()
{
    return adoptRef(new SpeechGrammarList);
}

SpeechGrammar* SpeechGrammarList::item(unsigned long index) const
{
    if (index >= m_grammars.size())
        return 0;

    return m_grammars[index].get();
}

void SpeechGrammarList::addFromUri(ScriptExecutionContext& executionContext, const String& src, double weight)
{
//    Document* document =  downcast<Document>(&executionContext);
//    m_grammars.append(SpeechGrammar::create(document->completeURL(src), weight));
}

void SpeechGrammarList::addFromString(const String& string, double weight)
{
    String urlString = String("data:application/xml,") + encodeWithURLEscapeSequences(string);
    m_grammars.append(SpeechGrammar::create(URL(URL(), urlString), weight));
}

SpeechGrammarList::SpeechGrammarList()
{
//    ScriptWrappable::init(this);
}

} // namespace WebCore

#endif //ENABLE(SPEECH_RECOGNITION)
