#ifndef PITCHGRAPHWIDGET_H
#define PITCHGRAPHWIDGET_H

#include <QWidget>
#include <QString>
#include <QPainter>
#include <QTimer>
#include <deque>
#include <utility>

class PitchGraphWidget : public QWidget {
    Q_OBJECT

public:
    explicit PitchGraphWidget(QWidget* parent = nullptr);

    void addPitchPoint(float frequency, float confidence, qint64 timestampMs = -1);
    void addAudioSamples(const float* data, unsigned int size);
    void clear();
    bool exportToTextFile(const QString& filePath, QString* errorMessage = nullptr) const;

    // Configuration
    void setTimeWindow(int seconds) { timeWindowSeconds_ = seconds; }
    void setFrequencyRange(float min, float max) { minFreq_ = min; maxFreq_ = max; }

protected:
    void paintEvent(QPaintEvent* event) override;

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
    QTimer* updateTimer_;

    // Configuration
    int timeWindowSeconds_;
    float minFreq_;
    float maxFreq_;

    void removeOldData();
    void removeOldWaveformData();
    void drawGrid(QPainter& painter);
    void drawWaveform(QPainter& painter);
    void drawPitchCurve(QPainter& painter);
};

#endif // PITCHGRAPHWIDGET_H
