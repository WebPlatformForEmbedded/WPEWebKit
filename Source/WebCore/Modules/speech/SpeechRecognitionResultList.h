
#ifndef SpeechRecognitionResultList_h
#define SpeechRecognitionResultList_h

#if  ENABLE(SPEECH_RECOGNITION)

#include "SpeechRecognitionResult.h"
#include "Logging.h"
#include "wtf/RefCounted.h"
#include "wtf/Vector.h"

namespace WebCore {

class SpeechRecognitionResultList : public RefCounted<SpeechRecognitionResultList> {
public:
    static PassRefPtr<SpeechRecognitionResultList> create(const Vector<RefPtr<SpeechRecognitionResult> >&);

    unsigned long length() { return m_results.size(); }
    SpeechRecognitionResult* item(unsigned long index);
    virtual ~SpeechRecognitionResultList() {}

private:
    explicit SpeechRecognitionResultList(const Vector<RefPtr<SpeechRecognitionResult> >&);
    Vector<RefPtr<SpeechRecognitionResult> > m_results;
};

} // namespace WebCore

#endif // ENABLE(SPEECH_RECOGNITION)

#endif // SpeechRecognitionResultList_h
