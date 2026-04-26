#ifndef PULSEAUDIOCAPTURE_H
#define PULSEAUDIOCAPTURE_H

#include "AudioCapture.h"
#include <atomic>
#include <pulse/error.h>
#include <pulse/simple.h>

class PulseAudioCapture final : public AudioCaptureBackend {
    Q_OBJECT

public:
    explicit PulseAudioCapture(QObject* parent = nullptr);
    ~PulseAudioCapture() override;

    bool start(unsigned int sampleRate = 44100) override;
    void stop() override;

protected:
    void run() override;

private:
    pa_simple* pulseAudio_;
    pa_sample_spec sampleSpec_;
    std::atomic<bool> running_;
    unsigned int bufferSize_;
};

#endif // PULSEAUDIOCAPTURE_H
