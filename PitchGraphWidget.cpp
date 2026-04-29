#include "PitchGraphWidget.h"
#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QFile>
#include <QMenu>
#include <QPen>
#include <QTextStream>
#include <cmath>

PitchGraphWidget::PitchGraphWidget(QWidget* parent)
    : QWidget(parent), timeWindowSeconds_(10), retentionSeconds_(120), minFreq_(50.0f), maxFreq_(1000.0f) {

    setMinimumSize(400, 300);
    setStyleSheet("background-color: white;");

    // Update display at 30 FPS
    updateTimer_ = new QTimer(this);
    connect(updateTimer_, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
    updateTimer_->start(33); // ~30 FPS
}

void PitchGraphWidget::addPitchPoint(float frequency, float confidence, qint64 timestampMs) {
    if (frequency > 0.0f) {
        PitchPoint point;
        point.timestamp = (timestampMs >= 0) ? timestampMs : QDateTime::currentMSecsSinceEpoch();
        point.frequency = frequency;
        point.confidence = confidence;
        pitchData_.push_back(point);

        // Keep enough history for export/debug while bounding memory.
        if (pitchData_.size() > 6000) {
            pitchData_.pop_front();
        }

        removeOldData();
    }
}

void PitchGraphWidget::addAudioSamples(const float* data, unsigned int size) {
    WaveformData waveform;
    waveform.timestamp = QDateTime::currentMSecsSinceEpoch();

    // Downsample the audio data for visualization (take every Nth sample)
    unsigned int downsampleFactor = 16; // Increased from 4 to reduce memory usage
    waveform.samples.reserve(size / downsampleFactor);
    for (unsigned int i = 0; i < size; i += downsampleFactor) {
        waveform.samples.push_back(data[i]);
    }

    waveformData_.push_back(waveform);

    // Keep enough history for export/debug while bounding memory.
    if (waveformData_.size() > 3000) {
        waveformData_.pop_front();
    }

    removeOldWaveformData();
}

void PitchGraphWidget::clear() {
    pitchData_.clear();
    waveformData_.clear();
    update();
}

bool PitchGraphWidget::exportToTextFile(const QString& filePath, QString* errorMessage) const {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    QTextStream out(&file);
    out << "PitchGraph Export\n";
    out << "GeneratedAt=" << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << "\n";
    out << "TimeWindowSeconds=" << timeWindowSeconds_ << "\n";
    out << "FrequencyRangeHz=" << minFreq_ << "," << maxFreq_ << "\n";
    out << "PitchPointsCount=" << pitchData_.size() << "\n";
    out << "WaveformChunksCount=" << waveformData_.size() << "\n\n";

    out << "[PitchPoints]\n";
    out << "TimestampMs,TimestampIso,FrequencyHz,Confidence\n";
    for (const auto& point : pitchData_) {
        out << point.timestamp << ","
            << QDateTime::fromMSecsSinceEpoch(point.timestamp).toString(Qt::ISODateWithMs) << ","
            << point.frequency << ","
            << point.confidence << "\n";
    }

    out << "\n[WaveformChunks]\n";
    out << "TimestampMs,TimestampIso,Samples\n";
    for (const auto& chunk : waveformData_) {
        out << chunk.timestamp << ","
            << QDateTime::fromMSecsSinceEpoch(chunk.timestamp).toString(Qt::ISODateWithMs) << ",";

        for (size_t i = 0; i < chunk.samples.size(); ++i) {
            if (i > 0) {
                out << ";";
            }
            out << chunk.samples[i];
        }
        out << "\n";
    }

    if (out.status() != QTextStream::Ok) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed while writing data.");
        }
        return false;
    }

    return true;
}

void PitchGraphWidget::removeOldData() {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 cutoff = now - (retentionSeconds_ * 1000);

    while (!pitchData_.empty() && pitchData_.front().timestamp < cutoff) {
        pitchData_.pop_front();
    }
}

void PitchGraphWidget::removeOldWaveformData() {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 cutoff = now - (retentionSeconds_ * 1000);

    while (!waveformData_.empty() && waveformData_.front().timestamp < cutoff) {
        waveformData_.pop_front();
    }
}

void PitchGraphWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    drawGrid(painter);
    drawWaveform(painter);
    drawPitchCurve(painter);
}

void PitchGraphWidget::contextMenuEvent(QContextMenuEvent* event) {
    QMenu contextMenu(this);
    QAction* quitAction = contextMenu.addAction("Quit");
    QAction* selectedAction = contextMenu.exec(event->globalPos());
    if (selectedAction == quitAction) {
        qApp->quit();
    }
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

void PitchGraphWidget::drawWaveform(QPainter& painter) {
    if (waveformData_.empty()) {
        return;
    }

    removeOldWaveformData();

    int w = width();
    int h = height();
    int centerY = h / 2;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 timeWindow = timeWindowSeconds_ * 1000;

    // Draw waveform in light gray behind the pitch curve
    painter.setPen(QPen(QColor(200, 200, 200), 1));

    for (const auto& waveform : waveformData_) {
        qint64 age = now - waveform.timestamp;
        float timeRatio = 1.0f - (static_cast<float>(age) / timeWindow);

        if (timeRatio < 0.0f || timeRatio > 1.0f) {
            continue;
        }

        // Calculate starting x position for this waveform chunk
        int baseX = static_cast<int>(timeRatio * w);

        // Calculate width per sample based on time window and sample count
        float samplesPerPixel = static_cast<float>(waveform.samples.size()) / (w * 0.05f);
        float pixelWidth = 1.0f / samplesPerPixel;

        QPointF prevPoint;
        bool firstPoint = true;

        for (size_t i = 0; i < waveform.samples.size(); ++i) {
            // Map sample position to x coordinate
            int x = baseX - static_cast<int>(i * pixelWidth);

            if (x < 0) break;

            // Map amplitude (-1.0 to 1.0) to y coordinate
            // Scale down the amplitude for better visualization (30% of half height)
            float amplitude = waveform.samples[i];
            int y = centerY - static_cast<int>(amplitude * h * 0.15f);

            QPointF currentPoint(x, y);

            if (!firstPoint) {
                painter.drawLine(prevPoint, currentPoint);
            }

            prevPoint = currentPoint;
            firstPoint = false;
        }
    }
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
