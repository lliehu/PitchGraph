#include "PitchGraphWidget.h"
#include <QDateTime>
#include <QPen>
#include <cmath>

PitchGraphWidget::PitchGraphWidget(QWidget* parent)
    : QWidget(parent), timeWindowSeconds_(10), minFreq_(50.0f), maxFreq_(1000.0f) {

    setMinimumSize(400, 300);
    setStyleSheet("background-color: white;");

    // Update display at 30 FPS
    updateTimer_ = new QTimer(this);
    connect(updateTimer_, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
    updateTimer_->start(33); // ~30 FPS
}

void PitchGraphWidget::addPitchPoint(float frequency, float confidence) {
    if (frequency > 0.0f) {
        PitchPoint point;
        point.timestamp = QDateTime::currentMSecsSinceEpoch();
        point.frequency = frequency;
        point.confidence = confidence;
        pitchData_.push_back(point);

        removeOldData();
    }
}

void PitchGraphWidget::clear() {
    pitchData_.clear();
    update();
}

void PitchGraphWidget::removeOldData() {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 cutoff = now - (timeWindowSeconds_ * 1000);

    while (!pitchData_.empty() && pitchData_.front().timestamp < cutoff) {
        pitchData_.pop_front();
    }
}

void PitchGraphWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    drawGrid(painter);
    drawPitchCurve(painter);
}

void PitchGraphWidget::drawGrid(QPainter& painter) {
    int w = width();
    int h = height();

    // Draw border
    painter.setPen(QPen(Qt::black, 2));
    painter.drawRect(0, 0, w - 1, h - 1);

    // Draw grid lines
    painter.setPen(QPen(Qt::lightGray, 1, Qt::DashLine));

    // Horizontal lines (frequency)
    for (int i = 1; i < 10; i++) {
        int y = h * i / 10;
        painter.drawLine(0, y, w, y);
    }

    // Vertical lines (time)
    for (int i = 1; i < 10; i++) {
        int x = w * i / 10;
        painter.drawLine(x, 0, x, h);
    }

    // Draw axis labels
    painter.setPen(Qt::black);
    QFont font = painter.font();
    font.setPointSize(8);
    painter.setFont(font);

    // Frequency labels (left side)
    for (int i = 0; i <= 10; i++) {
        float freq = minFreq_ + (maxFreq_ - minFreq_) * (10 - i) / 10.0f;
        int y = h * i / 10;
        painter.drawText(5, y + 15, QString("%1 Hz").arg(static_cast<int>(freq)));
    }

    // Time label
    painter.drawText(w - 100, h - 10, QString("%1 seconds").arg(timeWindowSeconds_));
}

void PitchGraphWidget::drawPitchCurve(QPainter& painter) {
    if (pitchData_.empty()) {
        return;
    }

    removeOldData();

    int w = width();
    int h = height();
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 timeWindow = timeWindowSeconds_ * 1000;

    painter.setPen(QPen(Qt::blue, 2));

    QPointF prevPoint;
    bool firstPoint = true;

    for (const auto& point : pitchData_) {
        // Calculate x position (time-based, scrolling from right to left)
        qint64 age = now - point.timestamp;
        float timeRatio = 1.0f - (static_cast<float>(age) / timeWindow);
        int x = static_cast<int>(timeRatio * w);

        // Calculate y position (frequency-based, inverted so higher freq is at top)
        float freqRatio = (point.frequency - minFreq_) / (maxFreq_ - minFreq_);
        freqRatio = std::max(0.0f, std::min(1.0f, freqRatio));
        int y = static_cast<int>((1.0f - freqRatio) * h);

        QPointF currentPoint(x, y);

        // Draw line from previous point
        if (!firstPoint) {
            // Set color based on confidence
            QColor color = Qt::blue;
            color.setAlphaF(point.confidence);
            painter.setPen(QPen(color, 2));
            painter.drawLine(prevPoint, currentPoint);
        }

        prevPoint = currentPoint;
        firstPoint = false;

        // Draw point
        painter.setBrush(Qt::blue);
        painter.drawEllipse(currentPoint, 3, 3);
    }
}
