#include "MainWindow.h"
#include <QDateTime>
#include <QDir>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QToolButton>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), isCapturing_(false), captureStartTimestampMs_(0), totalSamplesProcessed_(0) {

    setWindowTitle("PitchGraph - Real-time Pitch Detection");
    resize(900, 600);

    // Initialize audio capture and pitch detector
    // Using 48000 Hz sample rate for better pitch detection accuracy
    // 2048/256 analysis is tuned for speech intonation tracking (pitch accent contour)
    audioCapture_ = new AudioCapture(this);
    pitchDetector_ = new PitchDetector(48000, 2048, 256);

    // Connect signals
    connect(audioCapture_, &AudioCapture::audioDataReady, this, &MainWindow::onAudioDataReady, Qt::QueuedConnection);
    connect(audioCapture_, &AudioCapture::error, this, &MainWindow::onAudioError);

    setupUi();
}

MainWindow::~MainWindow() {
    if (isCapturing_) {
        audioCapture_->stop();
    }
    delete pitchDetector_;
}

void MainWindow::setupUi() {
    // Create central widget and layout
    QWidget* centralWidget = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(centralWidget);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // Create pitch graph widget
    graphWidget_ = new PitchGraphWidget(this);
    layout->addWidget(graphWidget_, 1);

    // Create compact controls row
    QHBoxLayout* controlsLayout = new QHBoxLayout();
    controlsLayout->setSpacing(6);

    // Start/stop button
    startStopButton_ = new QPushButton("▶", this);
    startStopButton_->setFixedHeight(28);
    connect(startStopButton_, &QPushButton::clicked, this, &MainWindow::onStartStopClicked);
    controlsLayout->addWidget(startStopButton_);

    // Small toggle to show/hide advanced controls
    advancedControlsToggleButton_ = new QToolButton(this);
    advancedControlsToggleButton_->setCheckable(true);
    advancedControlsToggleButton_->setChecked(false);
    advancedControlsToggleButton_->setArrowType(Qt::RightArrow);
    advancedControlsToggleButton_->setToolTip("Show extra controls");
    advancedControlsToggleButton_->setFixedSize(22, 22);
    controlsLayout->addWidget(advancedControlsToggleButton_);

    // Advanced controls (hidden by default)
    advancedControlsWidget_ = new QWidget(this);
    QHBoxLayout* advancedControlsLayout = new QHBoxLayout(advancedControlsWidget_);
    advancedControlsLayout->setContentsMargins(0, 0, 0, 0);
    advancedControlsLayout->setSpacing(6);

    // Export button
    exportButton_ = new QPushButton("Export", advancedControlsWidget_);
    exportButton_->setFixedHeight(28);
    connect(exportButton_, &QPushButton::clicked, this, &MainWindow::onExportClicked);
    advancedControlsLayout->addWidget(exportButton_);

    // "Stay on top" toggle
    stayOnTopCheckBox_ = new QCheckBox("Stay on top", advancedControlsWidget_);
    connect(stayOnTopCheckBox_, &QCheckBox::toggled, this, &MainWindow::onStayOnTopToggled);
    advancedControlsLayout->addWidget(stayOnTopCheckBox_);

    // Opacity controls
    QLabel* transparencyLabel = new QLabel("Opacity:", advancedControlsWidget_);
    transparencySlider_ = new QSlider(Qt::Horizontal, advancedControlsWidget_);
    transparencySlider_->setRange(0, 80);
    transparencySlider_->setValue(0);
    transparencySlider_->setSingleStep(1);
    transparencySlider_->setPageStep(10);
    transparencySlider_->setFixedWidth(140);

    transparencyValueLabel_ = new QLabel("0%", advancedControlsWidget_);
    transparencyValueLabel_->setMinimumWidth(32);

    connect(transparencySlider_, &QSlider::valueChanged, this, &MainWindow::onTransparencyChanged);

    advancedControlsLayout->addWidget(transparencyLabel);
    advancedControlsLayout->addWidget(transparencySlider_);
    advancedControlsLayout->addWidget(transparencyValueLabel_);

    advancedControlsWidget_->setVisible(false);
    controlsLayout->addWidget(advancedControlsWidget_);

    connect(advancedControlsToggleButton_, &QToolButton::toggled, this, [this](bool visible) {
        advancedControlsWidget_->setVisible(visible);
        advancedControlsToggleButton_->setArrowType(visible ? Qt::LeftArrow : Qt::RightArrow);
        advancedControlsToggleButton_->setToolTip(visible ? "Hide extra controls" : "Show extra controls");
    });

    controlsLayout->addStretch(1);

    // Compact status text at the right of the controls row
    statusLabel_ = new QLabel("Ready", this);
    statusLabel_->setStyleSheet("font-weight: bold; color: #444;");
    controlsLayout->addWidget(statusLabel_);

    layout->addLayout(controlsLayout);

    setCentralWidget(centralWidget);
}

void MainWindow::onStartStopClicked() {
    if (!isCapturing_) {
        // Start capturing
        if (audioCapture_->start(48000)) {
            isCapturing_ = true;
            captureStartTimestampMs_ = QDateTime::currentMSecsSinceEpoch();
            totalSamplesProcessed_ = 0;
            startStopButton_->setText("■");
            statusLabel_->setText("Capturing");
            statusLabel_->setStyleSheet("font-weight: bold; color: green;");
            graphWidget_->clear();
        } else {
            QMessageBox::critical(this, "Error", "Failed to start audio capture. Check your system audio device.");
        }
    } else {
        // Stop capturing
        audioCapture_->stop();
        isCapturing_ = false;
        startStopButton_->setText("▶");
        statusLabel_->setText("Stopped");
        statusLabel_->setStyleSheet("font-weight: bold; color: red;");
    }
}

void MainWindow::onAudioDataReady(const QVector<float>& data) {
    constexpr unsigned int sampleRate = 48000;

    // Add raw audio samples to waveform visualization
    graphWidget_->addAudioSamples(data.constData(), static_cast<unsigned int>(data.size()));

    // Detect pitch from audio data
    float pitch = pitchDetector_->detectPitch(data.constData(), static_cast<unsigned int>(data.size()));
    float confidence = pitchDetector_->getConfidence();

    // Timestamp from sample clock to avoid UI-event jitter collapsing contour timing.
    const qint64 chunkCenterTs =
        captureStartTimestampMs_ +
        static_cast<qint64>(((totalSamplesProcessed_ + (data.size() / 2.0)) * 1000.0) / sampleRate);
    totalSamplesProcessed_ += static_cast<quint64>(data.size());

    // Update graph with detected pitch
    if (pitch > 0.0f) {
        graphWidget_->addPitchPoint(pitch, confidence, chunkCenterTs);
    }
}

void MainWindow::onExportClicked() {
    const QString fileName =
        QString("pitch_graph_export_%1.txt").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    const QString filePath = QDir::current().filePath(fileName);

    QString errorMessage;
    if (!graphWidget_->exportToTextFile(filePath, &errorMessage)) {
        QMessageBox::critical(
            this,
            "Export Failed",
            QString("Could not export pitch graph to:\n%1\n\nReason: %2").arg(filePath, errorMessage)
        );
        return;
    }

    QMessageBox::information(this, "Export Complete", QString("Pitch graph exported to:\n%1").arg(filePath));
}

void MainWindow::onAudioError(const QString& message) {
    QMessageBox::critical(this, "Audio Error", message);
    if (isCapturing_) {
        onStartStopClicked(); // Stop capture
    }
}

void MainWindow::onStayOnTopToggled(bool enabled) {
    const bool wasVisible = isVisible();
    setWindowFlag(Qt::WindowStaysOnTopHint, enabled);
    if (wasVisible) {
        show();
    }
}

void MainWindow::onTransparencyChanged(int value) {
    transparencyValueLabel_->setText(QString("%1%").arg(value));
    const qreal opacity = 1.0 - (static_cast<qreal>(value) / 100.0);
    setWindowOpacity(opacity);
}
