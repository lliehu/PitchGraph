#ifndef PITCHGRAPHWIDGET_H
#define PITCHGRAPHWIDGET_H

#include <QWidget>
#include <QPainter>
#include <QTimer>
#include <deque>
#include <utility>

class PitchGraphWidget : public QWidget {
    Q_OBJECT

public:
    explicit PitchGraphWidget(QWidget* parent = nullptr);

    void addPitchPoint(float frequency, float confidence);
    void clear();

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

    std::deque<PitchPoint> pitchData_;
    QTimer* updateTimer_;

    // Configuration
    int timeWindowSeconds_;
    float minFreq_;
    float maxFreq_;

    void removeOldData();
    void drawGrid(QPainter& painter);
    void drawPitchCurve(QPainter& painter);
};

#endif // PITCHGRAPHWIDGET_H
