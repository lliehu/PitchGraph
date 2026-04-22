#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
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

private:
    void setupUi();

    PitchGraphWidget* graphWidget_;
    QPushButton* startStopButton_;
    QPushButton* exportButton_;
    QLabel* statusLabel_;

    AudioCapture* audioCapture_;
    PitchDetector* pitchDetector_;

    bool isCapturing_;
};

#endif // MAINWINDOW_H
