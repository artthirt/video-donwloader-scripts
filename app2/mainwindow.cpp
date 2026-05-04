#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "ffmpegdecoder.h"

#include <QMimeData>
#include <QFileDialog>
#include <QMessageBox>
#include <QClipboard>
#include <QApplication>
#include <QRegularExpression>
#include <QFileInfo>
#include <QUrl>
#include <QScrollBar>
#include <QSettings>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(std::make_unique<Ui::MainWindow>())
    , m_decoder(new FFmpegDecoder(this))
{
    ui->setupUi(this);
    setupConnections();

    mCompleterOut.setModel(&mCompleterModel);
    ui->lineEditOutput->setCompleter(&mCompleterOut);
    
    // Setup decoder callbacks using Qt's signal/slot mechanism via invokeMethod
    m_decoder->onProgress = [this](const FFmpegDecoder::ProgressInfo &info) {
        QMetaObject::invokeMethod(this, [this, info]() {
            onProgress(info);
        }, Qt::QueuedConnection);
    };
    
    m_decoder->onLog = [this](const QString &msg) {
        QMetaObject::invokeMethod(this, [this, msg]() {
            onLog(msg);
        }, Qt::QueuedConnection);
    };
    
    m_decoder->onFinished = [this](bool success, const QString &msg) {
        QMetaObject::invokeMethod(this, [this, success, msg]() {
            onFinished(success, msg);
        }, Qt::QueuedConnection);
    };

    loadSettings();
}

MainWindow::~MainWindow()
{
    saveSettings();

    ui.reset();
}

void MainWindow::setupConnections()
{
    //connect(ui->lineEditUrl, &QLineEdit::textChanged, this, &MainWindow::onUrlTextChanged);
    connect(ui->lineEditUrl, &QComboBox::currentTextChanged, this, &MainWindow::onUrlTextChanged);
    connect(ui->btnBrowse, &QPushButton::clicked, this, &MainWindow::onBrowseClicked);
    connect(ui->checkBoxCopy, &QCheckBox::stateChanged, this, &MainWindow::onCopyToggled);
    connect(ui->btnStart, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(ui->btnCancel, &QPushButton::clicked, this, &MainWindow::onCancelClicked);
    connect(ui->btnClear, &QPushButton::clicked, this, &MainWindow::onClearClicked);
    connect(ui->tbtClearListUrls, &QToolButton::clicked, this, [this](){
        if(QMessageBox::information(nullptr, "Clear list or url", "Are you sure?",
                                     QMessageBox::Ok | QMessageBox::Cancel) == QMessageBox::Ok)
        {
            ui->lineEditUrl->clear();
        }
    });
}

void MainWindow::suggestFilename()
{
    QString url = ui->lineEditUrl->currentText().trimmed();
    if (url.isEmpty()) return;
    
    QUrl qurl(url);
    QString path = qurl.path();
    QFileInfo info(path);
    QString name = info.baseName();
    
    if (!name.isEmpty() && name != "index" && name != "playlist" 
        && name != "master" && name != "stream") {
        QString suggested = name + ".mp4";
        if (ui->lineEditOutput->currentText().isEmpty()) {
            ui->lineEditOutput->setCurrentText(suggested);
        }
    }
}

void MainWindow::onUrlTextChanged(const QString &text)
{
    suggestFilename();
}

void MainWindow::onPasteClicked()
{
    QClipboard *clipboard = QApplication::clipboard();
    QString text = clipboard->text();
    if (!text.isEmpty()) {
        ui->lineEditUrl->setCurrentText(text);
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
        ui->lineEditOutput->setCurrentText(filePath);
    }
}

void MainWindow::onCopyToggled(int state)
{
    bool copyEnabled = (state == Qt::Checked);
    ui->spinBoxCRF->setEnabled(!copyEnabled);
    ui->comboBoxPreset->setEnabled(!copyEnabled);
    ui->comboBoxAudioFilter->setEnabled(copyEnabled);
}

bool MainWindow::validateInputs()
{
    QString url = ui->lineEditUrl->currentText().trimmed();
    QString output = ui->lineEditOutput->currentText().trimmed();
    
    if (url.isEmpty()) {
        QMessageBox::warning(this, "Input Error", "Please enter a valid M3U8 URL");
        return false;
    }
    
    if (output.isEmpty()) {
        ui->lineEditOutput->setCurrentText("output.mp4");
    }
    
    return true;
}

void MainWindow::loadSettings()
{
    QSettings settings;

    auto listUrls = settings.value("list_urls").toStringList();
    ui->lineEditUrl->addItems(listUrls);

    auto listOuts = settings.value("list_outs").toStringList();
    ui->lineEditOutput->addItems(listOuts);
    mCompleterModel.setStringList(listOuts);
}

QStringList saveToSett(QSettings& settings, QComboBox *cb, QString key)
{
    QStringList list;
    for(int i = 0; i < cb->count(); ++i){
        list << cb->itemText(i);
    }
    settings.setValue(key, list);
    return list;
}

void MainWindow::saveSettings()
{
    QSettings settings;

    saveToSett(settings, ui->lineEditUrl, "list_urls");
    saveToSett(settings, ui->lineEditOutput, "list_outs");
}

void MainWindow::addUrlToHistory(const QString &url)
{
    QStringList list;
    for(int i = 0; i < ui->lineEditUrl->count(); ++i){
        list << ui->lineEditUrl->itemText(i);
    }
    if(list.contains(url)){
        return;
    }
    list << url;
    ui->lineEditUrl->clear();
    ui->lineEditUrl->addItems(list);
    QEventLoop lp;
    lp.processEvents();
}

void MainWindow::addOutToHistory(const QString &url)
{
    QStringList list;
    for(int i = 0; i < ui->lineEditOutput->count(); ++i){
        list << ui->lineEditOutput->itemText(i);
    }
    if(list.contains(url)){
        return;
    }
    list << url;
    ui->lineEditOutput->clear();
    ui->lineEditOutput->addItems(list);
    mCompleterModel.setStringList(list);
    QEventLoop lp;
    lp.processEvents();
}

void MainWindow::onStartClicked()
{
    if (!validateInputs()) return;
    
    QString url = ui->lineEditUrl->currentText().trimmed();
    QString output = ui->lineEditOutput->currentText().trimmed();
    addUrlToHistory(url);
    addOutToHistory(output);
    saveSettings();
    
    ui->plainTextEditLog->clear();
    ui->plainTextEditLog->appendPlainText(QString("Input: %1\n").arg(url));
    ui->plainTextEditLog->appendPlainText(QString("Output: %1\n").arg(output));
    ui->plainTextEditLog->appendPlainText("Initializing FFmpeg...\n");
    
    // Initialize decoder
    if (!m_decoder->initialize(url, output)) {
        QMessageBox::critical(this, "Error", "Failed to initialize FFmpeg decoder");
        return;
    }
    
    ui->progressBar->setValue(0);
    ui->btnStart->setEnabled(false);
    ui->btnCancel->setEnabled(true);
    ui->lineEditUrl->setEnabled(false);
    ui->lineEditOutput->setEnabled(false);
    
    m_decoder->start();
}

void MainWindow::onCancelClicked()
{
    ui->statusBar->showMessage("Cancelling...");
    m_decoder->stop();
}

void MainWindow::onClearClicked()
{
    ui->plainTextEditLog->clear();
}

void MainWindow::onProgress(const FFmpegDecoder::ProgressInfo &info)
{
    if (info.totalDuration > 0) {
        int percent = qMin(100, int((info.currentTime / info.totalDuration) * 100));
        ui->progressBar->setValue(percent);
        ui->statusBar->showMessage(QString("Downloading: %1% (Frame %2, %3fps)")
            .arg(percent)
            .arg(info.frameCount)
            .arg(info.fps, 0, 'f', 1));
    } else {
        ui->statusBar->showMessage(QString("Downloading... Frame %1").arg(info.frameCount));
    }
}

void MainWindow::onLog(const QString &text)
{
    ui->plainTextEditLog->appendPlainText(text + "\n");
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
}

void MainWindow::on_tbtPastFromClipboard_clicked()
{
    auto clip = qApp->clipboard();
    auto md = clip->mimeData();
    if(md->hasText()){
        ui->lineEditUrl->setCurrentText(md->text());
    }
}