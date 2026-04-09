#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <memory>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class FFmpegWorker;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onUrlTextChanged();
    void onPasteClicked();
    void onBrowseClicked();
    void onCopyToggled(int state);
    void onStartClicked();
    void onCancelClicked();
    void onClearClicked();
    void onProgress(int percent);
    void onStatus(const QString &message);
    void onLog(const QString &text);
    void onFinished(bool success, const QString &message);

private:
    void setupConnections();
    void suggestFilename();
    QStringList buildCommand();
    void checkFFmpeg();
    QString findFFmpeg();

    std::unique_ptr<Ui::MainWindow> ui;
    FFmpegWorker *m_worker;
    QString m_ffmpegPath;
};

#endif // MAINWINDOW_H