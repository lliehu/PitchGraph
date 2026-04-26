#include "AudioCapture.h"
#include "AudioCaptureFactory.h"

AudioCapture::AudioCapture(QObject* parent)
    : QObject(parent),
      backend_(createAudioCaptureBackend()) {
    if (backend_) {
        connect(backend_.get(), &AudioCaptureBackend::audioDataReady, this, &AudioCapture::audioDataReady);
        connect(backend_.get(), &AudioCaptureBackend::error, this, &AudioCapture::error);
    }
}

AudioCapture::~AudioCapture() {
    stop();
}

bool AudioCapture::start(unsigned int sampleRate) {
    if (!backend_) {
        emit error("No supported audio capture backend for this platform.");
        return false;
    }
    return backend_->start(sampleRate);
}

void AudioCapture::stop() {
    if (backend_) {
        backend_->stop();
    }
}
