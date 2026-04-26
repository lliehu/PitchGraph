#include "AudioCaptureFactory.h"
#include "AudioCapture.h"

#ifdef _WIN32
#include "WasapiAudioCapture.h"
#elif defined(__linux__)
#include "PulseAudioCapture.h"
#endif

std::unique_ptr<AudioCaptureBackend> createAudioCaptureBackend() {
#ifdef _WIN32
    return std::make_unique<WasapiAudioCapture>();
#elif defined(__linux__)
    return std::make_unique<PulseAudioCapture>();
#else
    return nullptr;
#endif
}
