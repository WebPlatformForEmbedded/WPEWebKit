
#ifndef SpeechRecognitionAlternative_h
#define SpeechRecognitionAlternative_h

#if ENABLE(SPEECH_RECOGNITION)
#include "Logging.h"
#include "wtf/text/WTFString.h"

namespace WebCore {


class SpeechRecognitionAlternative : public RefCounted<SpeechRecognitionAlternative> {
public:
    static PassRefPtr<SpeechRecognitionAlternative> create(const String&, double);

    const String& transcript() const { return m_transcript; }
    double confidence() const { return m_confidence; }

    SpeechRecognitionAlternative(const String&, double);
    virtual ~SpeechRecognitionAlternative(){}
private:

    String m_transcript;
    double m_confidence;
};
} //namespace WebCore

#endif //ENABLE(SPEECH_RECOGNITION)
#endif // SpeechRecognitionAlternative_h
