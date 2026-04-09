#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "ffmpegworker.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QClipboard>
#include <QApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QFileInfo>
#include <QUrl>
#include <QScrollBar>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(std::make_unique<Ui::MainWindow>())
    , m_worker(nullptr)
{
    ui->setupUi(this);
    setupConnections();
    checkFFmpeg();
    
    // Set default preset
    ui->comboBoxPreset->setCurrentText("medium");
}

MainWindow::~MainWindow() = default;

void MainWindow::setupConnections()
{
    connect(ui->lineEditUrl, &QLineEdit::textChanged, this, &MainWindow::onUrlTextChanged);
    connect(ui->btnPaste, &QPushButton::clicked, this, &MainWindow::onPasteClicked);
    connect(ui->btnBrowse, &QPushButton::clicked, this, &MainWindow::onBrowseClicked);
    connect(ui->checkBoxCopy, &QCheckBox::stateChanged, this, &MainWindow::onCopyToggled);
    connect(ui->btnStart, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(ui->btnCancel, &QPushButton::clicked, this, &MainWindow::onCancelClicked);
    connect(ui->btnClear, &QPushButton::clicked, this, &MainWindow::onClearClicked);
}

QString MainWindow::findFFmpeg()
{
    // Check bundled first
    QString bundledPath = QApplication::applicationDirPath() + "/ffmpeg.exe";
    if (QFile::exists(bundledPath)) {
        return bundledPath;
    }
    
    // Check system PATH
    QProcess which;
    which.start("where", QStringList() << "ffmpeg");
    which.waitForFinished();
    QString output = QString::fromUtf8(which.readAllStandardOutput()).trimmed();
    if (!output.isEmpty()) {
        return output.split("\n").first().trimmed();
    }
    
    return QString();
}

void MainWindow::checkFFmpeg()
{
    m_ffmpegPath = findFFmpeg();
    
    if (m_ffmpegPath.isEmpty()) {
        QMessageBox::critical(this, "FFmpeg Not Found",
            "FFmpeg is not installed or not in PATH.\n\n"
            "Please install FFmpeg first:\n"
            "• Windows: Download from ffmpeg.org and add to PATH\n"
            "• macOS: brew install ffmpeg\n"
            "• Linux: sudo apt install ffmpeg");
        ui->btnStart->setEnabled(false);
    } else {
        // Get version
        QProcess proc;
        proc.start(m_ffmpegPath, QStringList() << "-version");
        proc.waitForFinished();
        QString version = QString::fromUtf8(proc.readAllStandardOutput());
        QString firstLine = version.split("\n").first();
        ui->statusBar->showMessage("FFmpeg: " + firstLine.left(60));
    }
}

void MainWindow::suggestFilename()
{
    QString url = ui->lineEditUrl->text().trimmed();
    if (url.isEmpty()) return;
    
    QUrl qurl(url);
    QString path = qurl.path();
    QFileInfo info(path);
    QString name = info.baseName();
    
    // Filter out common playlist names
    if (!name.isEmpty() && name != "index" && name != "playlist" 
        && name != "master" && name != "stream") {
        QString suggested = name + ".mp4";
        if (ui->lineEditOutput->text().isEmpty()) {
            ui->lineEditOutput->setText(suggested);
        }
    }
}

void MainWindow::onUrlTextChanged()
{
    suggestFilename();
}

void MainWindow::onPasteClicked()
{
    QClipboard *clipboard = QApplication::clipboard();
    QString text = clipboard->text();
    if (!text.isEmpty()) {
        ui->lineEditUrl->setText(text);
        suggestFilename();
    }
}

void MainWindow::onBrowseClicked()
{
    QString filePath = QFileDialog::getSaveFileName(this, 
        "Save MP4 File", QString(), "MP4 Files (*.mp4);;All Files (*)");
    
    if (!filePath.isEmpty()) {
        if (!filePath.endsWith(".mp4", Qt::CaseInsensitive)) {
            filePath += ".mp4";
        }
        ui->lineEditOutput->setText(filePath);
    }
}

void MainWindow::onCopyToggled(int state)
{
    bool copyEnabled = (state == Qt::Checked);
    ui->spinBoxCRF->setEnabled(!copyEnabled);
    ui->comboBoxPreset->setEnabled(!copyEnabled);
    ui->comboBoxAudioFilter->setEnabled(copyEnabled);
}

QStringList MainWindow::buildCommand()
{
    QString url = ui->lineEditUrl->text().trimmed();
    QString output = ui->lineEditOutput->text().trimmed();
    
    if (url.isEmpty()) {
        throw std::runtime_error("Please enter a valid M3U8 URL");
    }
    
    if (output.isEmpty()) {
        output = "output.mp4";
    }
    
    QStringList cmd;
    cmd << "-hide_banner" << "-nostdin" << "-stats";
    cmd << "-i" << url;
    
    if (ui->checkBoxCopy->isChecked()) {
        cmd << "-c" << "copy";
        if (ui->comboBoxAudioFilter->currentText().startsWith("aac_adtstoasc")) {
            cmd << "-bsf:a" << "aac_adtstoasc";
        }
    } else {
        cmd << "-c:v" << "libx264";
        cmd << "-crf" << QString::number(ui->spinBoxCRF->value());
        cmd << "-preset" << ui->comboBoxPreset->currentText();
        cmd << "-c:a" << "aac" << "-b:a" << "192k";
    }
    
    // Extra args
    QString extra = ui->lineEditExtra->text().trimmed();
    if (!extra.isEmpty()) {
        cmd << extra.split(" ", Qt::SkipEmptyParts);
    }
    
    cmd << "-y" << "-progress" << "pipe:2";  // Output progress to stderr
    cmd << output;
    
    return cmd;
}

void MainWindow::onStartClicked()
{
    try {
        QStringList args = buildCommand();
        
        ui->plainTextEditLog->clear();
        ui->plainTextEditLog->appendPlainText("Command: ffmpeg " + args.join(" ") + "\n");
        ui->plainTextEditLog->appendPlainText("Starting...\n");
        
        ui->progressBar->setValue(0);
        ui->btnStart->setEnabled(false);
        ui->btnCancel->setEnabled(true);
        ui->lineEditUrl->setEnabled(false);
        ui->lineEditOutput->setEnabled(false);
        
        m_worker = new FFmpegWorker(args, this);
        connect(m_worker, &FFmpegWorker::progress, this, &MainWindow::onProgress);
        connect(m_worker, &FFmpegWorker::status, this, &MainWindow::onStatus);
        connect(m_worker, &FFmpegWorker::log, this, &MainWindow::onLog);
        connect(m_worker, &FFmpegWorker::finished, this, &MainWindow::onFinished);
        
        m_worker->start();
        
    } catch (const std::exception &e) {
        QMessageBox::warning(this, "Input Error", e.what());
    }
}

void MainWindow::onCancelClicked()
{
    if (m_worker) {
        m_worker->stop();
        ui->statusBar->showMessage("Cancelling...");
    }
}

void MainWindow::onClearClicked()
{
    ui->plainTextEditLog->clear();
}

void MainWindow::onProgress(int percent)
{
    ui->progressBar->setValue(percent);
}

void MainWindow::onStatus(const QString &message)
{
    ui->statusBar->showMessage(message);
}

void MainWindow::onLog(const QString &text)
{
    ui->plainTextEditLog->insertPlainText(text);
    QScrollBar *sb = ui->plainTextEditLog->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void MainWindow::onFinished(bool success, const QString &message)
{
    ui->btnStart->setEnabled(true);
    ui->btnCancel->setEnabled(false);
    ui->lineEditUrl->setEnabled(true);
    ui->lineEditOutput->setEnabled(true);
    
    if (success) {
        ui->progressBar->setValue(100);
        QMessageBox::information(this, "Success", message);
    } else {
        ui->progressBar->setValue(0);
        QMessageBox::warning(this, "Download Status", message);
    }
    
    ui->statusBar->showMessage(message);
    
    if (m_worker) {
        m_worker->deleteLater();
        m_worker = nullptr;
    }
}