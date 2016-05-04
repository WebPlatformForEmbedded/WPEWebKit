
#include "config.h"

#include "SpeechRecognition.h"

#if ENABLE(SPEECH_RECOGNITION)

#include "SpeechRecognition.h"

#include "Document.h"
#include "ExceptionCode.h"
#include "Page.h"
#include "SpeechRecognitionError.h"
#include "SpeechRecognitionEvent.h"

namespace WebCore {

PassRefPtr<SpeechRecognition> SpeechRecognition::create(ScriptExecutionContext& context)
{
    printf("%s:%s:%d\n\n",__FILE__, __func__, __LINE__ );

    RefPtr<SpeechRecognition> speechRecognition(adoptRef(new SpeechRecognition(context)));
    return speechRecognition.release();
}

void SpeechRecognition::start(ExceptionCode& exceptionCode)
{

    printf("%s:%s:%d  m_started = %d m_stopping=%d \n\n",__FILE__, __func__, __LINE__, m_started, m_stopping );
    if (m_started) {
        exceptionCode = INVALID_STATE_ERR;
        return;
    }
    printf("%s:%s:%d\n\n",__FILE__, __func__, __LINE__ );

    m_finalResults.clear();
    
    if (!m_platformSpeechRecognizer)
        m_platformSpeechRecognizer = std::make_unique<PlatformSpeechRecognizer>(this);
   
    printf("%s:%s:%d  m_started = %d m_stopping=%d m_continuous=%d \n\n",__FILE__, __func__, __LINE__, m_started, m_stopping, m_continuous );
    m_platformSpeechRecognizer->setContinuous(m_continuous); 
    m_platformSpeechRecognizer->setInterimResults(m_interimResults); 
    m_platformSpeechRecognizer->start();

    m_stoppedByActiveDOMObject = false;
    m_started = true;
    m_stopping = false;
    printf("%s:%s:%d  m_started = %d m_stopping=%d \n\n",__FILE__, __func__, __LINE__, m_started, m_stopping );
}

void SpeechRecognition::stop()
{
    printf("%s:%s:%d  m_started = %d m_stopping=%d \n\n",__FILE__, __func__, __LINE__, m_started, m_stopping );

    if (m_started && !m_stopping) {
        printf("Inside stop check  %s:%s:%d\n\n",__FILE__, __func__, __LINE__ );
        m_stopping = true;
        m_started = false;
        m_platformSpeechRecognizer->stop();
    }
    printf("%s:%s:%d  m_started = %d m_stopping=%d \n\n",__FILE__, __func__, __LINE__, m_started, m_stopping );

}

void SpeechRecognition::abort()
{
    printf("%s:%s:%d  m_started = %d m_stopping=%d \n\n",__FILE__, __func__, __LINE__, m_started, m_stopping );
    if (m_started && !m_stopping) {
        m_stopping = true;
        m_started = false;

        printf("%s:%s:%d  m_started = %d m_stopping=%d \n\n",__FILE__, __func__, __LINE__, m_started, m_stopping );
        m_stoppedByActiveDOMObject = true;
        m_platformSpeechRecognizer->abort();
    }
    printf("%s:%s:%d  m_started = %d m_stopping=%d \n\n",__FILE__, __func__, __LINE__, m_started, m_stopping );
}

void SpeechRecognition::didStartAudio()
{
    printf("%s:%s:%d\n",__FILE__, __func__, __LINE__);
    dispatchEvent(Event::create(eventNames().audiostartEvent, /*canBubble=*/false, /*cancelable=*/false));
}

void SpeechRecognition::didStartSound()
{
    printf("%s:%s:%d\n",__FILE__, __func__, __LINE__);
    dispatchEvent(Event::create(eventNames().soundstartEvent, /*canBubble=*/false, /*cancelable=*/false));
}

void SpeechRecognition::didStartSpeech()
{
    printf("%s:%s:%d\n",__FILE__, __func__, __LINE__);
    dispatchEvent(Event::create(eventNames().speechstartEvent, /*canBubble=*/false, /*cancelable=*/false));
}

void SpeechRecognition::didEndSpeech()
{
    printf("%s:%s:%d\n",__FILE__, __func__, __LINE__);
    dispatchEvent(Event::create(eventNames().speechendEvent, /*canBubble=*/false, /*cancelable=*/false));
}

void SpeechRecognition::didEndSound()
{
    printf("%s:%s:%d\n",__FILE__, __func__, __LINE__);
    dispatchEvent(Event::create(eventNames().soundendEvent, /*canBubble=*/false, /*cancelable=*/false));
}

void SpeechRecognition::didEndAudio()
{
    printf("%s:%s:%d\n",__FILE__, __func__, __LINE__);
    dispatchEvent(Event::create(eventNames().audioendEvent, /*canBubble=*/false, /*cancelable=*/false));
}

void SpeechRecognition::didReceiveResults(const Vector<RefPtr<SpeechRecognitionResult> >& newFinalResults, 
                                          const Vector<RefPtr<SpeechRecognitionResult> >& currentInterimResults)
{
    if (!m_stoppedByActiveDOMObject) 
    {
	    printf("%s:%s:%d\n",__FILE__, __func__, __LINE__);
	    unsigned long resultIndex = m_finalResults.size();

	    for (size_t i = 0; i < newFinalResults.size(); ++i) {
		    m_finalResults.append(newFinalResults[i]);
	    }

	    Vector<RefPtr<SpeechRecognitionResult> > results = m_finalResults;
	    for (size_t i = 0; i < currentInterimResults.size(); ++i)
		    results.append(currentInterimResults[i]);

	    printf("%s:%s:%d\n",__FILE__, __func__, __LINE__);
	    dispatchEvent(SpeechRecognitionEvent::createResult(resultIndex, results));
    } 
}

void SpeechRecognition::didReceiveNoMatch(PassRefPtr<SpeechRecognitionResult> result)
{
    dispatchEvent(SpeechRecognitionEvent::createNoMatch(result));
}

void SpeechRecognition::didReceiveError(SpeechRecognitionError& error)
{
    printf("%s:%s:%d  m_started = %d m_stopping=%d \n\n",__FILE__, __func__, __LINE__, m_started, m_stopping );
    dispatchEvent(error);
    m_started = false;
}

void SpeechRecognition::didStart()
{
    printf("%s:%s:%d\n",__FILE__, __func__, __LINE__);
    dispatchEvent(Event::create(eventNames().startEvent, false, false));
}

void SpeechRecognition::didEnd()
{
    printf("%s:%s:%d\n",__FILE__, __func__, __LINE__);
    m_started = false;
    m_stopping = false;
    if (!m_stoppedByActiveDOMObject) {
        dispatchEvent(Event::create(eventNames().endEvent, false, false));
        m_stoppedByActiveDOMObject = true;
    }
}

EventTargetInterface SpeechRecognition::eventTargetInterface() const
{
    return SpeechRecognitionEventTargetInterfaceType;
}

ScriptExecutionContext* SpeechRecognition::scriptExecutionContext() const
{
    return ActiveDOMObject::scriptExecutionContext();
}

bool SpeechRecognition::hasPendingActivity() const
{
    return m_started;
}

SpeechRecognition::SpeechRecognition(ScriptExecutionContext& context)
    : ActiveDOMObject(&context)
    , m_grammars(SpeechGrammarList::create()) // FIXME: The spec is not clear on the default value for the grammars attribute.
    , m_continuous(false)
    , m_interimResults(false)
    , m_maxAlternatives(1)
    , m_stoppedByActiveDOMObject(false)
    , m_started(false)
    , m_stopping(false)
{

    printf("%s:%s:%d\n\n",__FILE__, __func__, __LINE__ );

    // FIXME: Need to hook up with Page to get notified when the visibility changes.
}

} // namespace WebCore
#endif //ENABLE(SPEECH_RECOGNITION)

