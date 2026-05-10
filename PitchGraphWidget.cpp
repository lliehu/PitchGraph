#include "PitchGraphWidget.h"
#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMenu>
#include <QPen>
#include <algorithm>
#include <cmath>

namespace {
QJsonValue toJsonNumberOrNull(float value) {
    if (!std::isfinite(value)) {
        return QJsonValue(QJsonValue::Null);
    }
    return QJsonValue(static_cast<double>(value));
}
}

PitchGraphWidget::PitchGraphWidget(QWidget* parent)
    : QWidget(parent), timeWindowSeconds_(10), retentionSeconds_(120), minFreq_(50.0f), maxFreq_(1000.0f),
      isFrozen_(false), frozenTimestampMs_(0) {

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

        removeOldData(QDateTime::currentMSecsSinceEpoch());
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

    RawAudioChunk rawChunk;
    rawChunk.startTimestamp = chunkStartTimestamp;
    rawChunk.centerTimestamp = chunkCenterTimestamp;
    rawChunk.startSampleIndex = startSampleIndex;
    rawChunk.sampleCount = size;
    rawChunk.samples.assign(data, data + size);
    rawAudioChunks_.push_back(std::move(rawChunk));

    if (rawAudioChunks_.size() > 6000) {
        rawAudioChunks_.pop_front();
    }

    removeOldWaveformData(nowMs);
    removeOldRawAudioChunks(nowMs);
}

void PitchGraphWidget::addAnalysisFrame(
    qint64 timestampMs,
    float frequencyHz,
    float confidence,
    float rms,
    unsigned int sampleCount,
    quint64 centerSampleIndex
) {
    AnalysisFrame frame;
    frame.timestamp = (timestampMs >= 0) ? timestampMs : QDateTime::currentMSecsSinceEpoch();
    frame.frequencyHz = frequencyHz;
    frame.confidence = std::clamp(confidence, 0.0f, 1.0f);
    frame.rms = std::max(0.0f, rms);
    frame.sampleCount = sampleCount;
    frame.centerSampleIndex = centerSampleIndex;
    frame.voiced = frequencyHz > 0.0f;
    analysisFrames_.push_back(frame);

    if (analysisFrames_.size() > 7000) {
        analysisFrames_.pop_front();
    }

    removeOldAnalysisFrames(QDateTime::currentMSecsSinceEpoch());
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
    analysisFrames_.clear();
    rawAudioChunks_.clear();
    update();
}

bool PitchGraphWidget::exportToJsonFile(
    const QString& filePath,
    const QJsonObject& sessionMetadata,
    QString* errorMessage
) const {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    QJsonObject root;
    root.insert("schema", QStringLiteral("pitchgraph.export.v2"));
    root.insert("generated_at_iso", QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    root.insert("session", sessionMetadata);

    QJsonObject display;
    display.insert("time_window_seconds", timeWindowSeconds_);
    display.insert("retention_seconds", retentionSeconds_);
    display.insert("frequency_range_hz", QJsonArray{minFreq_, maxFreq_});
    root.insert("display", display);

    QJsonArray pitchTimeline;
    for (const auto& point : pitchData_) {
        QJsonObject pitchPoint;
        pitchPoint.insert("timestamp_ms", static_cast<double>(point.timestamp));
        pitchPoint.insert("timestamp_iso", QDateTime::fromMSecsSinceEpoch(point.timestamp).toString(Qt::ISODateWithMs));
        pitchPoint.insert("frequency_hz", toJsonNumberOrNull(point.frequency));
        pitchPoint.insert("confidence", toJsonNumberOrNull(point.confidence));
        pitchTimeline.append(pitchPoint);
    }
    root.insert("pitch_points_voiced_only", pitchTimeline);

    QJsonArray frames;
    for (const auto& frame : analysisFrames_) {
        QJsonObject frameJson;
        frameJson.insert("timestamp_ms", static_cast<double>(frame.timestamp));
        frameJson.insert("timestamp_iso", QDateTime::fromMSecsSinceEpoch(frame.timestamp).toString(Qt::ISODateWithMs));
        frameJson.insert("sample_count", static_cast<int>(frame.sampleCount));
        frameJson.insert("center_sample_index", static_cast<double>(frame.centerSampleIndex));
        frameJson.insert("rms", toJsonNumberOrNull(frame.rms));
        frameJson.insert("confidence", toJsonNumberOrNull(frame.confidence));
        frameJson.insert("voiced", frame.voiced);
        frameJson.insert(
            "pitch_hz",
            frame.voiced ? toJsonNumberOrNull(frame.frequencyHz) : QJsonValue(QJsonValue::Null)
        );
        frames.append(frameJson);
    }
    root.insert("frames", frames);

    QJsonObject rawAudio;
    rawAudio.insert("encoding", QStringLiteral("pcm_f32le"));
    rawAudio.insert("channels", 1);

    QJsonArray chunks;
    for (const auto& chunk : rawAudioChunks_) {
        QJsonObject chunkJson;
        chunkJson.insert("start_timestamp_ms", static_cast<double>(chunk.startTimestamp));
        chunkJson.insert(
            "start_timestamp_iso",
            QDateTime::fromMSecsSinceEpoch(chunk.startTimestamp).toString(Qt::ISODateWithMs)
        );
        chunkJson.insert("center_timestamp_ms", static_cast<double>(chunk.centerTimestamp));
        chunkJson.insert(
            "center_timestamp_iso",
            QDateTime::fromMSecsSinceEpoch(chunk.centerTimestamp).toString(Qt::ISODateWithMs)
        );
        chunkJson.insert("start_sample_index", static_cast<double>(chunk.startSampleIndex));
        chunkJson.insert("sample_count", static_cast<int>(chunk.sampleCount));

        QJsonArray sampleArray;
        for (const float sample : chunk.samples) {
            sampleArray.append(toJsonNumberOrNull(sample));
        }
        chunkJson.insert("samples", sampleArray);
        chunks.append(chunkJson);
    }
    rawAudio.insert("chunks", chunks);
    root.insert("raw_audio", rawAudio);

    const QJsonDocument document(root);
    const qint64 bytesWritten = file.write(document.toJson(QJsonDocument::Indented));
    if (bytesWritten < 0) {
        if (errorMessage != nullptr) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    return true;
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

void PitchGraphWidget::removeOldAnalysisFrames(qint64 nowMs) {
    const qint64 cutoff = nowMs - (retentionSeconds_ * 1000);

    while (!analysisFrames_.empty() && analysisFrames_.front().timestamp < cutoff) {
        analysisFrames_.pop_front();
    }
}

void PitchGraphWidget::removeOldRawAudioChunks(qint64 nowMs) {
    const qint64 cutoff = nowMs - (retentionSeconds_ * 1000);

    while (!rawAudioChunks_.empty() && rawAudioChunks_.front().centerTimestamp < cutoff) {
        rawAudioChunks_.pop_front();
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
