#ifndef PITCHGRAPHWIDGET_H
#define PITCHGRAPHWIDGET_H

#include <QWidget>
#include <QString>
#include <QPainter>
#include <QTimer>
#include <QContextMenuEvent>
#include <QJsonObject>
#include <deque>
#include <memory>
#include <utility>
#include <vector>
#include "AnalysisSession.h"

class PitchGraphWidget : public QWidget {
    Q_OBJECT

public:
    explicit PitchGraphWidget(QWidget* parent = nullptr);
    void setAnalysisSession(AnalysisSession* session);

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
    void setTimeWindow(int seconds);
    void setFrequencyRange(float min, float max);

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

    std::deque<PitchPoint> pitchData_;
    std::deque<WaveformData> waveformData_;
    std::unique_ptr<AnalysisSession> ownedAnalysisSession_;
    AnalysisSession* analysisSession_;
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
    void syncSessionDisplayConfig();
    void drawGrid(QPainter& painter);
    void drawWaveform(QPainter& painter);
    void drawPitchCurve(QPainter& painter);
};

#endif // PITCHGRAPHWIDGET_H
