#ifndef WASAPIAUDIOCAPTURE_H
#define WASAPIAUDIOCAPTURE_H

#include "AudioCapture.h"
#include <atomic>
#include <vector>

class WasapiAudioCapture final : public AudioCaptureBackend {
    Q_OBJECT

public:
    explicit WasapiAudioCapture(QObject* parent = nullptr);
    ~WasapiAudioCapture() override;

    bool start(unsigned int sampleRate = 44100) override;
    void stop() override;

protected:
    void run() override;

private:
    void processSamples(const std::vector<float>& samples);
    void resampleAndQueue(const std::vector<float>& samples);
    void enqueueOutputSamples(const std::vector<float>& samples);

    std::atomic<bool> running_;
    unsigned int requestedSampleRate_;
    unsigned int deviceSampleRate_;
    unsigned int chunkSize_;
    std::vector<float> outputSamples_;
    std::vector<float> resampleBuffer_;
    double resampleIndex_;
    double resampleStep_;
};

#endif // WASAPIAUDIOCAPTURE_H
