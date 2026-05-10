#ifndef PITCHGRAPHWIDGET_H
#define PITCHGRAPHWIDGET_H

#include <QWidget>
#include <QString>
#include <QPainter>
#include <QTimer>
#include <QContextMenuEvent>
#include <QJsonObject>
#include <deque>
#include <utility>

class PitchGraphWidget : public QWidget {
    Q_OBJECT

public:
    explicit PitchGraphWidget(QWidget* parent = nullptr);

    void addPitchPoint(float frequency, float confidence, qint64 timestampMs = -1);
    void addAudioSamples(
        const float* data,
        unsigned int size,
        qint64 startTimestampMs = -1,
        unsigned int sampleRateHz = 0,
        quint64 startSampleIndex = 0
    );
    void addAnalysisFrame(
        qint64 timestampMs,
        float frequencyHz,
        float confidence,
        float rms,
        unsigned int sampleCount,
        quint64 centerSampleIndex
    );
    void setFrozen(bool frozen, qint64 freezeTimestampMs = -1);
    void clear();
    bool exportToJsonFile(
        const QString& filePath,
        const QJsonObject& sessionMetadata,
        QString* errorMessage = nullptr
    ) const;

    // Configuration
    void setTimeWindow(int seconds) { timeWindowSeconds_ = seconds; }
    void setFrequencyRange(float min, float max) { minFreq_ = min; maxFreq_ = max; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    struct PitchPoint {
        qint64 timestamp;  // milliseconds
        float frequency;   // Hz
        float confidence;  // 0.0 to 1.0
    };

    struct WaveformData {
        qint64 timestamp;
        std::vector<float> samples;
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

    std::deque<PitchPoint> pitchData_;
    std::deque<WaveformData> waveformData_;
    std::deque<AnalysisFrame> analysisFrames_;
    std::deque<RawAudioChunk> rawAudioChunks_;
    QTimer* updateTimer_;

    // Configuration
    int timeWindowSeconds_;
    int retentionSeconds_;
    float minFreq_;
    float maxFreq_;
    bool isFrozen_;
    qint64 frozenTimestampMs_;

    void removeOldData(qint64 nowMs);
    void removeOldWaveformData(qint64 nowMs);
    void removeOldAnalysisFrames(qint64 nowMs);
    void removeOldRawAudioChunks(qint64 nowMs);
    void drawGrid(QPainter& painter);
    void drawWaveform(QPainter& painter);
    void drawPitchCurve(QPainter& painter);
};

#endif // PITCHGRAPHWIDGET_H
