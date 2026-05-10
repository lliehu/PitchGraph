#include "OfflineWavReader.h"

#include <QByteArray>
#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {
constexpr unsigned int kTargetSampleRateHz = 48000;

bool readLe16(const QByteArray& bytes, qsizetype offset, quint16* out) {
    if (out == nullptr || offset < 0 || (offset + 2) > bytes.size()) {
        return false;
    }
    quint16 value = 0;
    std::memcpy(&value, bytes.constData() + offset, sizeof(value));
    *out = qFromLittleEndian(value);
    return true;
}

bool readLe32(const QByteArray& bytes, qsizetype offset, quint32* out) {
    if (out == nullptr || offset < 0 || (offset + 4) > bytes.size()) {
        return false;
    }
    quint32 value = 0;
    std::memcpy(&value, bytes.constData() + offset, sizeof(value));
    *out = qFromLittleEndian(value);
    return true;
}

bool readChunkHeader(const QByteArray& bytes, qsizetype offset, char id[5], quint32* chunkSize) {
    if (offset < 0 || (offset + 8) > bytes.size() || chunkSize == nullptr) {
        return false;
    }
    std::memcpy(id, bytes.constData() + offset, 4);
    id[4] = '\0';
    return readLe32(bytes, offset + 4, chunkSize);
}

float clampAudio(float sample) {
    return std::clamp(sample, -1.0f, 1.0f);
}

std::vector<float> resampleLinear(const std::vector<float>& input, unsigned int inputSampleRateHz);

bool decodeAudioBufferToMono(
    const QAudioBuffer& buffer,
    std::vector<float>* monoSamples,
    unsigned int* bitsPerSample,
    QString* errorMessage
) {
    if (monoSamples == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Internal error: null mono sample buffer.");
        }
        return false;
    }

    const QAudioFormat format = buffer.format();
    const int channelCount = format.channelCount();
    const int frameCount = buffer.frameCount();
    if (channelCount <= 0 || frameCount < 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Decoder produced invalid audio format.");
        }
        return false;
    }

    const size_t channels = static_cast<size_t>(channelCount);
    const size_t frames = static_cast<size_t>(frameCount);
    monoSamples->reserve(monoSamples->size() + frames);

    switch (format.sampleFormat()) {
        case QAudioFormat::SampleFormat::UInt8: {
            if (bitsPerSample != nullptr) {
                *bitsPerSample = 8;
            }
            const quint8* data = buffer.constData<quint8>();
            for (size_t frame = 0; frame < frames; ++frame) {
                double mixed = 0.0;
                const size_t frameOffset = frame * channels;
                for (size_t channel = 0; channel < channels; ++channel) {
                    const quint8 sample = data[frameOffset + channel];
                    mixed += (static_cast<double>(sample) - 128.0) / 128.0;
                }
                monoSamples->push_back(clampAudio(static_cast<float>(mixed / static_cast<double>(channels))));
            }
            return true;
        }
        case QAudioFormat::SampleFormat::Int16: {
            if (bitsPerSample != nullptr) {
                *bitsPerSample = 16;
            }
            const qint16* data = buffer.constData<qint16>();
            for (size_t frame = 0; frame < frames; ++frame) {
                double mixed = 0.0;
                const size_t frameOffset = frame * channels;
                for (size_t channel = 0; channel < channels; ++channel) {
                    mixed += static_cast<double>(data[frameOffset + channel]) / 32768.0;
                }
                monoSamples->push_back(clampAudio(static_cast<float>(mixed / static_cast<double>(channels))));
            }
            return true;
        }
        case QAudioFormat::SampleFormat::Int32: {
            if (bitsPerSample != nullptr) {
                *bitsPerSample = 32;
            }
            const qint32* data = buffer.constData<qint32>();
            for (size_t frame = 0; frame < frames; ++frame) {
                double mixed = 0.0;
                const size_t frameOffset = frame * channels;
                for (size_t channel = 0; channel < channels; ++channel) {
                    mixed += static_cast<double>(data[frameOffset + channel]) / 2147483648.0;
                }
                monoSamples->push_back(clampAudio(static_cast<float>(mixed / static_cast<double>(channels))));
            }
            return true;
        }
        case QAudioFormat::SampleFormat::Float: {
            if (bitsPerSample != nullptr) {
                *bitsPerSample = 32;
            }
            const float* data = buffer.constData<float>();
            for (size_t frame = 0; frame < frames; ++frame) {
                double mixed = 0.0;
                const size_t frameOffset = frame * channels;
                for (size_t channel = 0; channel < channels; ++channel) {
                    float sample = data[frameOffset + channel];
                    if (!std::isfinite(sample)) {
                        sample = 0.0f;
                    }
                    mixed += static_cast<double>(sample);
                }
                monoSamples->push_back(clampAudio(static_cast<float>(mixed / static_cast<double>(channels))));
            }
            return true;
        }
        case QAudioFormat::SampleFormat::Unknown:
        default:
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("Unsupported decoder sample format for MP3 input.");
            }
            return false;
    }
}

bool decodeMp3MonoFloat48k(
    const QString& filePath,
    std::vector<float>* monoSamples48k,
    DecodedWavInfo* sourceInfo,
    QString* errorMessage
) {
    if (monoSamples48k == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Output sample buffer is null.");
        }
        return false;
    }

    QAudioDecoder decoder;
    decoder.setSource(QUrl::fromLocalFile(filePath));

    QEventLoop eventLoop;
    std::vector<float> decodedMono;
    QString decodeError;
    unsigned int sourceSampleRateHz = 0;
    unsigned int sourceChannels = 0;
    unsigned int sourceBitsPerSample = 0;

    QObject::connect(&decoder, &QAudioDecoder::bufferReady, [&]() {
        while (decoder.bufferAvailable()) {
            const QAudioBuffer buffer = decoder.read();
            if (!buffer.isValid()) {
                continue;
            }

            const QAudioFormat format = buffer.format();
            if (format.sampleRate() <= 0 || format.channelCount() <= 0) {
                decodeError = QStringLiteral("Decoder produced invalid MP3 audio format for %1").arg(filePath);
                decoder.stop();
                eventLoop.quit();
                return;
            }

            if (sourceSampleRateHz == 0) {
                sourceSampleRateHz = static_cast<unsigned int>(format.sampleRate());
                sourceChannels = static_cast<unsigned int>(format.channelCount());
            }

            if (!decodeAudioBufferToMono(buffer, &decodedMono, &sourceBitsPerSample, &decodeError)) {
                decoder.stop();
                eventLoop.quit();
                return;
            }
        }
    });

    QObject::connect(&decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), [&](QAudioDecoder::Error error) {
        if (error == QAudioDecoder::NoError) {
            return;
        }

        decodeError = decoder.errorString();
        if (decodeError.isEmpty()) {
            decodeError = QStringLiteral("Unknown MP3 decode error for %1").arg(filePath);
        }
        eventLoop.quit();
    });

    QObject::connect(&decoder, &QAudioDecoder::finished, [&]() {
        eventLoop.quit();
    });

    decoder.start();
    eventLoop.exec();

    if (!decodeError.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = decodeError;
        }
        return false;
    }

    if (decodedMono.empty() || sourceSampleRateHz == 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to decode MP3 audio samples from %1").arg(filePath);
        }
        return false;
    }

    *monoSamples48k = resampleLinear(decodedMono, sourceSampleRateHz);
    if (monoSamples48k->empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to resample MP3 samples from %1").arg(filePath);
        }
        return false;
    }

    if (sourceInfo != nullptr) {
        sourceInfo->sampleRateHz = sourceSampleRateHz;
        sourceInfo->channels = sourceChannels;
        sourceInfo->bitsPerSample = sourceBitsPerSample;
        sourceInfo->audioFormat = 0x0055; // MPEG Layer 3
    }

    return true;
}

float decodePcmSample(const char* samplePtr, unsigned int bitsPerSample) {
    if (samplePtr == nullptr) {
        return 0.0f;
    }

    switch (bitsPerSample) {
        case 8: {
            const quint8 v = static_cast<quint8>(samplePtr[0]);
            return (static_cast<float>(v) - 128.0f) / 128.0f;
        }
        case 16: {
            quint16 raw = 0;
            std::memcpy(&raw, samplePtr, sizeof(raw));
            const qint16 v = static_cast<qint16>(qFromLittleEndian(raw));
            return static_cast<float>(v) / 32768.0f;
        }
        case 24: {
            const quint8 b0 = static_cast<quint8>(samplePtr[0]);
            const quint8 b1 = static_cast<quint8>(samplePtr[1]);
            const quint8 b2 = static_cast<quint8>(samplePtr[2]);
            qint32 v = static_cast<qint32>((static_cast<quint32>(b2) << 24) | (static_cast<quint32>(b1) << 16) |
                                           (static_cast<quint32>(b0) << 8));
            v >>= 8;
            return static_cast<float>(v) / 8388608.0f;
        }
        case 32: {
            quint32 raw = 0;
            std::memcpy(&raw, samplePtr, sizeof(raw));
            const qint32 v = static_cast<qint32>(qFromLittleEndian(raw));
            return static_cast<float>(v) / 2147483648.0f;
        }
        default:
            return 0.0f;
    }
}

float decodeFloatSample(const char* samplePtr, unsigned int bitsPerSample) {
    if (samplePtr == nullptr) {
        return 0.0f;
    }

    if (bitsPerSample == 32) {
        quint32 raw = 0;
        std::memcpy(&raw, samplePtr, sizeof(raw));
        raw = qFromLittleEndian(raw);
        float value = 0.0f;
        std::memcpy(&value, &raw, sizeof(value));
        return std::isfinite(value) ? value : 0.0f;
    }
    if (bitsPerSample == 64) {
        quint64 raw = 0;
        std::memcpy(&raw, samplePtr, sizeof(raw));
        raw = qFromLittleEndian(raw);
        double value = 0.0;
        std::memcpy(&value, &raw, sizeof(value));
        if (!std::isfinite(value)) {
            return 0.0f;
        }
        return static_cast<float>(std::clamp(value, -1.0, 1.0));
    }
    return 0.0f;
}

std::vector<float> resampleLinear(const std::vector<float>& input, unsigned int inputSampleRateHz) {
    if (input.empty() || inputSampleRateHz == 0) {
        return {};
    }
    if (inputSampleRateHz == kTargetSampleRateHz) {
        return input;
    }

    const double ratio = static_cast<double>(kTargetSampleRateHz) / static_cast<double>(inputSampleRateHz);
    const size_t outputLength = std::max<size_t>(1, static_cast<size_t>(std::llround(input.size() * ratio)));
    std::vector<float> output(outputLength);

    for (size_t i = 0; i < outputLength; ++i) {
        const double sourcePos = static_cast<double>(i) * static_cast<double>(inputSampleRateHz) /
                                 static_cast<double>(kTargetSampleRateHz);
        const size_t left = static_cast<size_t>(std::floor(sourcePos));
        const size_t right = std::min(input.size() - 1, left + 1);
        const double frac = sourcePos - static_cast<double>(left);
        const double sample = (static_cast<double>(input[left]) * (1.0 - frac)) +
                              (static_cast<double>(input[right]) * frac);
        output[i] = clampAudio(static_cast<float>(sample));
    }

    return output;
}
}

bool OfflineWavReader::readMonoFloat48k(
    const QString& filePath,
    std::vector<float>* monoSamples48k,
    DecodedWavInfo* sourceInfo,
    QString* errorMessage
) {
    if (monoSamples48k == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Output sample buffer is null.");
        }
        return false;
    }

    const QString fileSuffix = QFileInfo(filePath).suffix().toLower();
    if (fileSuffix == QStringLiteral("mp3")) {
        return decodeMp3MonoFloat48k(filePath, monoSamples48k, sourceInfo, errorMessage);
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to open %1: %2").arg(filePath, file.errorString());
        }
        return false;
    }

    const QByteArray bytes = file.readAll();
    if (bytes.size() < 44) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Invalid WAV file (too small): %1").arg(filePath);
        }
        return false;
    }

    if (std::memcmp(bytes.constData(), "RIFF", 4) != 0 || std::memcmp(bytes.constData() + 8, "WAVE", 4) != 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unsupported WAV container (expected RIFF/WAVE): %1").arg(filePath);
        }
        return false;
    }

    qsizetype fmtChunkOffset = -1;
    qsizetype dataChunkOffset = -1;
    quint32 fmtChunkSize = 0;
    quint32 dataChunkSize = 0;

    qsizetype offset = 12;
    while ((offset + 8) <= bytes.size()) {
        char chunkId[5] = {};
        quint32 chunkSize = 0;
        if (!readChunkHeader(bytes, offset, chunkId, &chunkSize)) {
            break;
        }

        const qsizetype dataOffset = offset + 8;
        const qsizetype nextOffset = dataOffset + static_cast<qsizetype>(chunkSize) + (chunkSize % 2);
        if (nextOffset > bytes.size()) {
            break;
        }

        if (std::memcmp(chunkId, "fmt ", 4) == 0) {
            fmtChunkOffset = dataOffset;
            fmtChunkSize = chunkSize;
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            dataChunkOffset = dataOffset;
            dataChunkSize = chunkSize;
        }

        offset = nextOffset;
    }

    if (fmtChunkOffset < 0 || dataChunkOffset < 0 || fmtChunkSize < 16 || dataChunkSize == 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("WAV file is missing fmt/data chunks: %1").arg(filePath);
        }
        return false;
    }

    quint16 audioFormat = 0;
    quint16 channelCount = 0;
    quint32 sampleRate = 0;
    quint16 blockAlign = 0;
    quint16 bitsPerSample = 0;

    if (!readLe16(bytes, fmtChunkOffset + 0, &audioFormat) || !readLe16(bytes, fmtChunkOffset + 2, &channelCount) ||
        !readLe32(bytes, fmtChunkOffset + 4, &sampleRate) || !readLe16(bytes, fmtChunkOffset + 12, &blockAlign) ||
        !readLe16(bytes, fmtChunkOffset + 14, &bitsPerSample)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to parse WAV fmt chunk: %1").arg(filePath);
        }
        return false;
    }

    if (audioFormat == 0xFFFE) {
        if (fmtChunkSize < 40) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("Unsupported extensible WAV fmt chunk: %1").arg(filePath);
            }
            return false;
        }
        quint16 subFormatTag = 0;
        if (!readLe16(bytes, fmtChunkOffset + 24, &subFormatTag)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("Failed to parse extensible WAV subformat: %1").arg(filePath);
            }
            return false;
        }
        audioFormat = subFormatTag;
    }

    if (channelCount == 0 || sampleRate == 0 || blockAlign == 0 || bitsPerSample == 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Invalid WAV format values: %1").arg(filePath);
        }
        return false;
    }

    if (!(audioFormat == 1 || audioFormat == 3)) {
        if (errorMessage != nullptr) {
            *errorMessage =
                QStringLiteral("Unsupported WAV audio format %1 in %2 (supported: PCM=1, FLOAT=3)")
                    .arg(audioFormat)
                    .arg(filePath);
        }
        return false;
    }

    if ((audioFormat == 1 && !(bitsPerSample == 8 || bitsPerSample == 16 || bitsPerSample == 24 || bitsPerSample == 32)) ||
        (audioFormat == 3 && !(bitsPerSample == 32 || bitsPerSample == 64))) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unsupported WAV bit depth %1 for format %2 in %3")
                                .arg(bitsPerSample)
                                .arg(audioFormat)
                                .arg(filePath);
        }
        return false;
    }

    const unsigned int bytesPerFrame = blockAlign;
    if (bytesPerFrame == 0 || dataChunkSize < bytesPerFrame) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Invalid WAV data chunk size in %1").arg(filePath);
        }
        return false;
    }

    const size_t frameCount = static_cast<size_t>(dataChunkSize / bytesPerFrame);
    if (frameCount == 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("WAV file has no audio frames: %1").arg(filePath);
        }
        return false;
    }

    const unsigned int bytesPerSample = bitsPerSample / 8;
    if (bytesPerSample == 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Invalid WAV bits per sample in %1").arg(filePath);
        }
        return false;
    }

    std::vector<float> mono;
    mono.reserve(frameCount);
    const char* dataPtr = bytes.constData() + dataChunkOffset;
    const size_t channelCountSize = static_cast<size_t>(channelCount);

    for (size_t frame = 0; frame < frameCount; ++frame) {
        const char* framePtr = dataPtr + (frame * bytesPerFrame);
        double mixed = 0.0;
        for (size_t channel = 0; channel < channelCountSize; ++channel) {
            const char* samplePtr = framePtr + (channel * bytesPerSample);
            float sample = 0.0f;
            if (audioFormat == 1) {
                sample = decodePcmSample(samplePtr, bitsPerSample);
            } else {
                sample = decodeFloatSample(samplePtr, bitsPerSample);
            }
            mixed += static_cast<double>(sample);
        }
        mixed /= static_cast<double>(channelCountSize);
        mono.push_back(clampAudio(static_cast<float>(mixed)));
    }

    *monoSamples48k = resampleLinear(mono, sampleRate);
    if (monoSamples48k->empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to decode audio samples from %1").arg(filePath);
        }
        return false;
    }

    if (sourceInfo != nullptr) {
        sourceInfo->sampleRateHz = sampleRate;
        sourceInfo->channels = channelCount;
        sourceInfo->bitsPerSample = bitsPerSample;
        sourceInfo->audioFormat = audioFormat;
    }

    return true;
}
