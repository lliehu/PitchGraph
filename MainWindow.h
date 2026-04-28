#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPushButton>
#include <QtGlobal>
#include <QVBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QSlider>
#include "PitchGraphWidget.h"
#include "AudioCapture.h"
#include "PitchDetector.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onStartStopClicked();
    void onExportClicked();
    void onAudioDataReady(const QVector<float>& data);
    void onAudioError(const QString& message);
    void onStayOnTopToggled(bool enabled);
    void onTransparencyChanged(int value);

private:
    void setupUi();

    PitchGraphWidget* graphWidget_;
    QPushButton* startStopButton_;
    QPushButton* exportButton_;
    QLabel* statusLabel_;
    QCheckBox* stayOnTopCheckBox_;
    QSlider* transparencySlider_;
    QLabel* transparencyValueLabel_;

    AudioCapture* audioCapture_;
    PitchDetector* pitchDetector_;

    bool isCapturing_;
    qint64 captureStartTimestampMs_;
    quint64 totalSamplesProcessed_;
};

#endif // MAINWINDOW_H
