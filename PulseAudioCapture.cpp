#include "PulseAudioCapture.h"
#include <vector>

PulseAudioCapture::PulseAudioCapture(QObject* parent)
    : AudioCaptureBackend(parent),
      pulseAudio_(nullptr),
      running_(false),
      bufferSize_(1024) {}

PulseAudioCapture::~PulseAudioCapture() {
    stop();
}

bool PulseAudioCapture::start(unsigned int sampleRate) {
    if (running_) {
        return false;
    }

    sampleSpec_.format = PA_SAMPLE_FLOAT32LE;
    sampleSpec_.channels = 1;
    sampleSpec_.rate = sampleRate;

    int errorCode = 0;
    pulseAudio_ = pa_simple_new(
        nullptr,
        "PitchGraph",
        PA_STREAM_RECORD,
        "@DEFAULT_MONITOR@",
        "Audio Capture",
        &sampleSpec_,
        nullptr,
        nullptr,
        &errorCode
    );

    if (!pulseAudio_) {
        emit error(QString("Failed to create PulseAudio stream: %1").arg(pa_strerror(errorCode)));
        return false;
    }

    running_ = true;
    QThread::start();
    return true;
}

void PulseAudioCapture::stop() {
    if (!running_ && !isRunning()) {
        return;
    }

    running_ = false;
    if (isRunning()) {
        QThread::wait();
    }

    if (pulseAudio_) {
        pa_simple_free(pulseAudio_);
        pulseAudio_ = nullptr;
    }
}

void PulseAudioCapture::run() {
    std::vector<float> buffer(bufferSize_);
    int errorCode = 0;

    while (running_) {
        if (pa_simple_read(pulseAudio_, buffer.data(), bufferSize_ * sizeof(float), &errorCode) < 0) {
            emit error(QString("Failed to read audio data: %1").arg(pa_strerror(errorCode)));
            running_ = false;
            break;
        }

        emit audioDataReady(QVector<float>(buffer.begin(), buffer.end()));
    }

    running_ = false;
}
