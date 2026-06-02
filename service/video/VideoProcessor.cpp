#include "service/video/VideoProcessor.h"

#include "common/config/AppConfig.h"
#include "service/inference/YoloInference.h"
#include "service/video/VideoMetadataHelper.h"

#ifdef __cplusplus
extern "C"
{
#endif
// FFmpeg 的头文件是 C 风格接口，放在 extern "C" 里避免 C++ 名字改编导致链接失败。
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#ifdef __cplusplus
}
#endif

#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>

namespace
{
struct FrameExtractionResult
{
    int extracted_frame_count = 0;
    int total_detection_count = 0;
    std::string inference_model_name;
    std::string inference_framework;
    bool inference_model_file_exists = false;
    bool real_inference_ran = false;
    std::string inference_runtime_message;
    bool result_video_generated = false;
    std::string video_build_mode;
    std::uint64_t decode_receive_duration_ms = 0;
    std::uint64_t render_scale_duration_ms = 0;
    std::uint64_t inference_scale_duration_ms = 0;
    std::uint64_t inference_forward_duration_ms = 0;
    std::uint64_t inference_postprocess_duration_ms = 0;
    std::uint64_t inference_draw_duration_ms = 0;
    std::uint64_t encode_write_duration_ms = 0;
    bool cancelled = false;
};

void markTaskFailure(TaskEntity &task, const char *status, const std::string &error_message)
{
    task.status = status;
    task.error_message = error_message;
    if (task.result_summary.empty())
    {
        task.result_summary = error_message;
    }
}

bool shouldCancelTask(const std::function<bool()> &is_cancel_requested)
{
    return is_cancel_requested && is_cancel_requested();
}

bool pathExists(const std::string &path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool createDirectoryIfNeeded(const std::string &path)
{
    if (path.empty())
    {
        return false;
    }

    if (pathExists(path))
    {
        return true;
    }

    std::size_t pos = 0;
    while (pos < path.size())
    {
        pos = path.find('/', pos);
        std::string current = path.substr(0, pos);
        if (!current.empty() && !pathExists(current))
        {
            if (mkdir(current.c_str(), 0755) != 0 && errno != EEXIST)
            {
                return false;
            }
        }

        if (pos == std::string::npos)
        {
            break;
        }
        ++pos;
    }

    return pathExists(path);
}

long long getFileSize(const std::string &path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
    {
        return 0;
    }
    return static_cast<long long>(st.st_size);
}

std::uint64_t getAvailableDiskBytes(const std::string &path)
{
    struct statvfs stat {};
    if (statvfs(path.c_str(), &stat) != 0)
    {
        return 0;
    }
    return static_cast<std::uint64_t>(stat.f_bavail) * static_cast<std::uint64_t>(stat.f_frsize);
}

std::uint64_t estimateRequiredDiskBytesForTask(const TaskEntity &task)
{
    long long input_size = getFileSize(task.input_video_path);
    std::uint64_t safe_input_size = input_size > 0 ? static_cast<std::uint64_t>(input_size) : 64ULL * 1024ULL * 1024ULL;
    std::uint64_t estimated_output_bytes = safe_input_size * 2;
    return AppConfig::MIN_FREE_DISK_BYTES_FOR_TASK + estimated_output_bytes;
}

std::string buildOutputPath(const std::string &input_path, long long task_id)
{
    std::string file_name = input_path;
    std::size_t slash_pos = file_name.find_last_of("/\\");
    if (slash_pos != std::string::npos)
    {
        file_name = file_name.substr(slash_pos + 1);
    }

    std::size_t dot_pos = file_name.find_last_of('.');
    if (dot_pos != std::string::npos)
    {
        file_name = file_name.substr(0, dot_pos);
    }

    return std::string(AppConfig::VIDEO_OUTPUT_DIR) + "/" + file_name + "_task_" + std::to_string(task_id) + "_result.mp4";
}

bool copyFileBinary(const std::string &source_path, const std::string &target_path)
{
    std::ifstream input(source_path.c_str(), std::ios::binary);
    if (!input.is_open())
    {
        return false;
    }

    std::ofstream output(target_path.c_str(), std::ios::binary);
    if (!output.is_open())
    {
        return false;
    }

    output << input.rdbuf();
    output.flush();
    return output.good();
}

bool extractFrames(const std::string &path,
                   int frame_interval,
                   long long task_id,
                   double output_fps,
                   const std::string &output_path,
                   const InferenceModelContext &model_context,
                   std::string &encode_error_message,
                   FrameExtractionResult &result,
                   const std::function<bool()> &is_cancel_requested)
{
    if (frame_interval <= 0 || output_path.empty())
    {
        return false;
    }

    AVFormatContext *format_context = nullptr;
    AVCodecContext *codec_context = nullptr;
    SwsContext *sws_context = nullptr;
    SwsContext *inference_sws_context = nullptr;
    AVFrame *decoded_frame = nullptr;
    AVFrame *rgb_frame = nullptr;
    AVFrame *inference_rgb_frame = nullptr;
    AVPacket *packet = nullptr;
    AVFormatContext *output_format_context = nullptr;
    AVCodecContext *encoder_context = nullptr;
    SwsContext *encode_sws_context = nullptr;
    AVFrame *encode_frame = nullptr;
    AVPacket *encode_packet = nullptr;
    AVStream *output_stream = nullptr;

    auto cleanup = [&]()
    {
        av_packet_free(&encode_packet);
        av_frame_free(&encode_frame);
        if (encode_sws_context != nullptr)
        {
            sws_freeContext(encode_sws_context);
        }
        if (output_format_context != nullptr)
        {
            if ((output_format_context->oformat->flags & AVFMT_NOFILE) == 0)
            {
                avio_closep(&output_format_context->pb);
            }
            avformat_free_context(output_format_context);
        }
        avcodec_free_context(&encoder_context);
        av_freep(rgb_frame != nullptr ? &rgb_frame->data[0] : nullptr);
        av_packet_free(&packet);
        av_frame_free(&decoded_frame);
        av_frame_free(&rgb_frame);
        av_freep(inference_rgb_frame != nullptr ? &inference_rgb_frame->data[0] : nullptr);
        av_frame_free(&inference_rgb_frame);
        if (sws_context != nullptr)
        {
            sws_freeContext(sws_context);
        }
        if (inference_sws_context != nullptr)
        {
            sws_freeContext(inference_sws_context);
        }
        avcodec_free_context(&codec_context);
        if (format_context != nullptr)
        {
            avformat_close_input(&format_context);
        }
    };

    if (avformat_open_input(&format_context, path.c_str(), nullptr, nullptr) < 0)
    {
        return false;
    }

    if (avformat_find_stream_info(format_context, nullptr) < 0)
    {
        cleanup();
        return false;
    }

    int video_stream_index = av_find_best_stream(format_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_index < 0)
    {
        cleanup();
        return false;
    }

    AVStream *video_stream = format_context->streams[video_stream_index];
    const AVCodec *decoder = avcodec_find_decoder(video_stream->codecpar->codec_id);
    if (decoder == nullptr)
    {
        cleanup();
        return false;
    }

    codec_context = avcodec_alloc_context3(decoder);
    if (codec_context == nullptr)
    {
        cleanup();
        return false;
    }

    if (avcodec_parameters_to_context(codec_context, video_stream->codecpar) < 0)
    {
        cleanup();
        return false;
    }

    if (avcodec_open2(codec_context, decoder, nullptr) < 0)
    {
        cleanup();
        return false;
    }

    sws_context = sws_getContext(codec_context->width,
                                 codec_context->height,
                                 codec_context->pix_fmt,
                                 codec_context->width,
                                 codec_context->height,
                                 AV_PIX_FMT_RGB24,
                                 SWS_BILINEAR,
                                 nullptr,
                                 nullptr,
                                 nullptr);
    if (sws_context == nullptr)
    {
        cleanup();
        return false;
    }

    inference_sws_context = sws_getContext(codec_context->width,
                                           codec_context->height,
                                           codec_context->pix_fmt,
                                           model_context.input_width,
                                           model_context.input_height,
                                           AV_PIX_FMT_RGB24,
                                           SWS_BILINEAR,
                                           nullptr,
                                           nullptr,
                                           nullptr);
    if (inference_sws_context == nullptr)
    {
        cleanup();
        return false;
    }

    decoded_frame = av_frame_alloc();
    rgb_frame = av_frame_alloc();
    inference_rgb_frame = av_frame_alloc();
    packet = av_packet_alloc();
    if (decoded_frame == nullptr || rgb_frame == nullptr || inference_rgb_frame == nullptr || packet == nullptr)
    {
        cleanup();
        return false;
    }

    int rgb_buffer_size = av_image_alloc(rgb_frame->data,
                                         rgb_frame->linesize,
                                         codec_context->width,
                                         codec_context->height,
                                         AV_PIX_FMT_RGB24,
                                         1);
    if (rgb_buffer_size < 0)
    {
        cleanup();
        return false;
    }

    int inference_rgb_buffer_size = av_image_alloc(inference_rgb_frame->data,
                                                   inference_rgb_frame->linesize,
                                                   model_context.input_width,
                                                   model_context.input_height,
                                                   AV_PIX_FMT_RGB24,
                                                   1);
    if (inference_rgb_buffer_size < 0)
    {
        cleanup();
        return false;
    }

    double target_fps = output_fps > 0.0 ? output_fps : 25.0;
    AVRational target_fps_q = av_d2q(target_fps, 100000);
    if (target_fps_q.num <= 0 || target_fps_q.den <= 0)
    {
        target_fps_q.num = 25;
        target_fps_q.den = 1;
    }

    avformat_alloc_output_context2(&output_format_context, nullptr, nullptr, output_path.c_str());
    if (output_format_context == nullptr)
    {
        encode_error_message = "创建输出视频上下文失败";
        cleanup();
        return false;
    }

    const AVCodec *encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (encoder == nullptr)
    {
        encode_error_message = "找不到 H264 编码器";
        cleanup();
        return false;
    }

    output_stream = avformat_new_stream(output_format_context, nullptr);
    encoder_context = output_stream == nullptr ? nullptr : avcodec_alloc_context3(encoder);
    if (output_stream == nullptr || encoder_context == nullptr)
    {
        encode_error_message = "创建输出视频流失败";
        cleanup();
        return false;
    }

    encoder_context->codec_id = encoder->id;
    encoder_context->codec_type = AVMEDIA_TYPE_VIDEO;
    encoder_context->pix_fmt = AV_PIX_FMT_YUV420P;
    encoder_context->width = codec_context->width;
    encoder_context->height = codec_context->height;
    encoder_context->time_base = av_inv_q(target_fps_q);
    encoder_context->framerate = target_fps_q;
    encoder_context->gop_size = 12;
    encoder_context->max_b_frames = 2;
    encoder_context->bit_rate = static_cast<int64_t>(codec_context->width) *
                                static_cast<int64_t>(codec_context->height) * 4;
    if ((output_format_context->oformat->flags & AVFMT_GLOBALHEADER) != 0)
    {
        encoder_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    if (encoder->id == AV_CODEC_ID_H264)
    {
        av_opt_set(encoder_context->priv_data, "preset", "veryfast", 0);
    }
    if (avcodec_open2(encoder_context, encoder, nullptr) < 0)
    {
        encode_error_message = "打开输出视频编码器失败";
        cleanup();
        return false;
    }

    output_stream->time_base = encoder_context->time_base;
    if (avcodec_parameters_from_context(output_stream->codecpar, encoder_context) < 0)
    {
        encode_error_message = "拷贝编码参数失败";
        cleanup();
        return false;
    }

    if ((output_format_context->oformat->flags & AVFMT_NOFILE) == 0)
    {
        if (avio_open(&output_format_context->pb, output_path.c_str(), AVIO_FLAG_WRITE) < 0)
        {
            encode_error_message = "打开结果视频文件失败";
            cleanup();
            return false;
        }
    }

    if (avformat_write_header(output_format_context, nullptr) < 0)
    {
        encode_error_message = "写入结果视频头失败";
        cleanup();
        return false;
    }

    encode_sws_context = sws_getContext(codec_context->width,
                                        codec_context->height,
                                        AV_PIX_FMT_RGB24,
                                        codec_context->width,
                                        codec_context->height,
                                        AV_PIX_FMT_YUV420P,
                                        SWS_BILINEAR,
                                        nullptr,
                                        nullptr,
                                        nullptr);
    encode_frame = av_frame_alloc();
    encode_packet = av_packet_alloc();
    if (encode_sws_context == nullptr || encode_frame == nullptr || encode_packet == nullptr)
    {
        encode_error_message = "创建结果视频编码缓冲失败";
        cleanup();
        return false;
    }

    encode_frame->format = encoder_context->pix_fmt;
    encode_frame->width = encoder_context->width;
    encode_frame->height = encoder_context->height;
    if (av_frame_get_buffer(encode_frame, 32) < 0)
    {
        encode_error_message = "分配结果视频编码帧失败";
        cleanup();
        return false;
    }

    result.inference_model_name = model_context.model_name;
    result.inference_framework = model_context.framework;
    result.inference_model_file_exists = model_context.file_exists;

    int decoded_index = 0;
    int extracted_index = 0;
    int total_detection_count = 0;

    auto markCancelled = [&]()
    {
        result.cancelled = true;
        result.extracted_frame_count = extracted_index;
        result.total_detection_count = total_detection_count;
        encode_error_message = "任务已被用户取消";
    };

    auto writeEncodedPackets = [&]() -> bool
    {
        while (true)
        {
            int encode_result = avcodec_receive_packet(encoder_context, encode_packet);
            if (encode_result == AVERROR(EAGAIN) || encode_result == AVERROR_EOF)
            {
                return true;
            }
            if (encode_result < 0)
            {
                encode_error_message = "从编码器接收结果视频包失败";
                return false;
            }

            av_packet_rescale_ts(encode_packet, encoder_context->time_base, output_stream->time_base);
            encode_packet->stream_index = output_stream->index;
            if (av_interleaved_write_frame(output_format_context, encode_packet) < 0)
            {
                av_packet_unref(encode_packet);
                encode_error_message = "写入结果视频数据失败";
                return false;
            }
            av_packet_unref(encode_packet);
        }
    };

    auto flush_decoder = [&](bool draining) -> bool
    {
        while (true)
        {
            if (shouldCancelTask(is_cancel_requested))
            {
                markCancelled();
                return false;
            }

            auto receive_started_at = std::chrono::steady_clock::now();
            int receive_result = avcodec_receive_frame(codec_context, decoded_frame);
            auto receive_finished_at = std::chrono::steady_clock::now();
            result.decode_receive_duration_ms += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(receive_finished_at - receive_started_at).count());
            if (receive_result == AVERROR(EAGAIN) || receive_result == AVERROR_EOF)
            {
                return true;
            }
            if (receive_result < 0)
            {
                return false;
            }

            if (decoded_index % frame_interval == 0)
            {
                if (shouldCancelTask(is_cancel_requested))
                {
                    markCancelled();
                    return false;
                }

                auto render_scale_started_at = std::chrono::steady_clock::now();
                sws_scale(sws_context,
                          decoded_frame->data,
                          decoded_frame->linesize,
                          0,
                          codec_context->height,
                          rgb_frame->data,
                          rgb_frame->linesize);
                auto render_scale_finished_at = std::chrono::steady_clock::now();
                result.render_scale_duration_ms += static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(render_scale_finished_at - render_scale_started_at).count());

                auto inference_scale_started_at = std::chrono::steady_clock::now();
                sws_scale(inference_sws_context,
                          decoded_frame->data,
                          decoded_frame->linesize,
                          0,
                          codec_context->height,
                          inference_rgb_frame->data,
                          inference_rgb_frame->linesize);
                auto inference_scale_finished_at = std::chrono::steady_clock::now();
                result.inference_scale_duration_ms += static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(inference_scale_finished_at - inference_scale_started_at).count());

                FrameBuffer render_frame_buffer;
                render_frame_buffer.width = codec_context->width;
                render_frame_buffer.height = codec_context->height;
                render_frame_buffer.linesize = rgb_frame->linesize[0];
                render_frame_buffer.data = rgb_frame->data[0];

                FrameBuffer inference_frame_buffer;
                inference_frame_buffer.width = model_context.input_width;
                inference_frame_buffer.height = model_context.input_height;
                inference_frame_buffer.linesize = inference_rgb_frame->linesize[0];
                inference_frame_buffer.data = inference_rgb_frame->data[0];

                if (shouldCancelTask(is_cancel_requested))
                {
                    markCancelled();
                    return false;
                }

                InferenceResult inference_result;
                if (!YoloInference::processFrame(render_frame_buffer,
                                                inference_frame_buffer,
                                                model_context,
                                                inference_result))
                {
                    return false;
                }
                total_detection_count += inference_result.detection_count;
                result.inference_model_name = inference_result.model_name;
                result.inference_framework = inference_result.model_framework;
                result.inference_model_file_exists = model_context.file_exists;
                result.real_inference_ran = inference_result.real_inference_ran;
                result.inference_runtime_message = inference_result.runtime_message;
                result.inference_forward_duration_ms += inference_result.forward_duration_ms;
                result.inference_postprocess_duration_ms += inference_result.postprocess_duration_ms;
                result.inference_draw_duration_ms += inference_result.draw_duration_ms;

                if (shouldCancelTask(is_cancel_requested))
                {
                    markCancelled();
                    return false;
                }

                if (av_frame_make_writable(encode_frame) < 0)
                {
                    encode_error_message = "结果视频编码帧不可写";
                    return false;
                }

                auto encode_started_at = std::chrono::steady_clock::now();
                sws_scale(encode_sws_context,
                          rgb_frame->data,
                          rgb_frame->linesize,
                          0,
                          codec_context->height,
                          encode_frame->data,
                          encode_frame->linesize);
                encode_frame->pts = extracted_index;

                if (avcodec_send_frame(encoder_context, encode_frame) < 0)
                {
                    encode_error_message = "发送处理后帧到编码器失败";
                    return false;
                }
                if (!writeEncodedPackets())
                {
                    return false;
                }
                auto encode_finished_at = std::chrono::steady_clock::now();
                result.encode_write_duration_ms += static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(encode_finished_at - encode_started_at).count());
                ++extracted_index;
            }

            ++decoded_index;
            if (!draining)
            {
                av_frame_unref(decoded_frame);
            }
        }
    };

    while (av_read_frame(format_context, packet) >= 0)
    {
        if (shouldCancelTask(is_cancel_requested))
        {
            markCancelled();
            av_packet_unref(packet);
            cleanup();
            return false;
        }

        if (packet->stream_index == video_stream_index)
        {
            if (avcodec_send_packet(codec_context, packet) < 0)
            {
                av_packet_unref(packet);
                cleanup();
                return false;
            }

            if (!flush_decoder(false))
            {
                av_packet_unref(packet);
                cleanup();
                return false;
            }
        }
        av_packet_unref(packet);
    }

    if (shouldCancelTask(is_cancel_requested))
    {
        markCancelled();
        cleanup();
        return false;
    }

    avcodec_send_packet(codec_context, nullptr);
    if (!flush_decoder(true))
    {
        cleanup();
        return false;
    }

    if (avcodec_send_frame(encoder_context, nullptr) < 0)
    {
        encode_error_message = "刷新结果视频编码器失败";
        cleanup();
        return false;
    }
    if (!writeEncodedPackets())
    {
        cleanup();
        return false;
    }

    if (av_write_trailer(output_format_context) < 0)
    {
        encode_error_message = "写入结果视频尾部失败";
        cleanup();
        return false;
    }

    result.extracted_frame_count = extracted_index;
    result.total_detection_count = total_detection_count;
    cleanup();
    return true;
}
}

bool VideoProcessor::hasSufficientDiskSpaceForTask(const TaskEntity &task, std::string &error_message)
{
    std::uint64_t available_bytes = getAvailableDiskBytes(AppConfig::PROJECT_ROOT);
    std::uint64_t required_bytes = estimateRequiredDiskBytesForTask(task);
    if (available_bytes >= required_bytes)
    {
        return true;
    }

    std::ostringstream oss;
    oss << "磁盘剩余空间不足，当前可用 "
        << (available_bytes / (1024ULL * 1024ULL)) << " MB"
        << "，预计至少需要 "
        << (required_bytes / (1024ULL * 1024ULL)) << " MB"
        << "，请清理磁盘空间或降低并发后重试";
    error_message = oss.str();
    return false;
}

bool VideoProcessor::processTask(const TaskEntity &task, TaskEntity &out_result)
{
    return VideoProcessor::processTask(task, out_result, std::function<bool()>());
}

bool VideoProcessor::processTask(const TaskEntity &task,
                                 TaskEntity &out_result,
                                 const std::function<bool()> &is_cancel_requested)
{
    auto task_started_at = std::chrono::steady_clock::now();
    out_result = task;

    if (shouldCancelTask(is_cancel_requested))
    {
        markTaskFailure(out_result, TaskStatus::CANCELLED, "任务已被用户取消");
        return false;
    }

    if (!pathExists(task.input_video_path))
    {
        markTaskFailure(out_result, TaskStatus::FAILED_INPUT_NOT_FOUND, "输入视频文件不存在");
        return false;
    }

    if (!createDirectoryIfNeeded(AppConfig::VIDEO_OUTPUT_DIR))
    {
        markTaskFailure(out_result, TaskStatus::FAILED_OUTPUT_DIR, "创建结果视频输出目录失败");
        return false;
    }

    out_result.output_video_path = buildOutputPath(task.input_video_path, task.id);

    VideoFileMetadata metadata;
    auto metadata_started_at = std::chrono::steady_clock::now();
    // 这里已经真正调用 FFmpeg 读取视频基础信息，后续抽帧和推理都会建立在这一步之上。
    if (!VideoMetadataHelper::readMetadata(task.input_video_path, metadata))
    {
        markTaskFailure(out_result, TaskStatus::FAILED_METADATA, "读取输入视频元数据失败");
        return false;
    }
    auto metadata_finished_at = std::chrono::steady_clock::now();

    if (shouldCancelTask(is_cancel_requested))
    {
        out_result.video_duration = metadata.duration_seconds;
        out_result.video_width = metadata.width;
        out_result.video_height = metadata.height;
        out_result.video_fps = metadata.fps;
        markTaskFailure(out_result, TaskStatus::CANCELLED, "任务已被用户取消");
        return false;
    }

    InferenceModelContext model_context;
    std::string model_error_message;
    if (!YoloInference::buildModelContext(task.model_id, model_context, model_error_message))
    {
        markTaskFailure(out_result, TaskStatus::FAILED_INFERENCE, model_error_message.empty() ? "加载推理模型上下文失败" : model_error_message);
        return false;
    }
    model_context.confidence_threshold = static_cast<float>(task.confidence_threshold);

    if (shouldCancelTask(is_cancel_requested))
    {
        out_result.video_duration = metadata.duration_seconds;
        out_result.video_width = metadata.width;
        out_result.video_height = metadata.height;
        out_result.video_fps = metadata.fps;
        out_result.used_model_name = model_context.model_name;
        out_result.used_model_framework = model_context.framework;
        markTaskFailure(out_result, TaskStatus::CANCELLED, "任务已被用户取消");
        return false;
    }

    FrameExtractionResult extraction_result;
    std::string encode_error_message;
    auto extraction_started_at = std::chrono::steady_clock::now();
    if (!extractFrames(task.input_video_path,
                       task.frame_interval,
                       task.id,
                       metadata.fps,
                       out_result.output_video_path,
                       model_context,
                       encode_error_message,
                       extraction_result,
                       is_cancel_requested))
    {
        if (extraction_result.cancelled)
        {
            std::remove(out_result.output_video_path.c_str());
            out_result.output_video_path.clear();
            out_result.video_duration = metadata.duration_seconds;
            out_result.video_width = metadata.width;
            out_result.video_height = metadata.height;
            out_result.video_fps = metadata.fps;
            out_result.processed_frame_count = extraction_result.extracted_frame_count;
            out_result.detection_count = extraction_result.total_detection_count;
            out_result.real_inference_executed = extraction_result.real_inference_ran;
            out_result.result_video_generated = false;
            out_result.used_model_name = extraction_result.inference_model_name;
            out_result.used_model_framework = extraction_result.inference_framework;
            out_result.video_build_mode = "cancelled";
            out_result.inference_runtime_message = extraction_result.inference_runtime_message;
            std::ostringstream cancel_summary;
            cancel_summary << "任务已取消，视频处理线程已安全退出；已处理帧数="
                           << extraction_result.extracted_frame_count
                           << "，总检测框数量=" << extraction_result.total_detection_count;
            out_result.result_summary = cancel_summary.str();
            markTaskFailure(out_result, TaskStatus::CANCELLED, "任务已被用户取消");
            return false;
        }

        if (!encode_error_message.empty())
        {
            markTaskFailure(out_result, TaskStatus::FAILED_ENCODE, encode_error_message);
        }
        else
        {
            markTaskFailure(out_result, TaskStatus::FAILED_INFERENCE, "视频抽帧推理失败");
        }
        return false;
    }
    auto extraction_finished_at = std::chrono::steady_clock::now();

    bool built_from_frames = extraction_result.extracted_frame_count > 0 && pathExists(out_result.output_video_path);
    std::string build_video_error = encode_error_message;
    auto video_build_started_at = std::chrono::steady_clock::now();
    extraction_result.result_video_generated = built_from_frames;
    extraction_result.video_build_mode = built_from_frames ? "ffmpeg_stream_encode" : "copy_fallback";

    if (!built_from_frames)
    {
        if (!copyFileBinary(task.input_video_path, out_result.output_video_path))
        {
            markTaskFailure(out_result, TaskStatus::FAILED_OUTPUT_COPY, "结果视频回退复制失败");
            return false;
        }
    }
    auto video_build_finished_at = std::chrono::steady_clock::now();

    long long output_size = getFileSize(out_result.output_video_path);
    std::uint64_t metadata_duration_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(metadata_finished_at - metadata_started_at).count());
    std::uint64_t extraction_duration_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(extraction_finished_at - extraction_started_at).count());
    std::uint64_t video_build_duration_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(video_build_finished_at - video_build_started_at).count());
    std::uint64_t total_duration_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(video_build_finished_at - task_started_at).count());
    out_result.video_duration = metadata.duration_seconds;
    out_result.video_width = metadata.width;
    out_result.video_height = metadata.height;
    out_result.video_fps = metadata.fps;
    out_result.processed_frame_count = extraction_result.extracted_frame_count;
    out_result.detection_count = extraction_result.total_detection_count;
    out_result.real_inference_executed = extraction_result.real_inference_ran;
    out_result.result_video_generated = built_from_frames;
    out_result.used_model_name = extraction_result.inference_model_name;
    out_result.used_model_framework = extraction_result.inference_framework;
    out_result.video_build_mode = built_from_frames ? "ffmpeg_stream_encode" : "copy_fallback";
    out_result.inference_runtime_message = extraction_result.inference_runtime_message;

    std::ostringstream oss;
    oss << (built_from_frames ? "处理完成，已基于处理后帧流式编码结果视频；类型=" :
                                "处理完成，但结果视频暂时回退为原视频复制；类型=")
        << task.task_type
        << "，时长=" << metadata.duration_seconds << " 秒"
        << "，分辨率=" << metadata.width << "x" << metadata.height
        << "，帧率=" << metadata.fps
        << "，抽帧间隔=" << task.frame_interval
        << "，抽帧数量=" << extraction_result.extracted_frame_count
        << "，推理模型=" << extraction_result.inference_model_name
        << "，推理框架=" << extraction_result.inference_framework
        << "，模型文件状态=" << (extraction_result.inference_model_file_exists ? "已找到" : "未找到，当前走占位模式")
        << "，真实前向状态=" << (extraction_result.real_inference_ran ? "已执行" : "未执行")
        << "，推理运行信息=" << extraction_result.inference_runtime_message
        << "，模型加载信息=" << model_context.load_message
        << "，结果视频生成方式=" << (built_from_frames ? "FFmpeg 流式编码" : "原视频复制回退")
        << (build_video_error.empty() ? "" : "，视频合成信息=" + build_video_error)
        << "，帧已通过 YoloInference 完成单帧处理"
        << "，总检测框数量=" << extraction_result.total_detection_count
        << "，置信度阈值=" << task.confidence_threshold
        << "，阶段耗时(ms): 元数据=" << metadata_duration_ms
        << "，抽帧推理=" << extraction_duration_ms
        << "，结果生成=" << video_build_duration_ms
        << "；细分耗时(ms): 解码取帧=" << extraction_result.decode_receive_duration_ms
        << "，原图转RGB=" << extraction_result.render_scale_duration_ms
        << "，推理图缩放=" << extraction_result.inference_scale_duration_ms
        << "，ONNX前向=" << extraction_result.inference_forward_duration_ms
        << "，后处理=" << extraction_result.inference_postprocess_duration_ms
        << "，画框=" << extraction_result.inference_draw_duration_ms
        << "，编码写出=" << extraction_result.encode_write_duration_ms
        << "，总耗时=" << total_duration_ms
        << "，结果文件大小=" << output_size << " 字节";

    out_result.result_summary = oss.str();
    out_result.status = TaskStatus::COMPLETED;
    out_result.error_message.clear();
    return true;
}

std::string VideoProcessor::inferFailureStatus(const TaskEntity &task_result)
{
    if (!task_result.status.empty())
    {
        return task_result.status;
    }
    return TaskStatus::FAILED_RUNTIME;
}
