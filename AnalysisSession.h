#ifndef ANALYSISSESSION_H
#define ANALYSISSESSION_H

#include <QJsonObject>
#include <QString>
#include <deque>
#include <vector>

struct AnalysisSessionLimits {
    int retentionSeconds = 120;
    size_t maxPitchPoints = 6000;
    size_t maxAnalysisFrames = 7000;
    size_t maxRawAudioChunks = 6000;
};

class AnalysisSession {
public:
    explicit AnalysisSession(const AnalysisSessionLimits& limits = AnalysisSessionLimits());

    void setDisplayConfig(int timeWindowSeconds, int retentionSeconds, float minFrequencyHz, float maxFrequencyHz);
    void clear();

    void addPitchPoint(float frequencyHz, float confidence, qint64 timestampMs = -1);
    void addAnalysisFrame(
        qint64 timestampMs,
        float frequencyHz,
        float confidence,
        float rms,
        unsigned int sampleCount,
        quint64 centerSampleIndex
    );
    void addRawAudioChunk(
        const float* data,
        unsigned int size,
        qint64 startTimestampMs = -1,
        unsigned int sampleRateHz = 0,
        quint64 startSampleIndex = 0
    );

    bool exportToJsonFile(
        const QString& filePath,
        const QJsonObject& sessionMetadata,
        QString* errorMessage = nullptr
    ) const;

private:
    struct PitchPoint {
        qint64 timestamp;
        float frequencyHz;
        float confidence;
    };

    struct AnalysisFrame {
        qint64 timestamp;
        float frequencyHz;
        float confidence;
        float rms;
        unsigned int sampleCount;
        quint64 centerSampleIndex;
        bool voiced;
    };

    struct RawAudioChunk {
        qint64 startTimestamp;
        qint64 centerTimestamp;
        quint64 startSampleIndex;
        unsigned int sampleCount;
        std::vector<float> samples;
    };

    void trimPitchPoints(qint64 nowMs);
    void trimAnalysisFrames(qint64 nowMs);
    void trimRawAudioChunks(qint64 nowMs);
    static qint64 resolveTimestamp(qint64 timestampMs);

    AnalysisSessionLimits limits_;
    int timeWindowSeconds_;
    int displayRetentionSeconds_;
    float minFrequencyHz_;
    float maxFrequencyHz_;
    std::deque<PitchPoint> pitchPoints_;
    std::deque<AnalysisFrame> analysisFrames_;
    std::deque<RawAudioChunk> rawAudioChunks_;
};

#endif // ANALYSISSESSION_H
