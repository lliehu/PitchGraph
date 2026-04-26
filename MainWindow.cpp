#include "MainWindow.h"
#include <QDateTime>
#include <QDir>
#include <QMessageBox>

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

    // Create status label
    statusLabel_ = new QLabel("Status: Ready", this);
    statusLabel_->setStyleSheet("font-weight: bold; padding: 5px;");
    layout->addWidget(statusLabel_);

    // Create pitch graph widget
    graphWidget_ = new PitchGraphWidget(this);
    layout->addWidget(graphWidget_);

    // Create start/stop button
    startStopButton_ = new QPushButton("Start Capture", this);
    startStopButton_->setMinimumHeight(40);
    connect(startStopButton_, &QPushButton::clicked, this, &MainWindow::onStartStopClicked);
    layout->addWidget(startStopButton_);

    // Create export button
    exportButton_ = new QPushButton("Export", this);
    exportButton_->setMinimumHeight(40);
    connect(exportButton_, &QPushButton::clicked, this, &MainWindow::onExportClicked);
    layout->addWidget(exportButton_);

    // Add info label
    QLabel* infoLabel = new QLabel(
        "This application captures system audio and displays detected pitch in real-time.\n"
        "Start capture, then play system audio to see the pitch graph update.",
        this
    );
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: gray; font-size: 10px; padding: 5px;");
    layout->addWidget(infoLabel);

    setCentralWidget(centralWidget);
}

void MainWindow::onStartStopClicked() {
    if (!isCapturing_) {
        // Start capturing
        if (audioCapture_->start(48000)) {
            isCapturing_ = true;
            captureStartTimestampMs_ = QDateTime::currentMSecsSinceEpoch();
            totalSamplesProcessed_ = 0;
            startStopButton_->setText("Stop Capture");
            statusLabel_->setText("Status: Capturing...");
            statusLabel_->setStyleSheet("font-weight: bold; padding: 5px; color: green;");
            graphWidget_->clear();
        } else {
            QMessageBox::critical(this, "Error", "Failed to start audio capture. Check your system audio device.");
        }
    } else {
        // Stop capturing
        audioCapture_->stop();
        isCapturing_ = false;
        startStopButton_->setText("Start Capture");
        statusLabel_->setText("Status: Stopped");
        statusLabel_->setStyleSheet("font-weight: bold; padding: 5px; color: red;");
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
