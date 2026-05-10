#include "MainWindow.h"
#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QMouseEvent>
#include <QSettings>
#include <QShortcut>
#include <QSysInfo>
#include <QToolButton>
#include <QWindow>
#include <algorithm>
#include <cmath>

namespace {
constexpr unsigned int kCaptureSampleRateHz = 48000;
constexpr unsigned int kDetectorWindowSize = 2048;
constexpr unsigned int kDetectorHopSize = 256;
constexpr float kDetectorSilenceDb = -45.0f;
constexpr float kDetectorTolerance = 0.8f;
constexpr float kMinValidPitchHz = 70.0f;
constexpr float kMaxValidPitchHz = 500.0f;
constexpr float kMinHopConfidence = 0.22f;
constexpr float kMinOutputConfidence = 0.28f;

float computeRms(const QVector<float>& data) {
    if (data.isEmpty()) {
        return 0.0f;
    }

    double sumSquares = 0.0;
    for (const float sample : data) {
        sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
    }

    return static_cast<float>(std::sqrt(sumSquares / static_cast<double>(data.size())));
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), isCapturing_(false), isLoadingSettings_(false), isWindowDragActive_(false),
      captureStartTimestampMs_(0), captureStopTimestampMs_(0), totalSamplesProcessed_(0) {

    setWindowTitle("PitchGraph - Real-time Pitch Detection");
    resize(900, 600);

    // Initialize audio capture and pitch detector
    // Using 48000 Hz sample rate for better pitch detection accuracy
    // 2048/256 analysis is tuned for speech intonation tracking (pitch accent contour)
    audioCapture_ = new AudioCapture(this);
    pitchDetector_ = new PitchDetector(kCaptureSampleRateHz, kDetectorWindowSize, kDetectorHopSize);

    // Connect signals
    connect(audioCapture_, &AudioCapture::audioDataReady, this, &MainWindow::onAudioDataReady, Qt::QueuedConnection);
    connect(audioCapture_, &AudioCapture::error, this, &MainWindow::onAudioError);

    setupUi();
    loadSettings();
    qApp->installEventFilter(this);
    updateControlsBarGeometry();
    updateControlsBarVisibility();
}

MainWindow::~MainWindow() {
    saveSettings();
    qApp->removeEventFilter(this);
    if (isCapturing_) {
        audioCapture_->stop();
    }
    delete pitchDetector_;
}

Qt::Edges MainWindow::hitTestResizeEdges(const QPoint& globalPos) const {
    if (!windowFlags().testFlag(Qt::FramelessWindowHint)) {
        return {};
    }

    constexpr int resizeMarginPx = 8;
    const QRect frameRect = frameGeometry();
    if (!frameRect.adjusted(-resizeMarginPx, -resizeMarginPx, resizeMarginPx, resizeMarginPx).contains(globalPos)) {
        return {};
    }

    Qt::Edges edges;
    if (globalPos.x() <= frameRect.left() + resizeMarginPx) {
        edges |= Qt::LeftEdge;
    } else if (globalPos.x() >= frameRect.right() - resizeMarginPx) {
        edges |= Qt::RightEdge;
    }

    if (globalPos.y() <= frameRect.top() + resizeMarginPx) {
        edges |= Qt::TopEdge;
    } else if (globalPos.y() >= frameRect.bottom() - resizeMarginPx) {
        edges |= Qt::BottomEdge;
    }

    return edges;
}

void MainWindow::updateResizeCursor(const QPoint& globalPos) {
    const Qt::Edges edges = hitTestResizeEdges(globalPos);
    Qt::CursorShape cursorShape = Qt::ArrowCursor;

    if ((edges & Qt::TopEdge) && (edges & Qt::LeftEdge)) {
        cursorShape = Qt::SizeFDiagCursor;
    } else if ((edges & Qt::TopEdge) && (edges & Qt::RightEdge)) {
        cursorShape = Qt::SizeBDiagCursor;
    } else if ((edges & Qt::BottomEdge) && (edges & Qt::LeftEdge)) {
        cursorShape = Qt::SizeBDiagCursor;
    } else if ((edges & Qt::BottomEdge) && (edges & Qt::RightEdge)) {
        cursorShape = Qt::SizeFDiagCursor;
    } else if ((edges & Qt::LeftEdge) || (edges & Qt::RightEdge)) {
        cursorShape = Qt::SizeHorCursor;
    } else if ((edges & Qt::TopEdge) || (edges & Qt::BottomEdge)) {
        cursorShape = Qt::SizeVerCursor;
    }

    setCursor(cursorShape);
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
    controlsBarWidget_ = new QWidget(centralWidget);
    QHBoxLayout* controlsLayout = new QHBoxLayout(controlsBarWidget_);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
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
    stayOnTopCheckBox_ = new QCheckBox("On top", advancedControlsWidget_);
    connect(stayOnTopCheckBox_, &QCheckBox::toggled, this, &MainWindow::onStayOnTopToggled);
    advancedControlsLayout->addWidget(stayOnTopCheckBox_);

    // Hide native window frame toggle
    hideWindowFrameCheckBox_ = new QCheckBox("Hide frame", advancedControlsWidget_);
    connect(hideWindowFrameCheckBox_, &QCheckBox::toggled, this, &MainWindow::onHideWindowFrameToggled);
    advancedControlsLayout->addWidget(hideWindowFrameCheckBox_);

    // Opacity control (value shown in tooltip)
    transparencySlider_ = new QSlider(Qt::Horizontal, advancedControlsWidget_);
    transparencySlider_->setRange(0, 80);
    transparencySlider_->setValue(0);
    transparencySlider_->setSingleStep(1);
    transparencySlider_->setPageStep(10);
    transparencySlider_->setFixedWidth(140);
    transparencySlider_->setToolTip(QString("Opacity: %1%").arg(transparencySlider_->value()));

    connect(transparencySlider_, &QSlider::valueChanged, this, &MainWindow::onTransparencyChanged);

    advancedControlsLayout->addWidget(transparencySlider_);

    advancedControlsWidget_->setVisible(false);
    controlsLayout->addWidget(advancedControlsWidget_);

    connect(advancedControlsToggleButton_, &QToolButton::toggled, this, [this](bool visible) {
        advancedControlsWidget_->setVisible(visible);
        advancedControlsToggleButton_->setArrowType(visible ? Qt::LeftArrow : Qt::RightArrow);
        advancedControlsToggleButton_->setToolTip(visible ? "Hide extra controls" : "Show extra controls");
        updateControlsBarGeometry();
        saveSettings();
    });

    controlsLayout->addStretch(1);

    controlsBarWidget_->setVisible(false);

    setCentralWidget(centralWidget);

    auto* quitShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q), this);
    connect(quitShortcut, &QShortcut::activated, qApp, &QCoreApplication::quit);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    const QWidget* watchedWidget = qobject_cast<QWidget*>(watched);
    const bool shouldHandleWindowEvent =
        watched == this ||
        (watchedWidget != nullptr && watchedWidget->window() == this);

    if (!shouldHandleWindowEvent) {
        return QMainWindow::eventFilter(watched, event);
    }

    if (event != nullptr) {
        if (event->type() == QEvent::MouseMove || event->type() == QEvent::Enter || event->type() == QEvent::Leave) {
            updateResizeCursor(QCursor::pos());
        }

        if (event->type() == QEvent::MouseButtonPress) {
            const auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                const Qt::Edges resizeEdges = hitTestResizeEdges(mouseEvent->globalPosition().toPoint());
                if (resizeEdges != Qt::Edges()) {
                    QWindow* handle = windowHandle();
                    if (handle != nullptr && handle->startSystemResize(resizeEdges)) {
                        return true;
                    }
                }
            }
        }
    }

    if (watched == graphWidget_ && event != nullptr) {
        switch (event->type()) {
            case QEvent::MouseButtonPress: {
                const auto* mouseEvent = static_cast<QMouseEvent*>(event);
                if (mouseEvent->button() == Qt::LeftButton) {
                    QWindow* handle = windowHandle();
                    if (handle != nullptr && handle->startSystemMove()) {
                        return true;
                    }

                    isWindowDragActive_ = true;
                    windowDragOffset_ = mouseEvent->globalPosition().toPoint() - frameGeometry().topLeft();
                    return true;
                }
                break;
            }
            case QEvent::MouseMove: {
                if (!isWindowDragActive_) {
                    break;
                }

                const auto* mouseEvent = static_cast<QMouseEvent*>(event);
                if (!(mouseEvent->buttons() & Qt::LeftButton)) {
                    isWindowDragActive_ = false;
                    break;
                }

                move(mouseEvent->globalPosition().toPoint() - windowDragOffset_);
                return true;
            }
            case QEvent::MouseButtonRelease: {
                const auto* mouseEvent = static_cast<QMouseEvent*>(event);
                if (mouseEvent->button() == Qt::LeftButton && isWindowDragActive_) {
                    isWindowDragActive_ = false;
                    return true;
                }
                break;
            }
            default:
                break;
        }
    }

    if (event != nullptr) {
        switch (event->type()) {
            case QEvent::Enter:
            case QEvent::Leave:
            case QEvent::MouseMove:
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonRelease:
            case QEvent::Wheel:
                updateControlsBarGeometry();
                updateControlsBarVisibility();
                break;
            case QEvent::Show:
            case QEvent::Hide:
            case QEvent::Move:
            case QEvent::Resize:
            case QEvent::WindowActivate:
            case QEvent::WindowDeactivate:
                updateControlsBarGeometry();
                updateControlsBarVisibility();
                break;
            default:
                break;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::loadSettings() {
    isLoadingSettings_ = true;

    QSettings settings("PitchGraph", "PitchGraph");
    settings.beginGroup("MainWindow");

    const bool extraControlsHidden = settings.value("extraControlsHidden", true).toBool();
    const bool stayOnTop = settings.value("stayOnTop", false).toBool();
    const bool hideWindowFrame = settings.value("hideWindowFrame", false).toBool();
    const int opacityPercent = std::clamp(settings.value("opacityPercent", 0).toInt(), 0, 80);
    const QByteArray geometry = settings.value("geometry").toByteArray();

    advancedControlsToggleButton_->setChecked(!extraControlsHidden);
    stayOnTopCheckBox_->setChecked(stayOnTop);
    hideWindowFrameCheckBox_->setChecked(hideWindowFrame);
    transparencySlider_->setValue(opacityPercent);

    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }

    settings.endGroup();
    isLoadingSettings_ = false;
}

void MainWindow::saveSettings() const {
    if (isLoadingSettings_) {
        return;
    }

    QSettings settings("PitchGraph", "PitchGraph");
    settings.beginGroup("MainWindow");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("extraControlsHidden", !advancedControlsToggleButton_->isChecked());
    settings.setValue("stayOnTop", stayOnTopCheckBox_->isChecked());
    settings.setValue("hideWindowFrame", hideWindowFrameCheckBox_->isChecked());
    settings.setValue("opacityPercent", transparencySlider_->value());
    settings.endGroup();
}

void MainWindow::updateControlsBarVisibility() {
    if (controlsBarWidget_ != nullptr) {
        const QWidget* topLevelWidget = QApplication::topLevelAt(QCursor::pos());
        const bool shouldBeVisible = isVisible() && (topLevelWidget == this);
        controlsBarWidget_->setVisible(shouldBeVisible);
        if (shouldBeVisible) {
            controlsBarWidget_->raise();
        }
    }
}

void MainWindow::updateControlsBarGeometry() {
    if (controlsBarWidget_ != nullptr && graphWidget_ != nullptr) {
        constexpr int overlayMarginPx = 4;

        const QRect graphRect = graphWidget_->geometry();
        const int controlsBarHeight = controlsBarWidget_->sizeHint().height();
        const QRect targetGeometry(
            graphRect.left() + overlayMarginPx,
            graphRect.y() + graphRect.height() - controlsBarHeight - overlayMarginPx,
            std::max(0, graphRect.width() - (overlayMarginPx * 2)),
            controlsBarHeight
        );

        if (controlsBarWidget_->geometry() != targetGeometry) {
            controlsBarWidget_->setGeometry(targetGeometry);
        }
    }
}

void MainWindow::onStartStopClicked() {
    if (!isCapturing_) {
        // Start capturing
        if (audioCapture_->start(kCaptureSampleRateHz)) {
            isCapturing_ = true;
            captureStartTimestampMs_ = QDateTime::currentMSecsSinceEpoch();
            captureStopTimestampMs_ = 0;
            totalSamplesProcessed_ = 0;
            startStopButton_->setText("■");
            graphWidget_->setFrozen(false);
            graphWidget_->clear();
        } else {
            QMessageBox::critical(this, "Error", "Failed to start audio capture. Check your system audio device.");
        }
    } else {
        // Stop capturing
        isCapturing_ = false;
        audioCapture_->stop();
        captureStopTimestampMs_ = QDateTime::currentMSecsSinceEpoch();
        startStopButton_->setText("▶");
        graphWidget_->setFrozen(true, captureStopTimestampMs_);
    }
}

void MainWindow::onAudioDataReady(const QVector<float>& data) {
    if (!isCapturing_) {
        return;
    }

    const unsigned int chunkSize = static_cast<unsigned int>(data.size());
    if (chunkSize == 0) {
        return;
    }

    const quint64 chunkStartSampleIndex = totalSamplesProcessed_;
    const qint64 chunkStartTs =
        captureStartTimestampMs_ +
        static_cast<qint64>((static_cast<double>(chunkStartSampleIndex) * 1000.0) / kCaptureSampleRateHz);
    const qint64 chunkCenterTs =
        captureStartTimestampMs_ +
        static_cast<qint64>(((chunkStartSampleIndex + (chunkSize / 2.0)) * 1000.0) / kCaptureSampleRateHz);
    const quint64 chunkCenterSampleIndex = chunkStartSampleIndex + (chunkSize / 2);
    totalSamplesProcessed_ += static_cast<quint64>(chunkSize);

    // Add raw audio samples to waveform visualization and export buffer.
    graphWidget_->addAudioSamples(data.constData(), chunkSize, chunkStartTs, kCaptureSampleRateHz, chunkStartSampleIndex);

    // Detect pitch from audio data
    const float pitch = pitchDetector_->detectPitch(data.constData(), chunkSize);
    const float confidence = pitchDetector_->getConfidence();
    const float rms = computeRms(data);

    // Record every analysis frame, including unvoiced frames.
    graphWidget_->addAnalysisFrame(chunkCenterTs, pitch, confidence, rms, chunkSize, chunkCenterSampleIndex);

    // Update graph with detected pitch
    if (pitch > 0.0f) {
        graphWidget_->addPitchPoint(pitch, confidence, chunkCenterTs);
    }
}

void MainWindow::onExportClicked() {
    const QString fileName =
        QString("pitch_graph_export_%1.json").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    const QString filePath = QDir::current().filePath(fileName);

    const qint64 sessionEndTimestampMs =
        isCapturing_ ? QDateTime::currentMSecsSinceEpoch() : captureStopTimestampMs_;

    QJsonObject detector;
    detector.insert("library", QStringLiteral("aubio"));
    detector.insert("algorithm", QStringLiteral("yin"));
    detector.insert("sample_rate_hz", static_cast<int>(pitchDetector_->sampleRate()));
    detector.insert("window_size_samples", static_cast<int>(pitchDetector_->windowSize()));
    detector.insert("hop_size_samples", static_cast<int>(pitchDetector_->hopSize()));
    detector.insert("silence_threshold_db", kDetectorSilenceDb);
    detector.insert("tolerance", kDetectorTolerance);
    detector.insert("valid_pitch_range_hz", QJsonArray{kMinValidPitchHz, kMaxValidPitchHz});
    detector.insert("min_hop_confidence", kMinHopConfidence);
    detector.insert("min_output_confidence", kMinOutputConfidence);

    QJsonObject capture;
    capture.insert("backend", audioCapture_->backendName());
    capture.insert("sample_rate_hz", static_cast<int>(kCaptureSampleRateHz));
    capture.insert("channels", 1);
    capture.insert("start_timestamp_ms", static_cast<double>(captureStartTimestampMs_));
    capture.insert(
        "start_timestamp_iso",
        (captureStartTimestampMs_ > 0)
            ? QDateTime::fromMSecsSinceEpoch(captureStartTimestampMs_).toString(Qt::ISODateWithMs)
            : QString()
    );
    capture.insert("end_timestamp_ms", static_cast<double>(sessionEndTimestampMs));
    capture.insert(
        "end_timestamp_iso",
        (sessionEndTimestampMs > 0)
            ? QDateTime::fromMSecsSinceEpoch(sessionEndTimestampMs).toString(Qt::ISODateWithMs)
            : QString()
    );
    capture.insert("total_samples_processed", static_cast<double>(totalSamplesProcessed_));

    QJsonObject application;
    application.insert("name", QCoreApplication::applicationName());
    application.insert("version", QCoreApplication::applicationVersion());
    application.insert("qt_version", QString::fromLatin1(qVersion()));
    application.insert("build_arch", QSysInfo::buildAbi());

    QJsonObject sessionMetadata;
    sessionMetadata.insert("application", application);
    sessionMetadata.insert("capture", capture);
    sessionMetadata.insert("detector", detector);

    QString errorMessage;
    if (!graphWidget_->exportToJsonFile(filePath, sessionMetadata, &errorMessage)) {
        QMessageBox::critical(
            this,
            "Export Failed",
            QString("Could not export analysis data to:\n%1\n\nReason: %2").arg(filePath, errorMessage)
        );
        return;
    }

    QMessageBox::information(this, "Export Complete", QString("Analysis export written to:\n%1").arg(filePath));
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
    saveSettings();
}

void MainWindow::onHideWindowFrameToggled(bool enabled) {
    const bool wasVisible = isVisible();
    setWindowFlag(Qt::FramelessWindowHint, enabled);
    if (wasVisible) {
        show();
    }
    saveSettings();
}

void MainWindow::onTransparencyChanged(int value) {
    transparencySlider_->setToolTip(QString("Opacity: %1%").arg(value));
    const qreal opacity = 1.0 - (static_cast<qreal>(value) / 100.0);
    setWindowOpacity(opacity);
    saveSettings();
}
