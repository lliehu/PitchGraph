#include "BatchExportRunner.h"

#include "AnalysisSession.h"
#include "OfflineWavReader.h"
#include "PitchDetector.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QSysInfo>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr unsigned int kCaptureSampleRateHz = 48000;
constexpr unsigned int kDetectorWindowSize = 2048;
constexpr unsigned int kDetectorHopSize = 256;
constexpr float kDetectorSilenceDb = -45.0f;
constexpr float kDetectorTolerance = 0.8f;
constexpr float kMinValidPitchHz = 70.0f;
constexpr float kMaxValidPitchHz = 500.0f;
constexpr float kMinHopConfidence = 0.22f;
constexpr float kMinOutputConfidence = 0.28f;
constexpr unsigned int kBatchChunkSizeSamples = 256;

struct BatchManifest {
    QStringList conversationFiles;
    QStringList musicFiles;
    QString outputDir;
    float mixGainDb = -18.0f;
    float conversationGainDb = 0.0f;
};

struct LoadedAudioFile {
    QString absolutePath;
    QString fileStem;
    std::vector<float> samples48kMono;
};

float computeRms(const float* data, size_t count) {
    if (data == nullptr || count == 0) {
        return 0.0f;
    }

    double sumSquares = 0.0;
    for (size_t i = 0; i < count; ++i) {
        sumSquares += static_cast<double>(data[i]) * static_cast<double>(data[i]);
    }

    return static_cast<float>(std::sqrt(sumSquares / static_cast<double>(count)));
}

float dbToLinear(float gainDb) {
    return std::pow(10.0f, gainDb / 20.0f);
}

float clampAudio(float sample) {
    return std::clamp(sample, -1.0f, 1.0f);
}

QString sanitizeStem(const QString& stem) {
    QString out;
    out.reserve(stem.size());

    bool lastWasUnderscore = false;
    for (const QChar c : stem) {
        if (c.isLetterOrNumber()) {
            out.append(c.toLower());
            lastWasUnderscore = false;
        } else if (!lastWasUnderscore) {
            out.append('_');
            lastWasUnderscore = true;
        }
    }

    while (out.startsWith('_')) {
        out.remove(0, 1);
    }
    while (out.endsWith('_')) {
        out.chop(1);
    }

    if (out.isEmpty()) {
        out = QStringLiteral("untitled");
    }
    return out;
}

QString resolvePath(const QString& pathValue, const QDir& baseDir) {
    QFileInfo fileInfo(pathValue);
    if (fileInfo.isAbsolute()) {
        return QDir::cleanPath(pathValue);
    }
    return QDir::cleanPath(baseDir.filePath(pathValue));
}

bool parseFileArray(
    const QJsonObject& root,
    const QString& key,
    const QDir& baseDir,
    QStringList* output,
    QString* errorMessage
) {
    if (output == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Output array for key %1 is null.").arg(key);
        }
        return false;
    }

    if (!root.contains(key) || !root.value(key).isArray()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Manifest key '%1' is required and must be an array.").arg(key);
        }
        return false;
    }

    const QJsonArray fileArray = root.value(key).toArray();
    for (const QJsonValue& value : fileArray) {
        if (!value.isString()) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("Manifest key '%1' must contain only string paths.").arg(key);
            }
            return false;
        }
        output->append(resolvePath(value.toString(), baseDir));
    }
    return true;
}

bool loadManifest(const QString& configPath, BatchManifest* manifest, QString* errorMessage) {
    if (manifest == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Internal error: null manifest output.");
        }
        return false;
    }

    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to open batch config %1: %2").arg(configPath, file.errorString());
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Invalid JSON in batch config %1: %2")
                                .arg(configPath, parseError.errorString());
        }
        return false;
    }

    const QJsonObject root = document.object();
    const QDir configDir = QFileInfo(configPath).absoluteDir();

    if (!parseFileArray(root, QStringLiteral("conversation_files"), configDir, &manifest->conversationFiles, errorMessage)) {
        return false;
    }
    if (!parseFileArray(root, QStringLiteral("music_files"), configDir, &manifest->musicFiles, errorMessage)) {
        return false;
    }

    if (manifest->conversationFiles.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Manifest must include at least one conversation file.");
        }
        return false;
    }

    if (!root.contains("output_dir") || !root.value("output_dir").isString()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Manifest key 'output_dir' is required and must be a string.");
        }
        return false;
    }

    manifest->outputDir = resolvePath(root.value("output_dir").toString(), configDir);
    manifest->mixGainDb = static_cast<float>(root.value("mix_gain_db").toDouble(-18.0));
    manifest->conversationGainDb = static_cast<float>(root.value("conversation_gain_db").toDouble(0.0));

    return true;
}

QJsonObject createDetectorMetadata() {
    QJsonObject detector;
    detector.insert("library", QStringLiteral("aubio"));
    detector.insert("algorithm", QStringLiteral("yin"));
    detector.insert("sample_rate_hz", static_cast<int>(kCaptureSampleRateHz));
    detector.insert("window_size_samples", static_cast<int>(kDetectorWindowSize));
    detector.insert("hop_size_samples", static_cast<int>(kDetectorHopSize));
    detector.insert("silence_threshold_db", kDetectorSilenceDb);
    detector.insert("tolerance", kDetectorTolerance);
    detector.insert("valid_pitch_range_hz", QJsonArray{kMinValidPitchHz, kMaxValidPitchHz});
    detector.insert("min_hop_confidence", kMinHopConfidence);
    detector.insert("min_output_confidence", kMinOutputConfidence);
    return detector;
}

QJsonObject createApplicationMetadata() {
    QJsonObject application;
    application.insert("name", QCoreApplication::applicationName());
    application.insert("version", QCoreApplication::applicationVersion());
    application.insert("qt_version", QString::fromLatin1(qVersion()));
    application.insert("build_arch", QSysInfo::buildAbi());
    return application;
}

std::vector<float> buildMixedSamples(
    const std::vector<float>& conversation,
    const std::vector<float>* music,
    float conversationGainDb,
    float musicGainDb
) {
    std::vector<float> mixed;
    mixed.resize(conversation.size(), 0.0f);

    const float conversationGain = dbToLinear(conversationGainDb);
    const float musicGain = dbToLinear(musicGainDb);

    const bool hasMusic = (music != nullptr && !music->empty());
    const size_t musicSize = hasMusic ? music->size() : 0;

    for (size_t i = 0; i < conversation.size(); ++i) {
        float sample = conversation[i] * conversationGain;
        if (hasMusic) {
            sample += (*music)[i % musicSize] * musicGain;
        }
        mixed[i] = clampAudio(sample);
    }

    return mixed;
}

QJsonObject createRunSessionMetadata(
    const QString& conversationFilePath,
    const QString& musicFilePathOrEmpty,
    bool isBaseline,
    float conversationGainDb,
    float mixGainDb,
    int runIndex,
    qint64 runStartTimestampMs,
    qint64 runEndTimestampMs,
    quint64 totalSamplesProcessed
) {
    QJsonObject capture;
    capture.insert("backend", QStringLiteral("offline_wav_batch"));
    capture.insert("sample_rate_hz", static_cast<int>(kCaptureSampleRateHz));
    capture.insert("channels", 1);
    capture.insert("start_timestamp_ms", static_cast<double>(runStartTimestampMs));
    capture.insert("start_timestamp_iso", QDateTime::fromMSecsSinceEpoch(runStartTimestampMs).toString(Qt::ISODateWithMs));
    capture.insert("end_timestamp_ms", static_cast<double>(runEndTimestampMs));
    capture.insert("end_timestamp_iso", QDateTime::fromMSecsSinceEpoch(runEndTimestampMs).toString(Qt::ISODateWithMs));
    capture.insert("total_samples_processed", static_cast<double>(totalSamplesProcessed));

    QJsonObject batchRun;
    batchRun.insert("conversation_file", conversationFilePath);
    batchRun.insert("music_file", musicFilePathOrEmpty.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(musicFilePathOrEmpty));
    batchRun.insert("is_baseline", isBaseline);
    batchRun.insert("conversation_gain_db", conversationGainDb);
    batchRun.insert("mix_gain_db", mixGainDb);
    batchRun.insert("run_index", runIndex);

    QJsonObject session;
    session.insert("application", createApplicationMetadata());
    session.insert("capture", capture);
    session.insert("detector", createDetectorMetadata());
    session.insert("batch_run", batchRun);
    return session;
}

QString makeOutputFileName(const QString& conversationStem, const QString& musicStem, bool isBaseline) {
    if (isBaseline) {
        return QStringLiteral("conv_%1__baseline.json").arg(conversationStem);
    }
    return QStringLiteral("conv_%1__music_%2.json").arg(conversationStem, musicStem);
}

bool runPitchAnalysisAndExport(
    const std::vector<float>& samples48kMono,
    const QString& outputPath,
    qint64 runStartTimestampMs,
    const QJsonObject& sessionMetadata,
    QString* errorMessage
) {
    AnalysisSession session(AnalysisSessionLimits{0, 0, 0, 0});
    session.setDisplayConfig(10, 0, 50.0f, 1000.0f);

    PitchDetector detector(kCaptureSampleRateHz, kDetectorWindowSize, kDetectorHopSize);
    quint64 totalSamplesProcessed = 0;
    const qint64 runStart = runStartTimestampMs;

    for (size_t offset = 0; offset < samples48kMono.size(); offset += kBatchChunkSizeSamples) {
        const unsigned int chunkSize =
            static_cast<unsigned int>(std::min<size_t>(kBatchChunkSizeSamples, samples48kMono.size() - offset));
        const float* chunkPtr = samples48kMono.data() + offset;

        const quint64 chunkStartSampleIndex = totalSamplesProcessed;
        const qint64 chunkStartTs =
            runStart +
            static_cast<qint64>((static_cast<double>(chunkStartSampleIndex) * 1000.0) /
                                static_cast<double>(kCaptureSampleRateHz));
        const qint64 chunkCenterTs =
            runStart +
            static_cast<qint64>(((static_cast<double>(chunkStartSampleIndex) + (chunkSize / 2.0)) * 1000.0) /
                                static_cast<double>(kCaptureSampleRateHz));
        const quint64 chunkCenterSampleIndex = chunkStartSampleIndex + (chunkSize / 2);
        totalSamplesProcessed += static_cast<quint64>(chunkSize);

        session.addRawAudioChunk(chunkPtr, chunkSize, chunkStartTs, kCaptureSampleRateHz, chunkStartSampleIndex);

        const float pitch = detector.detectPitch(chunkPtr, chunkSize);
        const float confidence = detector.getConfidence();
        const float rms = computeRms(chunkPtr, chunkSize);
        session.addAnalysisFrame(chunkCenterTs, pitch, confidence, rms, chunkSize, chunkCenterSampleIndex);
        if (pitch > 0.0f) {
            session.addPitchPoint(pitch, confidence, chunkCenterTs);
        }
    }

    QString exportError;
    if (!session.exportToJsonFile(outputPath, sessionMetadata, &exportError)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to export %1: %2").arg(outputPath, exportError);
        }
        return false;
    }

    return true;
}
}

bool BatchExportRunner::runFromConfig(const QString& configPath, QString* errorMessage) const {
    BatchManifest manifest;
    if (!loadManifest(configPath, &manifest, errorMessage)) {
        return false;
    }

    if (!QDir().mkpath(manifest.outputDir)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to create output directory: %1").arg(manifest.outputDir);
        }
        return false;
    }

    const QString runFolderName =
        QStringLiteral("run_%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    const QString runOutputDir = QDir(manifest.outputDir).filePath(runFolderName);
    if (!QDir().mkpath(runOutputDir)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to create batch run directory: %1").arg(runOutputDir);
        }
        return false;
    }

    QTextStream(stdout) << "Batch export output: " << runOutputDir << "\n";

    std::vector<LoadedAudioFile> loadedMusic;
    loadedMusic.reserve(static_cast<size_t>(manifest.musicFiles.size()));
    for (const QString& musicPath : manifest.musicFiles) {
        LoadedAudioFile music;
        music.absolutePath = QFileInfo(musicPath).absoluteFilePath();
        music.fileStem = sanitizeStem(QFileInfo(musicPath).completeBaseName());

        QString decodeError;
        DecodedWavInfo sourceInfo;
        if (!OfflineWavReader::readMonoFloat48k(music.absolutePath, &music.samples48kMono, &sourceInfo, &decodeError)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("Failed to decode music file %1: %2").arg(music.absolutePath, decodeError);
            }
            return false;
        }
        loadedMusic.push_back(std::move(music));
    }

    int runIndex = 0;
    for (const QString& conversationPath : manifest.conversationFiles) {
        LoadedAudioFile conversation;
        conversation.absolutePath = QFileInfo(conversationPath).absoluteFilePath();
        conversation.fileStem = sanitizeStem(QFileInfo(conversationPath).completeBaseName());

        QString decodeError;
        DecodedWavInfo sourceInfo;
        if (!OfflineWavReader::readMonoFloat48k(
                conversation.absolutePath,
                &conversation.samples48kMono,
                &sourceInfo,
                &decodeError
            )) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("Failed to decode conversation file %1: %2")
                                    .arg(conversation.absolutePath, decodeError);
            }
            return false;
        }

        {
            const std::vector<float> baselineSamples = buildMixedSamples(
                conversation.samples48kMono,
                nullptr,
                manifest.conversationGainDb,
                manifest.mixGainDb
            );
            const qint64 runStartTimestampMs = QDateTime::currentMSecsSinceEpoch();
            const qint64 runEndTimestampMs = runStartTimestampMs +
                                             static_cast<qint64>((static_cast<double>(baselineSamples.size()) * 1000.0) /
                                                                 static_cast<double>(kCaptureSampleRateHz));
            const QJsonObject sessionMetadata = createRunSessionMetadata(
                conversation.absolutePath,
                QString(),
                true,
                manifest.conversationGainDb,
                manifest.mixGainDb,
                runIndex,
                runStartTimestampMs,
                runEndTimestampMs,
                static_cast<quint64>(baselineSamples.size())
            );

            QString outputFileName = makeOutputFileName(conversation.fileStem, QString(), true);
            QString outputPath = QDir(runOutputDir).filePath(outputFileName);
            if (QFileInfo::exists(outputPath)) {
                outputFileName =
                    QStringLiteral("conv_%1__baseline__run_%2.json")
                        .arg(conversation.fileStem, QString::number(runIndex).rightJustified(4, '0'));
                outputPath = QDir(runOutputDir).filePath(outputFileName);
            }

            QTextStream(stdout) << "[" << runIndex << "] baseline: " << outputFileName << "\n";
            QString runError;
            if (!runPitchAnalysisAndExport(
                    baselineSamples,
                    outputPath,
                    runStartTimestampMs,
                    sessionMetadata,
                    &runError
                )) {
                if (errorMessage != nullptr) {
                    *errorMessage = runError;
                }
                return false;
            }
            ++runIndex;
        }

        for (const LoadedAudioFile& music : loadedMusic) {
            const std::vector<float> mixedSamples = buildMixedSamples(
                conversation.samples48kMono,
                &music.samples48kMono,
                manifest.conversationGainDb,
                manifest.mixGainDb
            );
            const qint64 runStartTimestampMs = QDateTime::currentMSecsSinceEpoch();
            const qint64 runEndTimestampMs = runStartTimestampMs +
                                             static_cast<qint64>((static_cast<double>(mixedSamples.size()) * 1000.0) /
                                                                 static_cast<double>(kCaptureSampleRateHz));
            const QJsonObject sessionMetadata = createRunSessionMetadata(
                conversation.absolutePath,
                music.absolutePath,
                false,
                manifest.conversationGainDb,
                manifest.mixGainDb,
                runIndex,
                runStartTimestampMs,
                runEndTimestampMs,
                static_cast<quint64>(mixedSamples.size())
            );

            QString outputFileName = makeOutputFileName(conversation.fileStem, music.fileStem, false);
            QString outputPath = QDir(runOutputDir).filePath(outputFileName);
            if (QFileInfo::exists(outputPath)) {
                outputFileName =
                    QStringLiteral("conv_%1__music_%2__run_%3.json")
                        .arg(
                            conversation.fileStem,
                            music.fileStem,
                            QString::number(runIndex).rightJustified(4, '0')
                        );
                outputPath = QDir(runOutputDir).filePath(outputFileName);
            }

            QTextStream(stdout) << "[" << runIndex << "] mixed: " << outputFileName << "\n";
            QString runError;
            if (!runPitchAnalysisAndExport(mixedSamples, outputPath, runStartTimestampMs, sessionMetadata, &runError)) {
                if (errorMessage != nullptr) {
                    *errorMessage = runError;
                }
                return false;
            }
            ++runIndex;
        }
    }

    return true;
}
