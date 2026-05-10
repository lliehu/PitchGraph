#include "AnalysisSession.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
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

AnalysisSession::AnalysisSession(const AnalysisSessionLimits& limits)
    : limits_(limits), timeWindowSeconds_(10), displayRetentionSeconds_(limits.retentionSeconds), minFrequencyHz_(50.0f),
      maxFrequencyHz_(1000.0f) {}

void AnalysisSession::setDisplayConfig(
    int timeWindowSeconds,
    int retentionSeconds,
    float minFrequencyHz,
    float maxFrequencyHz
) {
    timeWindowSeconds_ = std::max(1, timeWindowSeconds);
    displayRetentionSeconds_ = std::max(0, retentionSeconds);
    minFrequencyHz_ = std::max(1.0f, minFrequencyHz);
    maxFrequencyHz_ = std::max(minFrequencyHz_ + 1.0f, maxFrequencyHz);
}

void AnalysisSession::clear() {
    pitchPoints_.clear();
    analysisFrames_.clear();
    rawAudioChunks_.clear();
}

void AnalysisSession::addPitchPoint(float frequencyHz, float confidence, qint64 timestampMs) {
    if (frequencyHz <= 0.0f) {
        return;
    }

    PitchPoint point;
    point.timestamp = resolveTimestamp(timestampMs);
    point.frequencyHz = frequencyHz;
    point.confidence = std::clamp(confidence, 0.0f, 1.0f);
    pitchPoints_.push_back(point);

    trimPitchPoints(point.timestamp);
}

void AnalysisSession::addAnalysisFrame(
    qint64 timestampMs,
    float frequencyHz,
    float confidence,
    float rms,
    unsigned int sampleCount,
    quint64 centerSampleIndex
) {
    AnalysisFrame frame;
    frame.timestamp = resolveTimestamp(timestampMs);
    frame.frequencyHz = frequencyHz;
    frame.confidence = std::clamp(confidence, 0.0f, 1.0f);
    frame.rms = std::max(0.0f, rms);
    frame.sampleCount = sampleCount;
    frame.centerSampleIndex = centerSampleIndex;
    frame.voiced = frequencyHz > 0.0f;
    analysisFrames_.push_back(frame);

    trimAnalysisFrames(frame.timestamp);
}

void AnalysisSession::addRawAudioChunk(
    const float* data,
    unsigned int size,
    qint64 startTimestampMs,
    unsigned int sampleRateHz,
    quint64 startSampleIndex
) {
    if (data == nullptr || size == 0) {
        return;
    }

    const qint64 startTimestamp = resolveTimestamp(startTimestampMs);
    const qint64 centerTimestamp =
        (sampleRateHz > 0)
            ? (startTimestamp + static_cast<qint64>(((size / 2.0) * 1000.0) / sampleRateHz))
            : startTimestamp;

    RawAudioChunk chunk;
    chunk.startTimestamp = startTimestamp;
    chunk.centerTimestamp = centerTimestamp;
    chunk.startSampleIndex = startSampleIndex;
    chunk.sampleCount = size;
    chunk.samples.assign(data, data + size);
    rawAudioChunks_.push_back(std::move(chunk));

    trimRawAudioChunks(centerTimestamp);
}

bool AnalysisSession::exportToJsonFile(
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
    display.insert("retention_seconds", displayRetentionSeconds_);
    display.insert("frequency_range_hz", QJsonArray{minFrequencyHz_, maxFrequencyHz_});
    root.insert("display", display);

    QJsonArray pitchTimeline;
    for (const auto& point : pitchPoints_) {
        QJsonObject pitchPoint;
        pitchPoint.insert("timestamp_ms", static_cast<double>(point.timestamp));
        pitchPoint.insert("timestamp_iso", QDateTime::fromMSecsSinceEpoch(point.timestamp).toString(Qt::ISODateWithMs));
        pitchPoint.insert("frequency_hz", toJsonNumberOrNull(point.frequencyHz));
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
        frameJson.insert("pitch_hz", frame.voiced ? toJsonNumberOrNull(frame.frequencyHz) : QJsonValue(QJsonValue::Null));
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

void AnalysisSession::trimPitchPoints(qint64 nowMs) {
    if (limits_.retentionSeconds > 0) {
        const qint64 cutoff = nowMs - (static_cast<qint64>(limits_.retentionSeconds) * 1000);
        while (!pitchPoints_.empty() && pitchPoints_.front().timestamp < cutoff) {
            pitchPoints_.pop_front();
        }
    }

    if (limits_.maxPitchPoints > 0) {
        while (pitchPoints_.size() > limits_.maxPitchPoints) {
            pitchPoints_.pop_front();
        }
    }
}

void AnalysisSession::trimAnalysisFrames(qint64 nowMs) {
    if (limits_.retentionSeconds > 0) {
        const qint64 cutoff = nowMs - (static_cast<qint64>(limits_.retentionSeconds) * 1000);
        while (!analysisFrames_.empty() && analysisFrames_.front().timestamp < cutoff) {
            analysisFrames_.pop_front();
        }
    }

    if (limits_.maxAnalysisFrames > 0) {
        while (analysisFrames_.size() > limits_.maxAnalysisFrames) {
            analysisFrames_.pop_front();
        }
    }
}

void AnalysisSession::trimRawAudioChunks(qint64 nowMs) {
    if (limits_.retentionSeconds > 0) {
        const qint64 cutoff = nowMs - (static_cast<qint64>(limits_.retentionSeconds) * 1000);
        while (!rawAudioChunks_.empty() && rawAudioChunks_.front().centerTimestamp < cutoff) {
            rawAudioChunks_.pop_front();
        }
    }

    if (limits_.maxRawAudioChunks > 0) {
        while (rawAudioChunks_.size() > limits_.maxRawAudioChunks) {
            rawAudioChunks_.pop_front();
        }
    }
}

qint64 AnalysisSession::resolveTimestamp(qint64 timestampMs) {
    return (timestampMs >= 0) ? timestampMs : QDateTime::currentMSecsSinceEpoch();
}
