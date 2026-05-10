#include "PitchGraphWidget.h"
#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QMenu>
#include <QPen>
#include <algorithm>
#include <cmath>

PitchGraphWidget::PitchGraphWidget(QWidget* parent)
    : QWidget(parent), timeWindowSeconds_(10), retentionSeconds_(120), minFreq_(50.0f), maxFreq_(1000.0f),
      isFrozen_(false), frozenTimestampMs_(0) {

    ownedAnalysisSession_ = std::make_unique<AnalysisSession>();
    analysisSession_ = ownedAnalysisSession_.get();
    syncSessionDisplayConfig();

    setMinimumSize(400, 300);
    setStyleSheet("background-color: white;");

    // Update display at 30 FPS
    updateTimer_ = new QTimer(this);
    connect(updateTimer_, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
    updateTimer_->start(33); // ~30 FPS
}

void PitchGraphWidget::setAnalysisSession(AnalysisSession* session) {
    analysisSession_ = (session != nullptr) ? session : ownedAnalysisSession_.get();
    syncSessionDisplayConfig();
}

void PitchGraphWidget::setTimeWindow(int seconds) {
    timeWindowSeconds_ = std::max(1, seconds);
    syncSessionDisplayConfig();
}

void PitchGraphWidget::setFrequencyRange(float min, float max) {
    minFreq_ = std::max(1.0f, min);
    maxFreq_ = std::max(minFreq_ + 1.0f, max);
    syncSessionDisplayConfig();
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

        removeOldData(QDateTime::currentMSecsSinceEpoch());

        if (analysisSession_ != nullptr) {
            analysisSession_->addPitchPoint(frequency, confidence, point.timestamp);
        }
    }
}

void PitchGraphWidget::addAudioSamples(
    const float* data,
    unsigned int size,
    qint64 startTimestampMs,
    unsigned int sampleRateHz,
    quint64 startSampleIndex
) {
    if (data == nullptr || size == 0) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 chunkStartTimestamp = (startTimestampMs >= 0) ? startTimestampMs : nowMs;
    const qint64 chunkCenterTimestamp =
        (sampleRateHz > 0)
            ? (chunkStartTimestamp + static_cast<qint64>(((size / 2.0) * 1000.0) / sampleRateHz))
            : chunkStartTimestamp;

    WaveformData waveform;
    waveform.timestamp = chunkCenterTimestamp;

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

    removeOldWaveformData(nowMs);

    if (analysisSession_ != nullptr) {
        analysisSession_->addRawAudioChunk(data, size, chunkStartTimestamp, sampleRateHz, startSampleIndex);
    }
}

void PitchGraphWidget::addAnalysisFrame(
    qint64 timestampMs,
    float frequencyHz,
    float confidence,
    float rms,
    unsigned int sampleCount,
    quint64 centerSampleIndex
) {
    if (analysisSession_ != nullptr) {
        analysisSession_->addAnalysisFrame(timestampMs, frequencyHz, confidence, rms, sampleCount, centerSampleIndex);
    }
}

void PitchGraphWidget::setFrozen(bool frozen, qint64 freezeTimestampMs) {
    isFrozen_ = frozen;
    if (isFrozen_) {
        frozenTimestampMs_ = (freezeTimestampMs >= 0) ? freezeTimestampMs : QDateTime::currentMSecsSinceEpoch();
        updateTimer_->stop();
    } else if (!updateTimer_->isActive()) {
        updateTimer_->start(33);
    }

    update();
}

void PitchGraphWidget::clear() {
    pitchData_.clear();
    waveformData_.clear();
    if (analysisSession_ != nullptr) {
        analysisSession_->clear();
    }
    update();
}

bool PitchGraphWidget::exportToJsonFile(
    const QString& filePath,
    const QJsonObject& sessionMetadata,
    QString* errorMessage
) const {
    if (analysisSession_ == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Analysis session is not configured.");
        }
        return false;
    }
    return analysisSession_->exportToJsonFile(filePath, sessionMetadata, errorMessage);
}

void PitchGraphWidget::removeOldData(qint64 nowMs) {
    qint64 cutoff = nowMs - (retentionSeconds_ * 1000);

    while (!pitchData_.empty() && pitchData_.front().timestamp < cutoff) {
        pitchData_.pop_front();
    }
}

void PitchGraphWidget::removeOldWaveformData(qint64 nowMs) {
    qint64 cutoff = nowMs - (retentionSeconds_ * 1000);

    while (!waveformData_.empty() && waveformData_.front().timestamp < cutoff) {
        waveformData_.pop_front();
    }
}

void PitchGraphWidget::syncSessionDisplayConfig() {
    if (analysisSession_ != nullptr) {
        analysisSession_->setDisplayConfig(timeWindowSeconds_, retentionSeconds_, minFreq_, maxFreq_);
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
    contextMenu.setStyleSheet(
        "QMenu { background-color: #f5f5f5; color: #1f1f1f; }"
        "QMenu::item:selected { background-color: #dce8ff; color: #111111; }"
    );
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

    const qint64 renderNow = isFrozen_ ? frozenTimestampMs_ : QDateTime::currentMSecsSinceEpoch();
    if (!isFrozen_) {
        removeOldWaveformData(renderNow);
    }

    int w = width();
    int h = height();
    int centerY = h / 2;
    qint64 timeWindow = timeWindowSeconds_ * 1000;

    // Draw waveform in light gray behind the pitch curve
    painter.setPen(QPen(QColor(200, 200, 200), 1));

    for (const auto& waveform : waveformData_) {
        qint64 age = renderNow - waveform.timestamp;
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

    const qint64 renderNow = isFrozen_ ? frozenTimestampMs_ : QDateTime::currentMSecsSinceEpoch();
    if (!isFrozen_) {
        removeOldData(renderNow);
    }

    int w = width();
    int h = height();
    qint64 timeWindow = timeWindowSeconds_ * 1000;

    Qt::GlobalColor pitchColor = Qt::darkGreen;
    painter.setPen(QPen(pitchColor, 2));

    QPointF prevPoint;
    bool firstPoint = true;

    for (const auto& point : pitchData_) {
        // Calculate x position (time-based, scrolling from right to left)
        qint64 age = renderNow - point.timestamp;
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
            QColor color = pitchColor;
            color.setAlphaF(point.confidence);
            painter.setPen(QPen(color, 2));
            painter.drawLine(prevPoint, currentPoint);
        }

        prevPoint = currentPoint;
        firstPoint = false;

        // Draw point
        painter.setBrush(pitchColor);
        painter.drawEllipse(currentPoint, 3, 3);
    }
}
