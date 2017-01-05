
#ifndef SpeechRecognitionResult_h
#define SpeechRecognitionResult_h

#if ENABLE(SPEECH_RECOGNITION)

#include "SpeechRecognitionAlternative.h"
#include "Logging.h"
#include "wtf/RefCounted.h"
#include "wtf/Vector.h"

namespace WebCore {

class SpeechRecognitionResult : public RefCounted<SpeechRecognitionResult> {
public:
    static PassRefPtr<SpeechRecognitionResult> create(const Vector<RefPtr<SpeechRecognitionAlternative> >&, bool final);

    unsigned long length() { return m_alternatives.size(); }
    SpeechRecognitionAlternative* item(unsigned long index);
    bool final() { return m_final; }

    SpeechRecognitionResult(const Vector<RefPtr<SpeechRecognitionAlternative> >&, bool final);
    virtual ~SpeechRecognitionResult() {}
private:

    bool m_final;
    Vector<RefPtr<SpeechRecognitionAlternative> > m_alternatives;
};

} // namespace WebCore


#endif //ENABLE(SPEECH_RECOGNITION)

#endif // SpeechRecognitionResult_h
