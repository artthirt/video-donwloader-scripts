#ifndef FFMPEGDECODER_H
#define FFMPEGDECODER_H

#include <QThread>
#include <QString>
#include <atomic>
#include <functional>

// Forward declarations for FFmpeg C structs
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct AVStream;
struct AVIOContext;

class FFmpegDecoder : public QThread
{
    Q_OBJECT

public:
    struct ProgressInfo {
        int64_t totalBytes = 0;
        int64_t downloadedBytes = 0;
        double currentTime = 0.0;
        double totalDuration = 0.0;
        int frameCount = 0;
        double fps = 0.0;
        int64_t bitrate = 0;
    };

    explicit FFmpegDecoder(QObject *parent = nullptr);
    ~FFmpegDecoder();

    bool initialize(const QString &inputUrl, const QString &outputPath);
    void stop();
    bool isRunning() const;

    // Callbacks for progress
    std::function<void(const ProgressInfo&)> onProgress;
    std::function<void(const QString&)> onLog;
    std::function<void(bool success, const QString& message)> onFinished;

protected:
    void run() override;

private:
    bool openInput();
    bool setupOutput();
    bool transcode();
    void cleanup();
    void log(const QString &message);
    double getDuration() const;
    QString errorString(int errnum) const;

    // Input
    QString m_inputUrl;
    AVFormatContext *m_inputCtx = nullptr;
    
    // Output
    QString m_outputPath;
    AVFormatContext *m_outputCtx = nullptr;
    AVIOContext *m_outputIO = nullptr;
    
    // Streams
    struct StreamContext {
        AVStream *inStream = nullptr;
        AVStream *outStream = nullptr;
        AVCodecContext *decoder = nullptr;
        AVCodecContext *encoder = nullptr;
        int streamIndex = -1;
        bool copyCodec = false;
    };
    std::vector<StreamContext> m_streams;
    
    // State
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_initialized{false};
    ProgressInfo m_progress;
    
    // Options
    bool m_copyStreams = true;
    int m_crf = 23;
    QString m_preset = "medium";
    QStringList m_extraArgs;
};

#endif // FFMPEGDECODER_H