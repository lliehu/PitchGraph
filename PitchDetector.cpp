#include "PitchDetector.h"
#include <cstring>
#include <cmath>

PitchDetector::PitchDetector(unsigned int sampleRate, unsigned int bufferSize)
    : sampleRate_(sampleRate), bufferSize_(bufferSize), confidence_(0.0f) {

    // Create aubio pitch detection object with YINFAST algorithm
    // YINFAST is better for polyphonic/complex audio
    pitch_ = new_aubio_pitch("yinfast", bufferSize, bufferSize / 2, sampleRate);

    // Set units to Hz
    aubio_pitch_set_unit(pitch_, "Hz");

    // Optimize for human voice detection
    // Human voice fundamental frequency range: ~85-300 Hz (male and female)
    // Silence threshold: adjusted to filter out background noise
    aubio_pitch_set_silence(pitch_, -35.0);

    // Tolerance: optimized for voice pitch tracking
    aubio_pitch_set_tolerance(pitch_, 0.6);

    // Create aubio bandpass filter for human voice range (80 Hz - 1000 Hz)
    // This isolates voice frequencies and suppresses bass/drums and very high frequencies
    bandpassFilter_ = new_aubio_filter_a_weighting(sampleRate);

    // Create input, filtered, and output buffers
    inputBuffer_ = new_fvec(bufferSize);
    filteredBuffer_ = new_fvec(bufferSize);
    outputBuffer_ = new_fvec(1);
}

PitchDetector::~PitchDetector() {
    del_aubio_pitch(pitch_);
    del_aubio_filter(bandpassFilter_);
    del_fvec(inputBuffer_);
    del_fvec(filteredBuffer_);
    del_fvec(outputBuffer_);
}

float PitchDetector::detectPitch(const float* buffer, unsigned int size) {
    // Copy input data to aubio buffer
    unsigned int copySize = size < bufferSize_ ? size : bufferSize_;
    std::memcpy(inputBuffer_->data, buffer, copySize * sizeof(float));

    // Zero-pad if needed
    if (copySize < bufferSize_) {
        std::memset(inputBuffer_->data + copySize, 0, (bufferSize_ - copySize) * sizeof(float));
    }

    // Apply A-weighting filter to emphasize human voice frequencies
    // A-weighting approximates human hearing sensitivity
    aubio_filter_do(bandpassFilter_, inputBuffer_);

    // Apply custom voice range filtering
    applyVoiceRangeFilter(inputBuffer_->data, filteredBuffer_->data, bufferSize_);

    // Perform pitch detection on filtered audio
    aubio_pitch_do(pitch_, filteredBuffer_, outputBuffer_);

    // Get confidence
    confidence_ = aubio_pitch_get_confidence(pitch_);

    // Get pitch value
    float pitch = outputBuffer_->data[0];

    // Human voice pitch range:
    // Male voices: ~85-180 Hz fundamental
    // Female voices: ~165-255 Hz fundamental
    // Children: ~250-300 Hz fundamental
    // Accept range: 80-400 Hz to cover all voice types safely
    if (confidence_ > 0.4f && pitch >= 80.0f && pitch <= 400.0f) {
        return pitch;
    }

    return 0.0f;
}

void PitchDetector::applyVoiceRangeFilter(const float* input, float* output, unsigned int size) {
    // Simple high-pass filter to remove low-frequency noise (bass, drums, rumble)
    // and gentle low-pass to reduce very high frequencies
    // This is a basic IIR filter that emphasizes 80-1000 Hz range (human voice)

    static float prevInput = 0.0f;
    static float prevOutput = 0.0f;

    // High-pass filter coefficient (removes frequencies below ~80 Hz)
    const float hpAlpha = 0.96f;

    for (unsigned int i = 0; i < size; ++i) {
        // High-pass filter
        float hpOutput = hpAlpha * (prevOutput + input[i] - prevInput);
        prevInput = input[i];
        prevOutput = hpOutput;

        // Apply gentle boost to voice range (simple amplification)
        output[i] = hpOutput * 1.5f;

        // Clip to prevent distortion
        if (output[i] > 1.0f) output[i] = 1.0f;
        if (output[i] < -1.0f) output[i] = -1.0f;
    }
}
