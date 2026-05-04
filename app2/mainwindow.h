#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QCompleter>
#include <QStringListModel>
#include <memory>

#include "ffmpegdecoder.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class FFmpegDecoder;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onUrlTextChanged(const QString &text);
    void onPasteClicked();
    void onBrowseClicked();
    void onCopyToggled(int state);
    void onStartClicked();
    void onCancelClicked();
    void onClearClicked();
    void onProgress(const FFmpegDecoder::ProgressInfo &info);
    void onLog(const QString &text);
    void onFinished(bool success, const QString &message);

    void on_tbtPastFromClipboard_clicked();

private:
    void setupConnections();
    void suggestFilename();
    bool validateInputs();

    void loadSettings();
    void saveSettings();

    void addUrlToHistory(const QString& url);
    void addOutToHistory(const QString& url);

    std::unique_ptr<Ui::MainWindow> ui;
    FFmpegDecoder *m_decoder{};

    QCompleter mCompleterOut;
    QStringListModel mCompleterModel;
};

#endif // MAINWINDOW_H