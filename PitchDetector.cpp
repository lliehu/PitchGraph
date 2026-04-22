#include "PitchDetector.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <numeric>

namespace {
float medianOf(std::vector<float> values) {
    if (values.empty()) {
        return 0.0f;
    }
    const size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid), values.end());
    if (values.size() % 2 == 1) {
        return values[mid];
    }
    const float upper = values[mid];
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid - 1), values.end());
    const float lower = values[mid - 1];
    return (lower + upper) * 0.5f;
}
}

PitchDetector::PitchDetector(unsigned int sampleRate, unsigned int windowSize, unsigned int hopSize)
    : sampleRate_(sampleRate),
      windowSize_(windowSize),
      hopSize_(hopSize),
      confidence_(0.0f),
      lastPitchHz_(0.0f) {

    // Monophonic speech pitch tracking with tight frame hop for intonation contour.
    pitch_ = new_aubio_pitch("yin", windowSize_, hopSize_, sampleRate_);

    // Set units to Hz
    aubio_pitch_set_unit(pitch_, "Hz");
    aubio_pitch_set_silence(pitch_, -45.0f);
    aubio_pitch_set_tolerance(pitch_, 0.8f);

    hopBuffer_ = new_fvec(hopSize_);
    outputBuffer_ = new_fvec(1);
    pendingSamples_.reserve(windowSize_ * 2);
}

PitchDetector::~PitchDetector() {
    del_aubio_pitch(pitch_);
    del_fvec(hopBuffer_);
    del_fvec(outputBuffer_);
}

float PitchDetector::detectPitch(const float* buffer, unsigned int size) {
    if (buffer == nullptr || size == 0) {
        confidence_ = 0.0f;
        return 0.0f;
    }

    pendingSamples_.insert(pendingSamples_.end(), buffer, buffer + size);

    std::vector<float> pitches;
    std::vector<float> confidences;
    pitches.reserve(16);
    confidences.reserve(16);

    size_t consumedSamples = 0;
    while ((pendingSamples_.size() - consumedSamples) >= hopSize_) {
        std::memcpy(hopBuffer_->data, pendingSamples_.data() + consumedSamples, hopSize_ * sizeof(float));
        consumedSamples += hopSize_;

        aubio_pitch_do(pitch_, hopBuffer_, outputBuffer_);

        float hopConfidence = aubio_pitch_get_confidence(pitch_);
        float hopPitch = outputBuffer_->data[0];

        if (hopConfidence < 0.20f || hopPitch < 70.0f || hopPitch > 500.0f) {
            continue;
        }

        // Fix common octave jumps by snapping to the previous stable octave when ratio is near 2:1.
        if (lastPitchHz_ > 0.0f) {
            float half = hopPitch * 0.5f;
            float twice = hopPitch * 2.0f;
            float directDelta = std::abs(hopPitch - lastPitchHz_);
            float halfDelta = std::abs(half - lastPitchHz_);
            float twiceDelta = std::abs(twice - lastPitchHz_);
            const float ratio = hopPitch / std::max(lastPitchHz_, 1.0f);
            if (half >= 70.0f && half <= 500.0f && ratio > 1.75f && ratio < 2.25f && halfDelta < directDelta * 0.80f) {
                hopPitch = half;
            } else if (twice >= 70.0f && twice <= 500.0f && ratio > 0.44f && ratio < 0.57f && twiceDelta < directDelta * 0.80f) {
                hopPitch = twice;
            }
        }

        pitches.push_back(hopPitch);
        confidences.push_back(hopConfidence);
    }
    if (consumedSamples > 0) {
        pendingSamples_.erase(pendingSamples_.begin(), pendingSamples_.begin() + static_cast<std::ptrdiff_t>(consumedSamples));
    }

    if (pitches.empty()) {
        confidence_ = 0.0f;
        return 0.0f;
    }

    // Weighted average across hops in this callback.
    float weightSum = std::accumulate(confidences.begin(), confidences.end(), 0.0f);
    if (weightSum <= 0.0f) {
        confidence_ = 0.0f;
        return 0.0f;
    }

    float weightedPitchSum = 0.0f;
    for (size_t i = 0; i < pitches.size(); ++i) {
        weightedPitchSum += pitches[i] * confidences[i];
    }
    float estimateHz = weightedPitchSum / weightSum;

    confidence_ = weightSum / static_cast<float>(confidences.size());

    // Smooth short-term jitter without flattening accent movement.
    if (lastPitchHz_ > 0.0f) {
        float alpha = 0.30f + 0.50f * std::min(1.0f, confidence_);
        float relativeJump = std::abs(estimateHz - lastPitchHz_) / std::max(lastPitchHz_, 1.0f);
        if (relativeJump > 0.35f && confidence_ < 0.60f) {
            alpha *= 0.45f;
        }
        estimateHz = lastPitchHz_ + alpha * (estimateHz - lastPitchHz_);
    }

    recentVoicedPitches_.push_back(estimateHz);
    if (recentVoicedPitches_.size() > 5) {
        recentVoicedPitches_.pop_front();
    }

    std::vector<float> recentCopy(recentVoicedPitches_.begin(), recentVoicedPitches_.end());
    const float recentMedian = medianOf(recentCopy);
    if (recentMedian > 0.0f) {
        const float deviation = std::abs(estimateHz - recentMedian) / recentMedian;
        if (deviation > 0.18f && confidence_ < 0.80f) {
            estimateHz = 0.70f * recentMedian + 0.30f * estimateHz;
        }
    }

    if (confidence_ >= 0.22f) {
        lastPitchHz_ = estimateHz;
        return estimateHz;
    }

    confidence_ = 0.0f;
    return 0.0f;
}
