#include "PitchDetector.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <numeric>

namespace {
constexpr float kMinValidPitchHz = 70.0f;
constexpr float kMaxValidPitchHz = 500.0f;
constexpr float kMinHopConfidence = 0.22f;
constexpr float kMinOutputConfidence = 0.28f;
constexpr float kSlewGuardHighJumpCents = 360.0f;
constexpr float kSlewGuardMediumJumpCents = 260.0f;
constexpr float kExtremeDeviationCents = 320.0f;

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

float centsDistance(float a, float b) {
    if (a <= 0.0f || b <= 0.0f) {
        return std::numeric_limits<float>::infinity();
    }
    return std::abs(1200.0f * std::log2(a / b));
}

float weightedLogMeanHz(const std::vector<float>& pitches, const std::vector<float>& confidences) {
    float weightedLogSum = 0.0f;
    float weightSum = 0.0f;
    for (size_t i = 0; i < pitches.size(); ++i) {
        const float hz = pitches[i];
        const float w = confidences[i];
        if (hz <= 0.0f || w <= 0.0f) {
            continue;
        }
        weightedLogSum += std::log2(hz) * w;
        weightSum += w;
    }
    if (weightSum <= 0.0f) {
        return 0.0f;
    }
    return std::exp2(weightedLogSum / weightSum);
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

    float referencePitch = 0.0f;
    float referenceConfidence = 0.0f;
    if (!recentVoicedPitches_.empty()) {
        std::vector<float> recentPitchCopy(recentVoicedPitches_.begin(), recentVoicedPitches_.end());
        referencePitch = medianOf(recentPitchCopy);
        std::vector<float> recentConfCopy(recentVoicedConfidences_.begin(), recentVoicedConfidences_.end());
        referenceConfidence = medianOf(recentConfCopy);
    } else if (lastPitchHz_ > 0.0f) {
        referencePitch = lastPitchHz_;
        referenceConfidence = confidence_;
    }

    size_t consumedSamples = 0;
    while ((pendingSamples_.size() - consumedSamples) >= hopSize_) {
        std::memcpy(hopBuffer_->data, pendingSamples_.data() + consumedSamples, hopSize_ * sizeof(float));
        consumedSamples += hopSize_;

        aubio_pitch_do(pitch_, hopBuffer_, outputBuffer_);

        float hopConfidence = aubio_pitch_get_confidence(pitch_);
        float hopPitch = outputBuffer_->data[0];

        if (hopConfidence < kMinHopConfidence || hopPitch < kMinValidPitchHz || hopPitch > kMaxValidPitchHz) {
            continue;
        }

        // Octave correction is only applied when either the current hop is weak or the recent track is stable.
        if (referencePitch > 0.0f) {
            float rawDistance = centsDistance(hopPitch, referencePitch);
            float bestPitch = hopPitch;
            float bestDistance = rawDistance;
            float half = hopPitch * 0.5f;
            float twice = hopPitch * 2.0f;

            if (half >= kMinValidPitchHz && half <= kMaxValidPitchHz) {
                const float halfDistance = centsDistance(half, referencePitch);
                if (halfDistance < bestDistance) {
                    bestDistance = halfDistance;
                    bestPitch = half;
                }
            }
            if (twice >= kMinValidPitchHz && twice <= kMaxValidPitchHz) {
                const float twiceDistance = centsDistance(twice, referencePitch);
                if (twiceDistance < bestDistance) {
                    bestDistance = twiceDistance;
                    bestPitch = twice;
                }
            }

            const bool stableReference = recentVoicedPitches_.size() >= 3 && referenceConfidence >= 0.50f;
            const bool lowConfidenceHop = hopConfidence < 0.40f;
            const bool meaningfulImprovement = (rawDistance - bestDistance) > 45.0f;
            if (bestPitch != hopPitch && meaningfulImprovement && (stableReference || lowConfidenceHop)) {
                hopPitch = bestPitch;
            }

            // Reject likely subharmonic artifacts once a stable baseline exists.
            if (stableReference && hopPitch < (referencePitch * 0.62f) && hopConfidence < 0.75f) {
                continue;
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

    // Weighted average across hops in this callback, in log space (pitch is multiplicative).
    float weightSum = std::accumulate(confidences.begin(), confidences.end(), 0.0f);
    if (weightSum <= 0.0f) {
        confidence_ = 0.0f;
        return 0.0f;
    }

    float estimateHz = weightedLogMeanHz(pitches, confidences);
    if (estimateHz <= 0.0f) {
        confidence_ = 0.0f;
        return 0.0f;
    }

    confidence_ = weightSum / static_cast<float>(confidences.size());

    // Smooth short-term jitter without flattening accent movement.
    if (lastPitchHz_ > 0.0f) {
        float alpha = 0.20f + 0.55f * std::min(1.0f, confidence_);
        const float jumpCents = centsDistance(estimateHz, lastPitchHz_);
        // Speech F0 cannot realistically jump by several semitones within one ~20 ms update.
        // Clamp adaptation on extreme frame-to-frame leaps to suppress single-frame spikes.
        if (jumpCents > kSlewGuardHighJumpCents) {
            alpha = std::min(alpha, 0.10f);
        } else if (jumpCents > kSlewGuardMediumJumpCents) {
            alpha = std::min(alpha, 0.18f);
        }
        if (jumpCents > 220.0f && confidence_ < 0.70f) {
            alpha *= 0.40f;
        } else if (jumpCents > 120.0f) {
            alpha *= 0.70f;
        }
        estimateHz = lastPitchHz_ + alpha * (estimateHz - lastPitchHz_);
    }

    std::vector<float> recentCopy(recentVoicedPitches_.begin(), recentVoicedPitches_.end());
    const float recentMedian = medianOf(recentCopy);
    if (recentMedian > 0.0f) {
        const float deviationCents = centsDistance(estimateHz, recentMedian);
        if (deviationCents > kExtremeDeviationCents) {
            estimateHz = 0.85f * recentMedian + 0.15f * estimateHz;
        } else if (deviationCents > 180.0f && confidence_ < 0.82f) {
            const float pull = (deviationCents > 300.0f) ? 0.80f : 0.55f;
            estimateHz = (pull * recentMedian) + ((1.0f - pull) * estimateHz);
        }
    }

    if (confidence_ >= kMinOutputConfidence) {
        lastPitchHz_ = estimateHz;
        recentVoicedPitches_.push_back(estimateHz);
        recentVoicedConfidences_.push_back(confidence_);
        while (recentVoicedPitches_.size() > 5) {
            recentVoicedPitches_.pop_front();
        }
        while (recentVoicedConfidences_.size() > 5) {
            recentVoicedConfidences_.pop_front();
        }
        return estimateHz;
    }

    confidence_ = 0.0f;
    return 0.0f;
}
