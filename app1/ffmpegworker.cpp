#include "ffmpegworker.h"
#include <QRegularExpression>

FFmpegWorker::FFmpegWorker(const QStringList &args, QObject *parent)
    : QThread(parent)
    , m_args(args)
    , m_process(nullptr)
    , m_running(true)
    , m_durationSeconds(0.0)
    , m_durationFound(false)
{
}

FFmpegWorker::~FFmpegWorker()
{
    stop();
    wait();
}

void FFmpegWorker::stop()
{
    m_running = false;
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(3000)) {
            m_process->kill();
        }
    }
}

double FFmpegWorker::timeToSeconds(const QString &timeStr)
{
    QStringList parts = timeStr.split(':');
    if (parts.size() != 3) return 0.0;
    
    bool ok;
    double hours = parts[0].toDouble(&ok);
    if (!ok) return 0.0;
    double minutes = parts[1].toDouble(&ok);
    if (!ok) return 0.0;
    double seconds = parts[2].toDouble(&ok);
    if (!ok) return 0.0;
    
    return hours * 3600.0 + minutes * 60.0 + seconds;
}

void FFmpegWorker::parseOutput(const QString &data)
{
    emit log(data);
    
    // Parse Duration
    if (!m_durationFound) {
        QRegularExpression reDuration(R"(Duration: (\d{2}):(\d{2}):(\d{2}\.\d{2}))");
        QRegularExpressionMatch match = reDuration.match(data);
        if (match.hasMatch()) {
            QString timeStr = match.captured(1) + ":" + match.captured(2) + ":" + match.captured(3);
            m_durationSeconds = timeToSeconds(timeStr);
            m_durationFound = true;
            emit status(QString("Duration: %1 (%2s)").arg(timeStr).arg(int(m_durationSeconds)));
            emit log("[INFO] Detected duration: " + timeStr + "\n");
        }
    }
    
    // Parse Progress
    if (m_durationSeconds > 0) {
        QRegularExpression reTime(R"(time=(\d{2}):(\d{2}):(\d{2}\.\d{2}))");
        QRegularExpressionMatch match = reTime.match(data);
        if (match.hasMatch()) {
            QString currentTimeStr = match.captured(1) + ":" + match.captured(2) + ":" + match.captured(3);
            double currentSeconds = timeToSeconds(currentTimeStr);
            int percent = qMin(int((currentSeconds / m_durationSeconds) * 100), 99);
            emit progress(percent);
            emit status(QString("Downloading: %1% (%2)").arg(percent).arg(currentTimeStr));
        }
    } else {
        // Live stream - show frame/size
        QRegularExpression reFrame(R"(frame=\s*(\d+))");
        QRegularExpression reSize(R"(size=\s*(\d+)kB)");
        QRegularExpression reFps(R"(fps=\s*(\d+))");
        
        auto frameMatch = reFrame.match(data);
        auto sizeMatch = reSize.match(data);
        auto fpsMatch = reFps.match(data);
        
        if (frameMatch.hasMatch() || sizeMatch.hasMatch()) {
            QStringList info;
            if (frameMatch.hasMatch()) {
                info << QString("Frame %1").arg(frameMatch.captured(1));
            }
            if (fpsMatch.hasMatch()) {
                info << QString("%1fps").arg(fpsMatch.captured(1));
            }
            if (sizeMatch.hasMatch()) {
                double mb = sizeMatch.captured(1).toDouble() / 1024.0;
                info << QString("%1MB").arg(mb, 0, 'f', 1);
            }
            emit status("Downloading... " + info.join(" | "));
        }
    }
}

void FFmpegWorker::run()
{
    emit status("Starting FFmpeg...");
    
    m_process = new QProcess();
    
    // FFmpeg outputs progress to stderr by default
    connect(m_process, &QProcess::readyReadStandardError, [this]() {
        QByteArray data = m_process->readAllStandardError();
        parseOutput(QString::fromUtf8(data));
    });
    
    connect(m_process, &QProcess::readyReadStandardOutput, [this]() {
        QByteArray data = m_process->readAllStandardOutput();
        emit log(QString::fromUtf8(data));
    });
    
    QString program = "ffmpeg";
    m_process->start(program, m_args);
    
    if (!m_process->waitForStarted(5000)) {
        emit finished(false, "Failed to start FFmpeg. Is it installed and in PATH?");
        delete m_process;
        m_process = nullptr;
        return;
    }
    
    // Wait for completion
    while (m_process->state() != QProcess::NotRunning) {
        if (!m_running) {
            m_process->terminate();
            if (!m_process->waitForFinished(3000)) {
                m_process->kill();
            }
            emit finished(false, "Download cancelled by user");
            delete m_process;
            m_process = nullptr;
            return;
        }
        msleep(50);
    }
    
    int exitCode = m_process->exitCode();
    bool success = (exitCode == 0);
    
    if (success) {
        emit progress(100);
        emit finished(true, "Download completed successfully!");
    } else {
        emit finished(false, QString("FFmpeg exited with code %1").arg(exitCode));
    }
    
    delete m_process;
    m_process = nullptr;
}