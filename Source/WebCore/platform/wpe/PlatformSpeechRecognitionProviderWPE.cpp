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
        context->m_speechEventQueue.append (event);     \
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
    , m_interimResults(false)
    , m_fireEventThread(0)
    , m_readThread(0)
    , m_recognitionThread(0)
    , m_platformSpeechRecognizer(client)

{
    LOG(SpeechRecognition, "%s:%s:%d \n", __FILE__, __func__, __LINE__);

    initSpeechRecognition();

    m_fireEventStatus = FireEventStarted;

    LOG(SpeechRecognition, "%s:%s:%d \n", __FILE__, __func__, __LINE__);
    if (!(m_fireEventThread = createThread(fireEventThread, this, 
                                           "WebCore: SpeechEventFiringThread"))) {
        LOG(SpeechRecognition, "Error in creating Speech Event Firing Thread\n");
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
    m_config = cmd_ln_init(NULL, contArgsDef, TRUE,  
                           "-topn", "2",
                           "-maxwpf", "5",
                           "-maxhmmpf", "3000",
                           "-pl_window", "4", 
                           NULL);

    LOG(SpeechRecognition, "%s:%s:%d \n", __FILE__, __func__, __LINE__);
   
    ps_default_search_args(m_config);
    m_recognizer = ps_init(m_config);
    if (m_recognizer == NULL) {
        LOG(SpeechRecognition, "Failed to create recognizer, hence exiting the thread\n");
        return -1;
    }

    /* Open Microphone device */
    if ((m_audioDevice = ad_open_dev("plughw:1", 16000)) == NULL) { //TODO: avoid device hardcoding
        LOG(SpeechRecognition, "Failed to open audio device\n");
        m_audioDevice = 0;
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

        LOG(SpeechRecognition, "%s:%s:%d \n", __FILE__, __func__, __LINE__);
        if (!(m_readThread = createThread(readThread, this, 
                                          "WebCore: SpeechRecognitionReadThread"))) {
            LOG(SpeechRecognition, "Error in creating Recognition Thread\n");
        }     

        if (!(m_recognitionThread = createThread(recognitionThread, this, 
                                                 "WebCore: SpeechRecognitionThread"))) {
            LOG(SpeechRecognition, "Error in creating Recognition Thread\n");
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
        LOG(SpeechRecognition, "Failed to start recording\n");
        FireErrorEvent(providerContext, ReceiveError, 
                       SpeechRecognitionError::ErrorCodeAudioCapture, 
                       "Failed to start recording");
        return;
    }
    LOG(SpeechRecognition, "%s:%s:%d \n", __FILE__, __func__, __LINE__);

    FireSpeechEvent(providerContext, Start);
    FireSpeechEvent(providerContext, StartAudio);

    adBuf = new int16[4096]();
    
    while (providerContext->m_recognitionStatus == RecognitionStarted) {
        if ((adLen = ad_read(providerContext->m_audioDevice, adBuf, 4096)) < 0) {
            LOG(SpeechRecognition, "Failed to read audio");

            FireErrorEvent(providerContext, ReceiveError, 
                           SpeechRecognitionError::ErrorCodeAudioCapture, 
                           "Failed to read audio");
        }
        
        if (providerContext->m_recognitionStatus == RecognitionAborted)
            break;

        if (!adLen)
            continue;

        providerContext->m_speechInputQueue.append(std::make_pair(adBuf, adLen));
        adBuf = new int16[4096]();
    }

    delete[] adBuf; adBuf = NULL;
    
    LOG(SpeechRecognition, "%s:%s:%d quesize = %d \n", 
                            __FILE__, __func__, __LINE__, 
                            providerContext->m_speechInputQueue.size());
 
    if (ad_stop_rec( providerContext->m_audioDevice) < 0) {
        LOG(SpeechRecognition, "Failed to stop recording\n");
        FireErrorEvent(providerContext, ReceiveError, 
                       SpeechRecognitionError::ErrorCodeAudioCapture, 
                       "Failed to stop recording");
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
    uint8 silenceCnt = 0, intrimCnt = 0;
    SpeechHyp *speechHyp;
    PlatformSpeechRecognitionProviderWPE *providerContext = (PlatformSpeechRecognitionProviderWPE*) context;
   
    LOG(SpeechRecognition, "%s:%s:%d \n", __FILE__, __func__, __LINE__);

    if (ps_start_utt(providerContext->m_recognizer) < 0) {
        LOG(SpeechRecognition, "Failed to start utterance\n");
        FireErrorEvent(providerContext, ReceiveError, 
                       SpeechRecognitionError::ErrorCodeAudioCapture, 
                       "Failed to start utterance");
    }

    LOG(SpeechRecognition, "%s:%s:%d \n", __FILE__, __func__, __LINE__);
    uttStarted = FALSE;
    LOG(SpeechRecognition, "Ready....\n");
   
    FireSpeechEvent(providerContext, StartSound);

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
            LOG(SpeechRecognition, "%s:%s:%d quesize = %d len =%d intrimCnt = %d\n", 
                                           __FILE__, __func__, __LINE__, providerContext->m_speechInputQueue.size(), adLen, intrimCnt);

            if (inSpeech && !uttStarted) {
                LOG(SpeechRecognition, "%s:%s:%d  in_speech = %d utt_started=%d \n", 
                                       __FILE__, __func__, __LINE__, inSpeech, uttStarted);
                LOG(SpeechRecognition, "Listening...\n");
                
                uttStarted = true;
                FireSpeechEvent(providerContext, StartSpeech);
                continue;
            }

            if (providerContext->m_recognitionStatus == RecognitionAborted)
                break; 

            if (!inSpeech && uttStarted) {
                LOG(SpeechRecognition, "%s:%s:%d\n", __FILE__, __func__, __LINE__);
                /* speech -> silence transition, time to start new utterance  */
                ps_end_utt(providerContext->m_recognizer);

                FireSpeechEvent(providerContext, EndSpeech);
                FireSpeechEvent(providerContext, EndSound);

                text = ps_get_hyp(providerContext->m_recognizer, NULL );
                if (providerContext->m_recognitionStatus == RecognitionAborted)
                    break;

                if (text != NULL) {
                    LOG(SpeechRecognition, "%s\n", text);
                    // Set flag to indicate final result
                    speechHyp = new SpeechHyp;
                    speechHyp->final = true; speechHyp->hyp = text;
                    speechHyp->confidence = ps_get_prob(providerContext->m_recognizer);
                    providerContext->m_speechHypQueue.append (speechHyp);
                    FireSpeechEvent(providerContext, ReceiveResults);
                    LOG(SpeechRecognition, "%s:%s:%d\n", __FILE__, __func__, __LINE__);
                }
                LOG(SpeechRecognition, "%s:%s:%d\n", __FILE__, __func__, __LINE__);

                if (!providerContext->m_continuous)
                    goto EndSpeech;

                if ((providerContext->m_recognitionStatus != RecognitionStarted) && 
                    (silenceCnt >= MAX_SILENCE_PERIOD)) {
                     goto EndSpeech;
                }
                silenceCnt++;  
                

                LOG(SpeechRecognition, "%s:%s:%d\n", __FILE__, __func__, __LINE__);

                if (ps_start_utt(providerContext->m_recognizer) < 0) {
                    LOG(SpeechRecognition, "Failed to start utterance\n");
                    FireErrorEvent(providerContext, ReceiveError, SpeechRecognitionError::ErrorCodeAudioCapture, "Failed to start utterance");
                }
                uttStarted = FALSE;
                LOG(SpeechRecognition, "Ready....\n");

                FireSpeechEvent(providerContext, StartSound);    
            } else if (providerContext->m_interimResults && uttStarted && (INTRIM_INTERVAL == intrimCnt++)) { //interim speech
                LOG(SpeechRecognition, "%s:%s:%d\n", __FILE__, __func__, __LINE__);
                 text = ps_get_hyp(providerContext->m_recognizer, NULL );
                 if (text != NULL) {
                    LOG(SpeechRecognition, "Interim result %s\n", text);
                    speechHyp = new SpeechHyp;
                    speechHyp->final = false; speechHyp->hyp = text;
                    speechHyp->confidence = ps_get_prob(providerContext->m_recognizer);
                    providerContext->m_speechHypQueue.append (speechHyp);

                    FireSpeechEvent(providerContext, ReceiveResults);
              
                    LOG(SpeechRecognition, "%s:%s:%d quesize = %d \n", 
                                           __FILE__, __func__, __LINE__, providerContext->m_speechInputQueue.size());
                }
                intrimCnt = 0;
            }

            if (providerContext->m_recognitionStatus == RecognitionAborted)
                break; 
 
        }
       //usleep(100 * 1000);
    } 

    ps_end_utt(providerContext->m_recognizer);
    //delete text; TODO getting corruption while deleting text need to check

EndSpeech:
    FireSpeechEvent(providerContext, EndAudio);
    FireSpeechEvent(providerContext, End);

    providerContext->m_recognitionThread = 0; 
    providerContext->clearSpeechQueue(); 
    LOG(SpeechRecognition, "%s:%s:%d\n\n", __FILE__, __func__, __LINE__ );
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
        LOG(SpeechRecognition, "%s:%s:%d quesize = %d \n", __FILE__, __func__, __LINE__, m_speechInputQueue.size());
    }    
}

void PlatformSpeechRecognitionProviderWPE::start()
{
    LOG(SpeechRecognition, "%s:%s:%d \n", __FILE__, __func__, __LINE__);
    if (m_recognizer) {
        if (m_audioDevice) {
            recognizeFromDevice();
        } else {
            FireErrorEvent(this, ReceiveError, 
                           SpeechRecognitionError::ErrorCodeAudioCapture, "Failed to start Audio device, recheck");
        }
    } else {
        FireErrorEvent(this, ReceiveError, 
                       SpeechRecognitionError::ErrorCodeAudioCapture, "Failed to start recognizer");
    }
}

void PlatformSpeechRecognitionProviderWPE::abort()
{
    LOG(SpeechRecognition, "%s:%s:%d \n", __FILE__, __func__, __LINE__);
    if (m_recognitionThread) {
    LOG(SpeechRecognition, "%s:%s:%d \n", __FILE__, __func__, __LINE__);
        m_recognitionStatus = RecognitionAborted;
        waitForThreadCompletion (m_readThread);
        waitForThreadCompletion (m_recognitionThread);   
        m_recognitionThread = m_readThread = 0;    
    }
}

void PlatformSpeechRecognitionProviderWPE::stop()
{
    LOG(SpeechRecognition, "%s:%s:%d \n", __FILE__, __func__, __LINE__);
    if (m_recognitionThread) {
        LOG(SpeechRecognition, "%s:%s:%d \n", __FILE__, __func__, __LINE__);
        m_recognitionStatus = RecognitionStopped;
    }
}

void PlatformSpeechRecognitionProviderWPE::fireEventThread(void* context)
{
    PlatformSpeechRecognitionProviderWPE *providerContext = (PlatformSpeechRecognitionProviderWPE*) context;

    LOG(SpeechRecognition, "%s:%s:%d \n", __FILE__, __func__, __LINE__);
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

void PlatformSpeechRecognitionProviderWPE::fireSpeechEvent(SpeechEvent speechEvent) 
{
     
    switch (speechEvent) {
    case Start:
        LOG(SpeechRecognition, "%s:%s:%d Event:Start \n", __FILE__, __func__, __LINE__);
        m_platformSpeechRecognizer->client()->didStart();
        break;
    case StartAudio:
        LOG(SpeechRecognition, "%s:%s:%d Event:StartAudio \n", __FILE__, __func__, __LINE__);
        m_platformSpeechRecognizer->client()->didStartAudio();
        break;
    case StartSound:
        LOG(SpeechRecognition, "%s:%s:%d Event:StartSound \n", __FILE__, __func__, __LINE__);
        m_platformSpeechRecognizer->client()->didStartSound();
        break;
    case StartSpeech:
        LOG(SpeechRecognition, "%s:%s:%d Event:StartSpeech \n", __FILE__, __func__, __LINE__);
        m_platformSpeechRecognizer->client()->didStartSpeech();
        break;
    case End:
        LOG(SpeechRecognition, "%s:%s:%d Event:End \n", __FILE__, __func__, __LINE__);
        m_recognitionStatus = RecognitionStopped;
        m_platformSpeechRecognizer->client()->didEnd();
        m_recognitionThread = 0;
        break;
    case EndAudio:
        LOG(SpeechRecognition, "%s:%s:%d Event:EndAudio \n", __FILE__, __func__, __LINE__);
        m_platformSpeechRecognizer->client()->didEndAudio();
        break;
    case EndSound:
        LOG(SpeechRecognition, "%s:%s:%d Event:EndSound \n", __FILE__, __func__, __LINE__);
        m_platformSpeechRecognizer->client()->didEndSound();
        break;
    case EndSpeech:
        LOG(SpeechRecognition, "%s:%s:%d Event:EndSpeech \n", __FILE__, __func__, __LINE__);
        m_platformSpeechRecognizer->client()->didEndSpeech();
        break;
    case ReceiveResults: {
        LOG(SpeechRecognition, "%s:%s:%d Event:ReceiveResults \n", __FILE__, __func__, __LINE__);
        if (m_speechHypQueue.size() >= 1) {
         
            SpeechHyp *speechHyp = m_speechHypQueue[0];
            m_speechHypQueue.removeFirst(speechHyp);
            String transcripts(speechHyp->hyp); 

            if (m_recognitionStatus == RecognitionAborted)
                   break;

            Vector<RefPtr<SpeechRecognitionResult>> finalResults;
            Vector<RefPtr<SpeechRecognitionResult>> interimResults;
            Vector<RefPtr<SpeechRecognitionAlternative> > alternatives;
            alternatives.append(SpeechRecognitionAlternative::create(transcripts, speechHyp->confidence));

            if (speechHyp->final) {
                LOG(SpeechRecognition, "%s:%s:%d Fire FinalResults \n", __FILE__, __func__, __LINE__);
                finalResults.append(SpeechRecognitionResult::create(alternatives, true)); 
            } else if (m_interimResults) {
                LOG(SpeechRecognition, "%s:%s:%d Fire IntrimResults \n", __FILE__, __func__, __LINE__);
                interimResults.append(SpeechRecognitionResult::create(alternatives, false)); 
            }
            m_platformSpeechRecognizer->client()->didReceiveResults(finalResults, interimResults);

            delete speechHyp;
            break;
        }
    }
    case ReceiveNoMatch:
        LOG(SpeechRecognition, "%s:%s:%d Event:ReceiveNoMatch \n", __FILE__, __func__, __LINE__);
//        m_platformSpeechRecognizer->client()->didReceiveNoMatch();
        break;
    case ReceiveError:
        LOG(SpeechRecognition, "%s:%s:%d Event:ReceiveError\n", __FILE__, __func__, __LINE__);
        if (m_speechErrorQueue.size() >= 1) {
             String errMessage (m_speechErrorQueue[0].second);
             m_platformSpeechRecognizer->client()->didReceiveError(SpeechRecognitionError::create(m_speechErrorQueue[0].first, errMessage));
             m_speechErrorQueue.removeFirst(m_speechErrorQueue[0]);
        }
        break;
    default:
        LOG(SpeechRecognition, "%s:%s:%d Invalid Event %d\n", __FILE__, __func__, __LINE__, speechEvent);
        break;
    }
}

} // namespace WebCore

#endif
