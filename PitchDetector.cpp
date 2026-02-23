#include "PitchDetector.h"
#include <cstring>

PitchDetector::PitchDetector(unsigned int sampleRate, unsigned int bufferSize)
    : sampleRate_(sampleRate), bufferSize_(bufferSize), confidence_(0.0f) {

    // Create aubio pitch detection object with YIN algorithm
    pitch_ = new_aubio_pitch("yin", bufferSize, bufferSize, sampleRate);

    // Set units to Hz
    aubio_pitch_set_unit(pitch_, "Hz");

    // Set confidence threshold
    aubio_pitch_set_silence(pitch_, -40.0);
    aubio_pitch_set_tolerance(pitch_, 0.8);

    // Create input and output buffers
    inputBuffer_ = new_fvec(bufferSize);
    outputBuffer_ = new_fvec(1);
}

PitchDetector::~PitchDetector() {
    del_aubio_pitch(pitch_);
    del_fvec(inputBuffer_);
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

    // Perform pitch detection
    aubio_pitch_do(pitch_, inputBuffer_, outputBuffer_);

    // Get confidence
    confidence_ = aubio_pitch_get_confidence(pitch_);

    // Get pitch value
    float pitch = outputBuffer_->data[0];

    // Return pitch only if confidence is high enough
    if (confidence_ > 0.7f && pitch > 20.0f) {
        return pitch;
    }

    return 0.0f;
}
