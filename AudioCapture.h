#ifndef AUDIOCAPTURE_H
#define AUDIOCAPTURE_H

#include <QObject>
#include <QThread>
#include <QVector>
#include <memory>

class AudioCaptureBackend : public QThread {
    Q_OBJECT

public:
    explicit AudioCaptureBackend(QObject* parent = nullptr) : QThread(parent) {}
    ~AudioCaptureBackend() override = default;

    virtual bool start(unsigned int sampleRate = 44100) = 0;
    virtual void stop() = 0;

signals:
    void audioDataReady(const QVector<float>& data);
    void error(const QString& message);
};

class AudioCapture : public QObject {
    Q_OBJECT

public:
    explicit AudioCapture(QObject* parent = nullptr);
    ~AudioCapture() override;

    bool start(unsigned int sampleRate = 44100);
    void stop();

signals:
    void audioDataReady(const QVector<float>& data);
    void error(const QString& message);

private:
    std::unique_ptr<AudioCaptureBackend> backend_;
};

#endif // AUDIOCAPTURE_H
