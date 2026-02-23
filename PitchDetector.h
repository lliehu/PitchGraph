#ifndef PITCHDETECTOR_H
#define PITCHDETECTOR_H

#include <aubio/aubio.h>
#include <vector>

class PitchDetector {
public:
    PitchDetector(unsigned int sampleRate = 44100, unsigned int bufferSize = 2048);
    ~PitchDetector();

    // Process audio buffer and return detected pitch in Hz
    // Returns 0.0 if no pitch detected or confidence too low
    float detectPitch(const float* buffer, unsigned int size);

    float getConfidence() const { return confidence_; }

private:
    aubio_pitch_t* pitch_;
    fvec_t* inputBuffer_;
    fvec_t* outputBuffer_;
    unsigned int sampleRate_;
    unsigned int bufferSize_;
    float confidence_;
};

#endif // PITCHDETECTOR_H
