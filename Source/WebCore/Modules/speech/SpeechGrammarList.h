
#ifndef SpeechGrammarList_h
#define SpeechGrammarList_h

#if ENABLE(SPEECH_RECOGNITION)

//#include "ScriptWrappable.h"
#include "SpeechGrammar.h"
#include "wtf/RefCounted.h"
#include "wtf/Vector.h"

namespace WebCore {

class ScriptExecutionContext;

class SpeechGrammarList : public RefCounted<SpeechGrammarList> {
public:
    static PassRefPtr<SpeechGrammarList> create();

    unsigned long length() const { return m_grammars.size(); }
    SpeechGrammar* item(unsigned long) const;

    void addFromUri(ScriptExecutionContext&, const String& src, double weight = 1.0);
    void addFromString(const String&, double weight = 1.0);

private:
    SpeechGrammarList();

    Vector<RefPtr<SpeechGrammar> > m_grammars;
};

} // namespace WebCore

#endif // ENABLE(SPEECH_RECOGNITION)

#endif // SpeechGrammarList_h
