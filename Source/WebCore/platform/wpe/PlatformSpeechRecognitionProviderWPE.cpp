#include "config.h"

#if ENABLE(SPEECH_RECOGNITION)

#include <wtf/text/CString.h>
#include <unistd.h>
#include <wtf/MainThread.h>

#include <PlatformSpeechRecognizer.h>
#include "PlatformSpeechRecognitionProviderWPE.h"

#define FireErrorEvent(context, event, errCode, errMessage)                        \
    do {                                                                           \
        context->m_speechErrorQueue.append (std::make_pair(errCode, errMessage));  \
        context->m_speechEventQueue.append (std::make_pair(event, ""));            \
        context->m_waitForEvents.signal();                                         \
    } while(0);

#define FireSpeechEvent(context, event)         \
    context->m_speechEventQueue.append (event); \
    context->m_waitForEvents.signal();


static const arg_t contArgsDef[] = {
    POCKETSPHINX_OPTIONS,
    /* Argument file. */
    {"-argfile",
     ARG_STRING,
     NULL,
     "Argument file giving extra arguments."},
    {"-adcdev",
     ARG_STRING,
     NULL,
     "Name of audio device to use for input."},
    {"-time",
     ARG_BOOLEAN,
     "no",
     "Print word times in file transcription."},
    CMDLN_EMPTY_OPTION
};

namespace WebCore {

PlatformSpeechRecognitionProviderWPE::PlatformSpeechRecognitionProviderWPE(PlatformSpeechRecognizer* client)
    : m_continuous(false)
    , m_finalResults(false)
    , m_interimResults(false)
    , m_fireEventThread(0)
    , m_readThread(0)
    , m_recognitionThread(0)
    , m_platformSpeechRecognizer(client)

{
    printf ("%s:%s:%d \n",__FILE__, __func__, __LINE__);
    initSpeechRecognition();

    m_fireEventStatus = FireEventStarted;

    printf("%s:%s:%d \n", __FILE__, __func__, __LINE__);
    if (!(m_fireEventThread = createThread(fireEventThread, this, "WebCore: SpeechEventFiringThread"))) {
        printf("Error in creating Speech Event Firing Thread\n");
    }     
}

PlatformSpeechRecognitionProviderWPE::~PlatformSpeechRecognitionProviderWPE()
{
    deinitSpeechRecognition();

    if (m_fireEventThread) {
        m_fireEventStatus = FireEventStopped;
        m_waitForEvents.signal();
        waitForThreadCompletion(m_fireEventThread);   
        m_fireEventThread = 0;
    }
}

int PlatformSpeechRecognitionProviderWPE::initSpeechRecognition()
{
    /* Initialize PocketSphinx Recognizer module */ 
    m_config = cmd_ln_init(NULL, contArgsDef, FALSE, NULL, TRUE);

    printf("%s:%s:%d \n", __FILE__, __func__, __LINE__);
   
    ps_default_search_args(m_config);
    m_recognizer = ps_init(m_config);
    if (m_recognizer == NULL) {
        printf("Failed to create recognizer, hence exiting the thread\n");
        return -1;
    }

    /* Open Microphone device */
    if ((m_audioDevice = ad_open_dev("plughw:1", 16000)) == NULL) { //TODO: avoid device hardcoding
        printf("Failed to open audio device\n");
        return -1;
    }

    return 0;
}

void PlatformSpeechRecognitionProviderWPE::deinitSpeechRecognition()
{
    if (m_audioDevice) {
        ad_close(m_audioDevice);
    }
    if (m_recognizer) {
        free (m_recognizer);
    }
}

void PlatformSpeechRecognitionProviderWPE::recognizeFromDevice()
{
    if (!m_recognitionThread) {    
        m_recognitionStatus = RecognitionStarted;

        printf("%s:%s:%d \n", __FILE__, __func__, __LINE__);
        if (!(m_readThread = createThread(readThread, this, "WebCore: SpeechRecognitionReadThread"))) {
            printf("Error in creating Recognition Thread\n");
        }     

        if (!(m_recognitionThread = createThread(recognitionThread, this, "WebCore: SpeechRecognitionThread"))) {
            printf("Error in creating Recognition Thread\n");
            m_recognitionStatus = RecognitionStopped;
            m_readThread = 0;
        }     
    }
}     

void PlatformSpeechRecognitionProviderWPE::readThread (void* context)
{
    int32  adLen;
    int16 *adBuf;
 
    PlatformSpeechRecognitionProviderWPE *providerContext = (PlatformSpeechRecognitionProviderWPE*) context;
    if (ad_start_rec( providerContext->m_audioDevice) < 0) {
        printf("Failed to start recording\n");
        FireErrorEvent(providerContext, ReceiveError, SpeechRecognitionError::ErrorCodeAudioCapture, "Failed to start recording");
        return;
    }
    printf("%s:%s:%d \n", __FILE__, __func__, __LINE__);

    FireSpeechEvent(providerContext, std::make_pair(Start, ""));
    FireSpeechEvent(providerContext, std::make_pair(StartAudio,""));

    adBuf = new int16[2048]();
    
    while (providerContext->m_recognitionStatus == RecognitionStarted) {
        if ((adLen = ad_read(providerContext->m_audioDevice, adBuf, 2048)) < 0) {
            printf("Failed to read audio");
            FireErrorEvent(providerContext, ReceiveError, SpeechRecognitionError::ErrorCodeAudioCapture, "Failed to read audio");
        }
        
        if (providerContext->m_recognitionStatus == RecognitionAborted)
            break;

        if (!adLen)
            continue;

        providerContext->m_speechInputQueue.append(std::make_pair(adBuf, adLen));
        adBuf = new int16[2048]();
    }

    delete[] adBuf; adBuf = NULL;
    
    printf("%s:%s:%d quesize = %d \n", __FILE__, __func__, __LINE__, providerContext->m_speechInputQueue.size());
    if (ad_stop_rec( providerContext->m_audioDevice) < 0) {
        printf("Failed to stop recording\n");
        FireErrorEvent(providerContext, ReceiveError, SpeechRecognitionError::ErrorCodeAudioCapture, "Failed to stop recording");
    }

    providerContext->m_readThread = 0; 

    return;
}

void PlatformSpeechRecognitionProviderWPE::recognitionThread (void* context)
{
    int16 *adBuf;
    int32  adLen = 0;
    char const *text;
    uint8 uttStarted, inSpeech;
    PlatformSpeechRecognitionProviderWPE *providerContext = (PlatformSpeechRecognitionProviderWPE*) context;
   
    printf("%s:%s:%d \n", __FILE__, __func__, __LINE__);

    if (ps_start_utt(providerContext->m_recognizer) < 0) {
        printf("Failed to start utterance\n");
        FireErrorEvent(providerContext, ReceiveError, SpeechRecognitionError::ErrorCodeAudioCapture, "Failed to start utterance");
    }

    printf("%s:%s:%d \n", __FILE__, __func__, __LINE__);
    uttStarted = FALSE;
    printf("Ready....\n");

    FireSpeechEvent(providerContext, std::make_pair(StartSound,""));

    while (providerContext->m_recognitionStatus == RecognitionStarted) { 
        while (providerContext->m_speechInputQueue.size() >= 1) {
            adBuf = providerContext->m_speechInputQueue[0].first;
            adLen = providerContext->m_speechInputQueue[0].second;
                        
            ps_process_raw(providerContext->m_recognizer, adBuf, adLen, false, false);
            
            //remove from vector and delete audio buffer
            providerContext->m_speechInputQueue.removeFirst(providerContext->m_speechInputQueue[0]);
            delete[] adBuf; adBuf = NULL;

            inSpeech = ps_get_in_speech(providerContext->m_recognizer);

            if (providerContext->m_recognitionStatus == RecognitionAborted)
                break;

            if (inSpeech && !uttStarted) {
                printf("%s:%s:%d  in_speech = %d utt_started=%d \n",__FILE__, __func__, __LINE__, inSpeech, uttStarted);
                uttStarted = true;
                printf("Listening...\n");

                FireSpeechEvent(providerContext, std::make_pair(StartSpeech,""));
            }

            if (providerContext->m_recognitionStatus == RecognitionAborted)
                break; 

            if (uttStarted) { //interim speech
                 text = ps_get_hyp(providerContext->m_recognizer, NULL );
                 if (text != NULL) {
                    printf("Interim result %s\n", text);
                    FireSpeechEvent(providerContext, std::make_pair(ReceiveResults, text));
                    printf("%s:%s:%d quesize = %d \n", __FILE__, __func__, __LINE__, providerContext->m_speechInputQueue.size());
                    fflush(stdout);
                }
            }

            if (providerContext->m_recognitionStatus == RecognitionAborted)
                break; 

            if (!inSpeech && uttStarted) {
                printf("%s:%s:%d\n",__FILE__, __func__, __LINE__);
                /* speech -> silence transition, time to start new utterance  */
                ps_end_utt(providerContext->m_recognizer);

                FireSpeechEvent(providerContext, std::make_pair(EndSpeech,""));
                FireSpeechEvent(providerContext, std::make_pair(EndSound,""));

                text = ps_get_hyp(providerContext->m_recognizer, NULL );
                if (providerContext->m_recognitionStatus == RecognitionAborted)
                    break;

                if (text != NULL) {
                    printf("%s\n", text);
                    // Set flag to indicate final result
                    providerContext->m_finalResults = true; 

                    FireSpeechEvent(providerContext, std::make_pair(ReceiveResults, text));
                    printf("%s:%s:%d\n",__FILE__, __func__, __LINE__);
                    fflush(stdout);
                }
                printf("%s:%s:%d\n",__FILE__, __func__, __LINE__);

                if (!providerContext->m_continuous)
                    goto EndSpeech;

                printf("%s:%s:%d\n",__FILE__, __func__, __LINE__);
                fflush(stdout);

                if (ps_start_utt(providerContext->m_recognizer) < 0) {
                    printf("Failed to start utterance\n");
                    FireErrorEvent(providerContext, ReceiveError, SpeechRecognitionError::ErrorCodeAudioCapture, "Failed to start utterance");
                }
                uttStarted = FALSE;
                printf("Ready....\n");

                fflush(stdout);
                FireSpeechEvent(providerContext, std::make_pair(StartSound,""));    
            }
        }
       //usleep(100 * 1000);
    } 

    ps_end_utt(providerContext->m_recognizer);

EndSpeech:
    FireSpeechEvent(providerContext, std::make_pair(EndAudio,""));
    FireSpeechEvent(providerContext, std::make_pair(End,""));

    providerContext->m_recognitionThread = 0; 
    providerContext->clearSpeechQueue(); 
    printf("%s:%s:%d\n\n",__FILE__, __func__, __LINE__ );
    return;
}

void PlatformSpeechRecognitionProviderWPE::clearSpeechQueue()
{
    int16 *adBuf;
    while (m_speechInputQueue.size() >= 1) {
        adBuf = m_speechInputQueue[0].first;

        //remove from vector and delete audio buffer
        m_speechInputQueue.removeFirst(m_speechInputQueue[0]);
        delete[] adBuf; adBuf = NULL;
        printf("%s:%s:%d quesize = %d \n", __FILE__, __func__, __LINE__, m_speechInputQueue.size());
    }    
}

void PlatformSpeechRecognitionProviderWPE::start()
{
    printf("%s:%s:%d \n", __FILE__, __func__, __LINE__);
    recognizeFromDevice();
}

void PlatformSpeechRecognitionProviderWPE::abort()
{
    printf("%s:%s:%d \n", __FILE__, __func__, __LINE__);
    if (m_recognitionThread) {
    printf("%s:%s:%d \n", __FILE__, __func__, __LINE__);
        m_recognitionStatus = RecognitionAborted;
        waitForThreadCompletion (m_readThread);
        waitForThreadCompletion (m_recognitionThread);   
        m_recognitionThread = m_readThread = 0;    
    }
}

void PlatformSpeechRecognitionProviderWPE::stop()
{
    printf("%s:%s:%d \n", __FILE__, __func__, __LINE__);
    if (m_recognitionThread) {
        printf("%s:%s:%d \n", __FILE__, __func__, __LINE__);
        m_recognitionStatus = RecognitionStopped;
    }
}

void PlatformSpeechRecognitionProviderWPE::fireEventThread(void* context)
{
    PlatformSpeechRecognitionProviderWPE *providerContext = (PlatformSpeechRecognitionProviderWPE*) context;

    printf("%s:%s:%d \n",__FILE__, __func__, __LINE__);
    while (providerContext->m_fireEventStatus == FireEventStarted) {
        while (providerContext->m_waitForEvents.wait(0)) {
            for (const auto& firedEvent: providerContext->m_speechEventQueue) {
                callOnMainThread([providerContext, firedEvent] { 
                    providerContext->fireSpeechEvent(firedEvent);  
                });
                providerContext->m_speechEventQueue.removeFirst(firedEvent);
            }
        }
    }
    return;
}

void PlatformSpeechRecognitionProviderWPE::fireSpeechEvent(std::pair<SpeechEvent, const char*> firedEvent) 
{
    SpeechEvent speechEvent = firedEvent.first;
     
    switch (speechEvent) {
    case Start:
        printf("%s:%s:%d \n",__FILE__, __func__, __LINE__);
        m_platformSpeechRecognizer->client()->didStart();
        break;
    case StartAudio:
        printf("%s:%s:%d \n",__FILE__, __func__, __LINE__);
        m_platformSpeechRecognizer->client()->didStartAudio();
        break;
    case StartSound:
        printf("%s:%s:%d \n",__FILE__, __func__, __LINE__);
        m_platformSpeechRecognizer->client()->didStartSound();
        break;
    case StartSpeech:
        printf("%s:%s:%d \n",__FILE__, __func__, __LINE__);
        m_platformSpeechRecognizer->client()->didStartSpeech();
        break;
    case End:
        printf("%s:%s:%d \n",__FILE__, __func__, __LINE__);
        m_recognitionStatus = RecognitionStopped;
        m_platformSpeechRecognizer->client()->didEnd();
        m_recognitionThread = 0;
        break;
    case EndAudio:
        printf("%s:%s:%d \n",__FILE__, __func__, __LINE__);
        m_platformSpeechRecognizer->client()->didEndAudio();
        break;
    case EndSound:
        printf("%s:%s:%d \n",__FILE__, __func__, __LINE__);
        m_platformSpeechRecognizer->client()->didEndSound();
        break;
    case EndSpeech:
        printf("%s:%s:%d \n",__FILE__, __func__, __LINE__);
        m_platformSpeechRecognizer->client()->didEndSpeech();
        break;
    case ReceiveResults: {
        printf("%s:%s:%d \n",__FILE__, __func__, __LINE__);
        String transcripts (firedEvent.second);
        double confidence;
        printf("%s:%s:%d \n",__FILE__, __func__, __LINE__);
        if (m_recognitionStatus == RecognitionAborted)
                break;

        Vector<RefPtr<SpeechRecognitionResult>> finalResults;
        Vector<RefPtr<SpeechRecognitionResult>> interimResults;
        Vector<RefPtr<SpeechRecognitionAlternative> > alternatives;
        alternatives.append(SpeechRecognitionAlternative::create(transcripts, confidence));

        if (m_interimResults) {
            printf("%s:%s:%d \n",__FILE__, __func__, __LINE__);
            interimResults.append(SpeechRecognitionResult::create(alternatives, false)); 
        }
        if (m_finalResults) {
            printf("%s:%s:%d \n",__FILE__, __func__, __LINE__);
            finalResults.append(SpeechRecognitionResult::create(alternatives, m_finalResults)); 
            m_finalResults = false;
        }   
        printf("%s:%s:%d \n",__FILE__, __func__, __LINE__);
        m_platformSpeechRecognizer->client()->didReceiveResults(finalResults, interimResults);
        printf("%s:%s:%d \n",__FILE__, __func__, __LINE__);
        break;
    }
    case ReceiveNoMatch:
        printf("%s:%s:%d \n",__FILE__, __func__, __LINE__);
//        m_platformSpeechRecognizer->client()->didReceiveNoMatch();
        break;
    case ReceiveError:
        printf("%s:%s:%d \n",__FILE__, __func__, __LINE__);
        if (m_speechErrorQueue.size() >= 1) {
             String errMessage (m_speechErrorQueue[0].second);
             m_platformSpeechRecognizer->client()->didReceiveError(SpeechRecognitionError::create(m_speechErrorQueue[0].first, errMessage));
             m_speechErrorQueue.removeFirst(m_speechErrorQueue[0]);
        }
        break;
    default:
        printf("%s:%s:%d invalid event %d\n",__FILE__, __func__, __LINE__, speechEvent);
        break;
    }
}

} // namespace WebCore

#endif
