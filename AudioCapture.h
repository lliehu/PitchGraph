#ifndef AUDIOCAPTURE_H
#define AUDIOCAPTURE_H

#include <QThread>
#include <pulse/simple.h>
#include <pulse/error.h>
#include <atomic>

class AudioCapture : public QThread {
    Q_OBJECT

public:
    explicit AudioCapture(QObject* parent = nullptr);
    ~AudioCapture() override;

    bool start(unsigned int sampleRate = 44100);
    void stop();

signals:
    void audioDataReady(const float* data, unsigned int size);
    void error(const QString& message);

protected:
    void run() override;

private:
    pa_simple* pulseAudio_;
    pa_sample_spec sampleSpec_;
    std::atomic<bool> running_;
    unsigned int bufferSize_;
};

#endif // AUDIOCAPTURE_H
