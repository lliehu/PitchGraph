#include "WasapiAudioCapture.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <QString>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <mmreg.h>

namespace {
template <typename T>
void safeRelease(T*& instance) {
    if (instance) {
        instance->Release();
        instance = nullptr;
    }
}

QString formatHresult(HRESULT result) {
    return QString("0x%1").arg(static_cast<qulonglong>(static_cast<unsigned long>(result)), 8, 16, QLatin1Char('0'));
}

bool isFloatFormat(const WAVEFORMATEX* format) {
    if (!format) {
        return false;
    }
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        return format->wBitsPerSample == 32;
    }
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        return IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != 0;
    }
    return false;
}

bool isPcmFormat(const WAVEFORMATEX* format) {
    if (!format) {
        return false;
    }
    if (format->wFormatTag == WAVE_FORMAT_PCM) {
        return true;
    }
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        return IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) != 0;
    }
    return false;
}

float pcmToFloatSample(const unsigned char* sampleData, bool floatFormat, unsigned short bitsPerSample) {
    if (floatFormat && bitsPerSample == 32) {
        float value = 0.0f;
        std::memcpy(&value, sampleData, sizeof(float));
        if (std::isfinite(value)) {
            return std::clamp(value, -1.0f, 1.0f);
        }
        return 0.0f;
    }

    switch (bitsPerSample) {
    case 8: {
        const auto value = static_cast<int>(*sampleData) - 128;
        return static_cast<float>(value) / 128.0f;
    }
    case 16: {
        int16_t value = 0;
        std::memcpy(&value, sampleData, sizeof(value));
        return static_cast<float>(value) / 32768.0f;
    }
    case 24: {
        int32_t value = (static_cast<int32_t>(sampleData[0]) |
                        (static_cast<int32_t>(sampleData[1]) << 8) |
                        (static_cast<int32_t>(sampleData[2]) << 16));
        if ((value & 0x00800000) != 0) {
            value |= ~0x00FFFFFF;
        }
        return static_cast<float>(value) / 8388608.0f;
    }
    case 32: {
        int32_t value = 0;
        std::memcpy(&value, sampleData, sizeof(value));
        return static_cast<float>(value) / 2147483648.0f;
    }
    default:
        return 0.0f;
    }
}

bool convertToMono(
    const unsigned char* data,
    unsigned int frameCount,
    const WAVEFORMATEX* format,
    std::vector<float>& monoSamples
) {
    if (!data || !format || format->nChannels == 0 || format->wBitsPerSample == 0 || format->nBlockAlign == 0) {
        return false;
    }

    const bool floatFormat = isFloatFormat(format);
    const bool pcmFormat = isPcmFormat(format);
    if (!floatFormat && !pcmFormat) {
        return false;
    }

    const auto bytesPerSample = static_cast<unsigned short>(format->wBitsPerSample / 8);
    if (bytesPerSample == 0) {
        return false;
    }

    monoSamples.assign(frameCount, 0.0f);

    for (unsigned int frame = 0; frame < frameCount; ++frame) {
        const unsigned char* frameData = data + static_cast<size_t>(frame) * format->nBlockAlign;
        double monoValue = 0.0;
        for (unsigned short channel = 0; channel < format->nChannels; ++channel) {
            const unsigned char* sampleData = frameData + static_cast<size_t>(channel) * bytesPerSample;
            monoValue += pcmToFloatSample(sampleData, floatFormat, format->wBitsPerSample);
        }
        monoValue /= static_cast<double>(format->nChannels);
        monoSamples[frame] = std::clamp(static_cast<float>(monoValue), -1.0f, 1.0f);
    }

    return true;
}
}

WasapiAudioCapture::WasapiAudioCapture(QObject* parent)
    : AudioCaptureBackend(parent),
      running_(false),
      requestedSampleRate_(44100),
      deviceSampleRate_(44100),
      chunkSize_(1024),
      resampleIndex_(0.0),
      resampleStep_(1.0) {}

WasapiAudioCapture::~WasapiAudioCapture() {
    stop();
}

bool WasapiAudioCapture::start(unsigned int sampleRate) {
    if (running_) {
        return false;
    }

    if (sampleRate == 0) {
        emit error("Invalid sample rate requested for WASAPI capture.");
        return false;
    }

    requestedSampleRate_ = sampleRate;
    deviceSampleRate_ = sampleRate;
    outputSamples_.clear();
    resampleBuffer_.clear();
    resampleIndex_ = 0.0;
    resampleStep_ = 1.0;

    running_ = true;
    QThread::start();
    return true;
}

void WasapiAudioCapture::stop() {
    if (!running_ && !isRunning()) {
        return;
    }

    running_ = false;
    if (isRunning()) {
        QThread::wait();
    }
}

void WasapiAudioCapture::run() {
    const HRESULT initResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool shouldUninitialize = SUCCEEDED(initResult);
    if (FAILED(initResult) && initResult != RPC_E_CHANGED_MODE) {
        emit error(QString("Failed to initialize COM for WASAPI: %1").arg(formatHresult(initResult)));
        running_ = false;
        return;
    }

    IMMDeviceEnumerator* deviceEnumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* audioClient = nullptr;
    IAudioCaptureClient* captureClient = nullptr;
    WAVEFORMATEX* mixFormat = nullptr;

    do {
        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            reinterpret_cast<void**>(&deviceEnumerator)
        );
        if (FAILED(hr)) {
            emit error(QString("Failed to create device enumerator: %1").arg(formatHresult(hr)));
            break;
        }

        hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        if (FAILED(hr)) {
            emit error(QString("Failed to get default render device: %1").arg(formatHresult(hr)));
            break;
        }

        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&audioClient));
        if (FAILED(hr)) {
            emit error(QString("Failed to activate IAudioClient: %1").arg(formatHresult(hr)));
            break;
        }

        hr = audioClient->GetMixFormat(&mixFormat);
        if (FAILED(hr) || !mixFormat) {
            emit error(QString("Failed to read WASAPI mix format: %1").arg(formatHresult(hr)));
            break;
        }

        deviceSampleRate_ = mixFormat->nSamplesPerSec;
        if (requestedSampleRate_ == 0) {
            emit error("Invalid requested sample rate.");
            break;
        }
        resampleStep_ = static_cast<double>(deviceSampleRate_) / static_cast<double>(requestedSampleRate_);

        hr = audioClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK,
            0,
            0,
            mixFormat,
            nullptr
        );
        if (FAILED(hr)) {
            emit error(QString("Failed to initialize loopback stream: %1").arg(formatHresult(hr)));
            break;
        }

        hr = audioClient->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&captureClient));
        if (FAILED(hr)) {
            emit error(QString("Failed to get IAudioCaptureClient: %1").arg(formatHresult(hr)));
            break;
        }

        hr = audioClient->Start();
        if (FAILED(hr)) {
            emit error(QString("Failed to start WASAPI client: %1").arg(formatHresult(hr)));
            break;
        }

        while (running_) {
            UINT32 packetLength = 0;
            hr = captureClient->GetNextPacketSize(&packetLength);
            if (FAILED(hr)) {
                emit error(QString("Failed to query next packet size: %1").arg(formatHresult(hr)));
                break;
            }

            if (packetLength == 0) {
                QThread::msleep(5);
                continue;
            }

            while (packetLength > 0 && running_) {
                BYTE* data = nullptr;
                UINT32 frameCount = 0;
                DWORD flags = 0;

                hr = captureClient->GetBuffer(&data, &frameCount, &flags, nullptr, nullptr);
                if (FAILED(hr)) {
                    emit error(QString("Failed to read loopback packet: %1").arg(formatHresult(hr)));
                    running_ = false;
                    break;
                }

                std::vector<float> monoSamples;
                if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0) {
                    monoSamples.assign(frameCount, 0.0f);
                } else if (!convertToMono(data, frameCount, mixFormat, monoSamples)) {
                    emit error("Unsupported WASAPI mix format for conversion to float mono.");
                    running_ = false;
                }

                captureClient->ReleaseBuffer(frameCount);

                if (!monoSamples.empty() && running_) {
                    processSamples(monoSamples);
                }

                hr = captureClient->GetNextPacketSize(&packetLength);
                if (FAILED(hr)) {
                    emit error(QString("Failed to query packet size during capture: %1").arg(formatHresult(hr)));
                    running_ = false;
                    break;
                }
            }
        }

        audioClient->Stop();
    } while (false);

    running_ = false;

    if (mixFormat) {
        CoTaskMemFree(mixFormat);
        mixFormat = nullptr;
    }
    safeRelease(captureClient);
    safeRelease(audioClient);
    safeRelease(device);
    safeRelease(deviceEnumerator);

    if (shouldUninitialize) {
        CoUninitialize();
    }
}

void WasapiAudioCapture::processSamples(const std::vector<float>& samples) {
    if (deviceSampleRate_ == requestedSampleRate_) {
        enqueueOutputSamples(samples);
        return;
    }
    resampleAndQueue(samples);
}

void WasapiAudioCapture::resampleAndQueue(const std::vector<float>& samples) {
    if (samples.empty()) {
        return;
    }
    if (resampleStep_ <= std::numeric_limits<double>::epsilon()) {
        return;
    }

    resampleBuffer_.insert(resampleBuffer_.end(), samples.begin(), samples.end());
    std::vector<float> resampled;
    resampled.reserve(samples.size());

    while ((resampleIndex_ + 1.0) < static_cast<double>(resampleBuffer_.size())) {
        const size_t index = static_cast<size_t>(resampleIndex_);
        const double frac = resampleIndex_ - static_cast<double>(index);
        const float a = resampleBuffer_[index];
        const float b = resampleBuffer_[index + 1];
        resampled.push_back(static_cast<float>(a + (b - a) * frac));
        resampleIndex_ += resampleStep_;
    }

    const size_t consumed = static_cast<size_t>(resampleIndex_);
    if (consumed > 0 && consumed <= resampleBuffer_.size()) {
        resampleBuffer_.erase(resampleBuffer_.begin(), resampleBuffer_.begin() + static_cast<std::ptrdiff_t>(consumed));
        resampleIndex_ -= static_cast<double>(consumed);
    }

    enqueueOutputSamples(resampled);
}

void WasapiAudioCapture::enqueueOutputSamples(const std::vector<float>& samples) {
    if (samples.empty()) {
        return;
    }

    outputSamples_.insert(outputSamples_.end(), samples.begin(), samples.end());
    while (outputSamples_.size() >= chunkSize_) {
        const auto chunkEnd = outputSamples_.begin() + static_cast<std::ptrdiff_t>(chunkSize_);
        emit audioDataReady(QVector<float>(outputSamples_.begin(), chunkEnd));
        outputSamples_.erase(outputSamples_.begin(), chunkEnd);
    }
}
