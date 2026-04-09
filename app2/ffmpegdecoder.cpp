#include "ffmpegdecoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libavutil/log.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <QFile>
#include <QDebug>

// Custom Qt log callback for FFmpeg
static void qtLogCallback(void *ptr, int level, const char *fmt, va_list vl)
{
    if (level > av_log_get_level()) return;
    
    char line[1024];
    vsnprintf(line, sizeof(line), fmt, vl);
    
    // Remove trailing newline
    size_t len = strlen(line);
    if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
    
    // Emit through decoder instance if available
    // For simplicity, we use a static callback or queue
    QString msg = QString::fromUtf8(line).trimmed();
    if (!msg.isEmpty()) {
        qDebug() << "[FFmpeg]" << msg;
    }
}

FFmpegDecoder::FFmpegDecoder(QObject *parent)
    : QThread(parent)
{
    // Initialize FFmpeg once
    static bool initialized = false;
    if (!initialized) {
        av_log_set_callback(qtLogCallback);
        av_log_set_level(AV_LOG_INFO);
        initialized = true;
    }
}

FFmpegDecoder::~FFmpegDecoder()
{
    stop();
    wait();
    cleanup();
}

bool FFmpegDecoder::initialize(const QString &inputUrl, const QString &outputPath)
{
    m_inputUrl = inputUrl;
    m_outputPath = outputPath;
    m_initialized = false;
    
    if (!openInput()) {
        return false;
    }
    
    if (!setupOutput()) {
        cleanup();
        return false;
    }
    
    m_initialized = true;
    return true;
}

bool FFmpegDecoder::openInput()
{
    int ret = avformat_open_input(&m_inputCtx, m_inputUrl.toUtf8().constData(), nullptr, nullptr);
    if (ret < 0) {
        log(QString("Failed to open input: %1").arg(errorString(ret)));
        return false;
    }
    
    ret = avformat_find_stream_info(m_inputCtx, nullptr);
    if (ret < 0) {
        log(QString("Failed to find stream info: %1").arg(errorString(ret)));
        return false;
    }
    
    // Log input info
    log(QString("Input: %1").arg(m_inputUrl));
    if (m_inputCtx->duration > 0) {
        double duration = m_inputCtx->duration / (double)AV_TIME_BASE;
        log(QString("Duration: %1 seconds").arg(duration, 0, 'f', 2));
    }
    
    // Dump format info to string (for debugging)
    // char buf[4096];
    // AVIOContext *ioCtx = nullptr;
    // uint8_t *buffer = (uint8_t*)av_malloc(4096);
    // avio_open_dyn_buf(&ioCtx);
    // av_dump_format(m_inputCtx, 0, m_inputUrl.toUtf8().constData(), 0);
    // int size = avio_get_dyn_buf(ioCtx, &buffer);
    // if (size > 0 && size < 4096) {
    //     memcpy(buf, buffer, size);
    //     buf[size] = '\0';
    //     log(QString::fromUtf8(buf));
    // }
    // avio_closep(&ioCtx);
    // av_freep(buffer);
    
    return true;
}

bool FFmpegDecoder::setupOutput()
{
    int ret = avformat_alloc_output_context2(&m_outputCtx, nullptr, nullptr, 
                                             m_outputPath.toUtf8().constData());
    if (ret < 0) {
        log(QString("Failed to create output context: %1").arg(errorString(ret)));
        return false;
    }
    
    // Setup streams
    for (unsigned int i = 0; i < m_inputCtx->nb_streams; i++) {
        AVStream *inStream = m_inputCtx->streams[i];
        
        // Skip unknown streams
        if (inStream->codecpar->codec_id == AV_CODEC_ID_NONE) {
            continue;
        }
        
        AVStream *outStream = avformat_new_stream(m_outputCtx, nullptr);
        if (!outStream) {
            log("Failed to create output stream");
            return false;
        }
        
        StreamContext sc;
        sc.inStream = inStream;
        sc.outStream = outStream;
        sc.streamIndex = i;
        
        // Copy codec parameters
        ret = avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
        if (ret < 0) {
            log(QString("Failed to copy codec parameters: %1").arg(errorString(ret)));
            return false;
        }
        
        outStream->codecpar->codec_tag = 0;
        
        // Setup codec contexts if not copying
        if (!m_copyStreams) {
            const AVCodec *decoder = avcodec_find_decoder(inStream->codecpar->codec_id);
            if (!decoder) {
                log("Failed to find decoder");
                continue;
            }
            
            sc.decoder = avcodec_alloc_context3(decoder);
            avcodec_parameters_to_context(sc.decoder, inStream->codecpar);
            avcodec_open2(sc.decoder, decoder, nullptr);
            
            // Setup encoder (simplified - for full implementation, add specific encoders)
            // This is a basic example using libx264 for video, aac for audio
            // In production, you'd want more codec selection logic
        } else {
            sc.copyCodec = true;
        }
        
        // Copy time base
        outStream->time_base = inStream->time_base;
        
        m_streams.push_back(sc);
    }
    
    // Dump output format
    av_dump_format(m_outputCtx, 0, m_outputPath.toUtf8().constData(), 1);
    
    // Open output file
    if (!(m_outputCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&m_outputCtx->pb, m_outputPath.toUtf8().constData(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            log(QString("Failed to open output file: %1").arg(errorString(ret)));
            return false;
        }
    }
    
    // Write header
    ret = avformat_write_header(m_outputCtx, nullptr);
    if (ret < 0) {
        log(QString("Failed to write header: %1").arg(errorString(ret)));
        return false;
    }
    
    return true;
}

void FFmpegDecoder::run()
{
    if (!m_initialized) {
        if (onFinished) onFinished(false, "Not initialized");
        return;
    }
    
    m_running = true;
    
    bool success = transcode();
    
    // Write trailer
    if (m_outputCtx && m_running) {
        av_write_trailer(m_outputCtx);
    }
    
    m_running = false;
    
    if (success && onFinished) {
        onFinished(true, "Download completed successfully!");
    } else if (!success && onFinished) {
        onFinished(false, "Download failed or cancelled");
    }
}

bool FFmpegDecoder::transcode()
{
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    
    if (!pkt || !frame) {
        log("Failed to allocate packet/frame");
        return false;
    }
    
    int64_t lastProgressTime = av_gettime();
    
    while (m_running && av_read_frame(m_inputCtx, pkt) >= 0) {
        int streamIndex = pkt->stream_index;
        
        // Find our stream context
        StreamContext *sc = nullptr;
        for (auto &s : m_streams) {
            if (s.streamIndex == streamIndex) {
                sc = &s;
                break;
            }
        }
        
        if (!sc) {
            av_packet_unref(pkt);
            continue;
        }
        
        // Update progress
        if (sc->inStream->time_base.den > 0) {
            double pts = pkt->pts * av_q2d(sc->inStream->time_base);
            m_progress.currentTime = pts;
            m_progress.frameCount++;
        }
        
        // Copy packet
        pkt->pts = av_rescale_q_rnd(pkt->pts, sc->inStream->time_base, 
                                    sc->outStream->time_base, 
                                    AV_ROUND_NEAR_INF);
        pkt->dts = av_rescale_q_rnd(pkt->dts, sc->inStream->time_base,
                                    sc->outStream->time_base,
                                    AV_ROUND_NEAR_INF);
        pkt->duration = av_rescale_q(pkt->duration, sc->inStream->time_base,
                                     sc->outStream->time_base);
        pkt->pos = -1;
        pkt->stream_index = sc->outStream->index;
        
        int ret = av_interleaved_write_frame(m_outputCtx, pkt);
        if (ret < 0) {
            log(QString("Error writing frame: %1").arg(errorString(ret)));
        }
        
        av_packet_unref(pkt);
        
        // Update progress every 100ms
        int64_t now = av_gettime();
        if (now - lastProgressTime > 100000) { // 100ms in microseconds
            lastProgressTime = now;
            
            if (m_inputCtx->duration > 0) {
                m_progress.totalDuration = m_inputCtx->duration / (double)AV_TIME_BASE;
                int percent = qMin(100, int((m_progress.currentTime / m_progress.totalDuration) * 100));
                
                if (onProgress) {
                    onProgress(m_progress);
                }
            }
            
            if (onLog) {
                log(QString("Progress: %1 / %2 (%3 frames)")
                    .arg(m_progress.currentTime, 0, 'f', 2)
                    .arg(m_progress.totalDuration, 0, 'f', 2)
                    .arg(m_progress.frameCount));
            }
        }
    }
    
    av_packet_free(&pkt);
    av_frame_free(&frame);
    
    return m_running; // Return true if completed normally, false if cancelled
}

void FFmpegDecoder::stop()
{
    m_running = false;
}

bool FFmpegDecoder::isRunning() const
{
    return m_running;
}

void FFmpegDecoder::cleanup()
{
    for (auto &sc : m_streams) {
        if (sc.decoder) avcodec_free_context(&sc.decoder);
        if (sc.encoder) avcodec_free_context(&sc.encoder);
    }
    m_streams.clear();
    
    if (m_outputCtx) {
        if (!(m_outputCtx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&m_outputCtx->pb);
        }
        avformat_free_context(m_outputCtx);
        m_outputCtx = nullptr;
    }
    
    if (m_inputCtx) {
        avformat_close_input(&m_inputCtx);
        m_inputCtx = nullptr;
    }
}

void FFmpegDecoder::log(const QString &message)
{
    if (onLog) onLog(message);
}

QString FFmpegDecoder::errorString(int errnum) const
{
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, errbuf, AV_ERROR_MAX_STRING_SIZE);
    return QString::fromUtf8(errbuf);
}