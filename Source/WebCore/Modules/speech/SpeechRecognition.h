
#ifndef SpeechRecognition_h
#define SpeechRecognition_h

#if ENABLE(SPEECH_RECOGNITION)

#include "PlatformSpeechRecognizer.h"
#include "ActiveDOMObject.h"
#include "EventTarget.h"
#include "SpeechGrammarList.h"
#include "wtf/Compiler.h"
#include "wtf/PassRefPtr.h"
#include "wtf/RefCounted.h"
#include "wtf/text/WTFString.h"

namespace WebCore {

class ScriptExecutionContext;

class SpeechRecognition : public PlatformSpeechRecognizerClient, public RefCounted<SpeechRecognition>, public ActiveDOMObject, public EventTarget {
public:
    static PassRefPtr<SpeechRecognition> create(ScriptExecutionContext&);
    virtual ~SpeechRecognition() {}

    // Attributes.
    PassRefPtr<SpeechGrammarList> grammars() { return m_grammars; }
    void setGrammars(PassRefPtr<SpeechGrammarList> grammars) { m_grammars = grammars; }
    String lang() { return m_lang; }
    void setLang(const String& lang) { m_lang = lang; }
    bool continuous() { return m_continuous; }
    void setContinuous(bool continuous) { m_continuous = continuous; }
    bool interimResults() { return m_interimResults; }
    void setInterimResults(bool interimResults) { m_interimResults = interimResults; }
    unsigned long maxAlternatives() { return m_maxAlternatives; }
    void setMaxAlternatives(unsigned long maxAlternatives) { m_maxAlternatives = maxAlternatives; }

    // Callable by the user.
    void start(ExceptionCode&);
    virtual void stop() override;
    void abort();

    // Called by the SpeechRecognizer.
    void didStartAudio() override;
    void didStartSound() override;
    void didStartSpeech() override;
    void didEndSpeech() override;
    void didEndSound() override;
    void didEndAudio() override;
    void didReceiveResults(const Vector<RefPtr<SpeechRecognitionResult> >& newFinalResults, const Vector<RefPtr<SpeechRecognitionResult> >& currentInterimResults) override;
    void didReceiveNoMatch(PassRefPtr<SpeechRecognitionResult>) override;
    void didReceiveError(SpeechRecognitionError&) override;
    void didStart() override;
    void didEnd() override;


    // EventTarget.
    virtual EventTargetInterface eventTargetInterface() const override;
    virtual ScriptExecutionContext* scriptExecutionContext() const override;
 
    // ActiveDOMObject.
    //virtual 
    bool hasPendingActivity() const;

    virtual const char* activeDOMObjectName() const override  { return 0; }
    virtual bool canSuspendForDocumentSuspension() const override {return false;}
   
    using RefCounted<SpeechRecognition>::ref;
    using RefCounted<SpeechRecognition>::deref;

private:
    friend class RefCounted<SpeechRecognition>;
    SpeechRecognition(ScriptExecutionContext&);

    // EventTarget
    virtual void refEventTarget() override { ref(); }
    virtual void derefEventTarget() override { deref(); }
    virtual EventTargetData* eventTargetData() override { return &m_eventTargetData; }
    virtual EventTargetData& ensureEventTargetData() override { return m_eventTargetData; }

    RefPtr<SpeechGrammarList> m_grammars;
    String m_lang;
    bool m_continuous;
    bool m_interimResults;
    unsigned long m_maxAlternatives;

    EventTargetData m_eventTargetData;
    
    std::unique_ptr<PlatformSpeechRecognizer> m_platformSpeechRecognizer;
    bool m_stoppedByActiveDOMObject;
    bool m_started;
    bool m_stopping;
    AtomicString m_startEvent;
    Vector<RefPtr<SpeechRecognitionResult> > m_finalResults;

};

} // namespace WebCore

#endif //ENABLE(SPEECH_RECOGNITION)

#endif // SpeechRecognition_h
