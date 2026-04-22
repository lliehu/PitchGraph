#include "AudioCapture.h"
#include <cstring>

AudioCapture::AudioCapture(QObject* parent)
    : QThread(parent), pulseAudio_(nullptr), running_(false), bufferSize_(4096) {
}

AudioCapture::~AudioCapture() {
    stop();
}

bool AudioCapture::start(unsigned int sampleRate) {
    if (running_) {
        return false;
    }

    // Configure PulseAudio sample specification
    sampleSpec_.format = PA_SAMPLE_FLOAT32LE;
    sampleSpec_.channels = 1; // Mono
    sampleSpec_.rate = sampleRate;

    int error;
    // Open PulseAudio stream from monitor source (captures system audio playback)
    // Using "@DEFAULT_MONITOR@" to capture from the default sink's monitor
    pulseAudio_ = pa_simple_new(
        nullptr,                    // Default server
        "PitchGraph",              // Application name
        PA_STREAM_RECORD,          // Record stream
        "@DEFAULT_MONITOR@",       // Capture from default sink monitor (system audio)
        "Audio Capture",           // Stream description
        &sampleSpec_,              // Sample spec
        nullptr,                    // Default channel map
        nullptr,                    // Default buffering attributes
        &error
    );

    if (!pulseAudio_) {
        emit this->error(QString("Failed to create PulseAudio stream: %1").arg(pa_strerror(error)));
        return false;
    }

    running_ = true;
    QThread::start();
    return true;
}

void AudioCapture::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    QThread::wait();

    if (pulseAudio_) {
        pa_simple_free(pulseAudio_);
        pulseAudio_ = nullptr;
    }
}

void AudioCapture::run() {
    std::vector<float> buffer(bufferSize_);
    int error;

    while (running_) {
        // Read audio data from PulseAudio
        if (pa_simple_read(pulseAudio_, buffer.data(), bufferSize_ * sizeof(float), &error) < 0) {
            emit this->error(QString("Failed to read audio data: %1").arg(pa_strerror(error)));
            break;
        }

        // Emit a deep-copied buffer so cross-thread delivery is safe.
        emit audioDataReady(QVector<float>(buffer.begin(), buffer.end()));
    }
}
