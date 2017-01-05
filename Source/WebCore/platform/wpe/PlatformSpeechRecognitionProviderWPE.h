#ifndef PlatformSpeechRecognitionProviderWPE_h
#define PlatformSpeechRecognitionProviderWPE_h

#if ENABLE(SPEECH_RECOGNITION)

#include <Logging.h>
#include <wtf/PassRefPtr.h>
#include <wtf/Vector.h>
#include <wtf/text/WTFString.h>
#include <wtf/Deque.h>
#include <wtf/threads/BinarySemaphore.h>

extern "C" {
#include <sphinxbase/err.h>
#include <sphinxbase/ad.h>
#include <sphinxbase/cmd_ln.h>
#include <pocketsphinx.h>
}

#define MAX_SILENCE_PERIOD 2
#define INTRIM_INTERVAL    6

typedef struct SpeechHyp {
    bool        final;
    int32       confidence;
    const char* hyp;
} SpeechHyp;

namespace WebCore {

class PlatformSpeechRecognizer;
class PlatformSpeechRecognitionUtterance;
class PlatformSpeechRecognitionVoice;

class PlatformSpeechRecognitionProviderWPE {
public:

    explicit PlatformSpeechRecognitionProviderWPE(PlatformSpeechRecognizer*);
    ~PlatformSpeechRecognitionProviderWPE();

    void start();
    void stop();
    void abort();
    void setContinuous(bool continuous) { m_continuous = continuous;}
    void setInterimResults(bool interimResults) {m_interimResults = interimResults;}

private:
    enum SpeechEvent {
        Start,
        StartAudio,
        StartSound,
        StartSpeech,
        End,
        EndAudio,
        EndSound,
        EndSpeech,
        ReceiveResults,
        ReceiveNoMatch,
        ReceiveError
    };
    
    enum RecognitionStatus {
        RecognitionStarted,
        RecognitionStopped,
        RecognitionAborted
    };
    enum FireEventStatus {
        FireEventStarted,
        FireEventStopped,
    };


    int  initSpeechRecognition();
    void deinitSpeechRecognition();
    void recognizeFromDevice();
    
    /* pocketsphinx specific */
    cmd_ln_t       *m_config;
    ad_rec_t       *m_audioDevice;
    ps_decoder_t   *m_recognizer;

    bool              m_continuous;
    bool              m_finalResults;
    bool              m_interimResults;

    BinarySemaphore   m_waitForEvents;
    FireEventStatus   m_fireEventStatus;
    ThreadIdentifier  m_fireEventThread;
    static void       fireEventThread(void*);
    void              fireSpeechEvent(SpeechEvent);
    void              stopFireEventThread();

    ThreadIdentifier  m_readThread;
    ThreadIdentifier  m_recognitionThread;
    RecognitionStatus m_recognitionStatus;
    static void       readThread(void*);
    static void       recognitionThread (void*);
    
          
    Vector <SpeechHyp*>   m_speechHypQueue;
    Vector <SpeechEvent> m_speechEventQueue;
    Vector <std::pair<int16*, int32>> m_speechInputQueue;
    Vector <std::pair<SpeechRecognitionError::ErrorCode, const char*>> m_speechErrorQueue;
    void clearSpeechQueue();
 
    PlatformSpeechRecognizer* m_platformSpeechRecognizer;
};

} // namespace WebCore

#endif // ENABLE(SPEECH_RECOGNITION)

#endif // PlatformSpeechRecognitionProviderWPE_h
