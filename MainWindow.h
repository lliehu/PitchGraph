#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QCloseEvent>
#include <QPushButton>
#include <QtGlobal>
#include <QVBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QSlider>
#include <QToolButton>
#include "PitchGraphWidget.h"
#include "AudioCapture.h"
#include "PitchDetector.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onStartStopClicked();
    void onExportClicked();
    void onAudioDataReady(const QVector<float>& data);
    void onAudioError(const QString& message);
    void onStayOnTopToggled(bool enabled);
    void onHideWindowFrameToggled(bool enabled);
    void onTransparencyChanged(int value);

private:
    void setupUi();
    void updateControlsBarVisibility();
    void updateControlsBarGeometry();
    void loadSettings();
    void saveSettings() const;

    PitchGraphWidget* graphWidget_;
    QPushButton* startStopButton_;
    QPushButton* exportButton_;
    QWidget* controlsBarWidget_;
    QToolButton* advancedControlsToggleButton_;
    QWidget* advancedControlsWidget_;
    QCheckBox* stayOnTopCheckBox_;
    QCheckBox* hideWindowFrameCheckBox_;
    QSlider* transparencySlider_;
    QLabel* transparencyValueLabel_;

    AudioCapture* audioCapture_;
    PitchDetector* pitchDetector_;

    bool isCapturing_;
    bool isLoadingSettings_;
    qint64 captureStartTimestampMs_;
    quint64 totalSamplesProcessed_;
};

#endif // MAINWINDOW_H
