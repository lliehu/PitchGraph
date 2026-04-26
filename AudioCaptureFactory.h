#ifndef AUDIOCAPTUREFACTORY_H
#define AUDIOCAPTUREFACTORY_H

#include <memory>

class AudioCaptureBackend;

std::unique_ptr<AudioCaptureBackend> createAudioCaptureBackend();

#endif // AUDIOCAPTUREFACTORY_H
