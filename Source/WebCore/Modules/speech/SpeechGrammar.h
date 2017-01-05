
#ifndef SpeechGrammar_h
#define SpeechGrammar_h

#if  ENABLE(SPEECH_RECOGNITION)

#include "URL.h"
#include "wtf/RefCounted.h"
#include "wtf/text/WTFString.h"

namespace WebCore {

class ScriptExecutionContext;

class SpeechGrammar : public RefCounted<SpeechGrammar> {
public:
    static PassRefPtr<SpeechGrammar> create(); // FIXME: The spec is not clear on what the constructor should look like.
    static PassRefPtr<SpeechGrammar> create(const URL& src, double weight);

    const URL& src(ScriptExecutionContext&) const { return m_src; }
    const URL& src() const { return m_src; }
    void setSrc(ScriptExecutionContext&, const String& src);

    double weight() const { return m_weight; }
    void setWeight(double weight) { m_weight = weight; }

private:
    SpeechGrammar();
    SpeechGrammar(const URL& src, double weight);

    URL m_src;
    double m_weight;
};

} // namespace WebCore

#endif // ENABLE(SPEECH_RECOGNITION)

#endif // SpeechGrammar_h
