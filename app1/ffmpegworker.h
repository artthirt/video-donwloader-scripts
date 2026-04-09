#ifndef FFMPEGWORKER_H
#define FFMPEGWORKER_H

#include <QThread>
#include <QProcess>
#include <QStringList>
#include <atomic>

class FFmpegWorker : public QThread
{
    Q_OBJECT

public:
    explicit FFmpegWorker(const QStringList &args, QObject *parent = nullptr);
    ~FFmpegWorker();

    void stop();

signals:
    void progress(int percent);
    void status(const QString &message);
    void log(const QString &text);
    void finished(bool success, const QString &message);

protected:
    void run() override;

private:
    double timeToSeconds(const QString &timeStr);
    void parseOutput(const QString &data);

    QStringList m_args;
    QProcess *m_process;
    std::atomic<bool> m_running;
    double m_durationSeconds;
    bool m_durationFound;
};

#endif // FFMPEGWORKER_H