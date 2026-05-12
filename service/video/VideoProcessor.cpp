#include "service/video/VideoProcessor.h"

#include "common/config/AppConfig.h"
#include "service/inference/YoloInference.h"

#ifdef __cplusplus
extern "C"
{
#endif
// FFmpeg 的头文件是 C 风格接口，放在 extern "C" 里避免 C++ 名字改编导致链接失败。
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#ifdef __cplusplus
}
#endif

#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>

namespace
{
struct VideoMetadata
{
    int width = 0;
    int height = 0;
    double fps = 0.0;
    double duration_seconds = 0.0;
};

struct FrameExtractionResult
{
    int extracted_frame_count = 0;
    int total_detection_count = 0;
    std::string frame_directory;
    std::string inference_model_name;
    std::string inference_framework;
    bool inference_model_file_exists = false;
    bool real_inference_ran = false;
    std::string inference_runtime_message;
    bool result_video_generated = false;
    std::string video_build_mode;
};

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

std::string buildFrameOutputDirectory(long long task_id)
{
    return std::string(AppConfig::VIDEO_FRAME_DIR) + "/task_" + std::to_string(task_id);
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

std::string shellEscapeSingleQuotes(const std::string &input)
{
    std::string escaped;
    escaped.reserve(input.size() + 8);
    for (char ch : input)
    {
        if (ch == '\'')
        {
            escaped += "'\\''";
        }
        else
        {
            escaped += ch;
        }
    }
    return escaped;
}

bool buildResultVideoFromFrames(const std::string &frame_directory,
                                double fps,
                                const std::string &output_path,
                                std::string &error_message)
{
    if (frame_directory.empty() || output_path.empty())
    {
        error_message = "帧目录或输出路径为空";
        return false;
    }

    double target_fps = fps > 0.0 ? fps : 25.0;
    std::ostringstream fps_stream;
    fps_stream.setf(std::ios::fixed);
    fps_stream.precision(3);
    fps_stream << target_fps;

    std::string input_pattern = frame_directory + "/frame_%d.ppm";
    std::ostringstream command;
    // 这里直接复用系统 ffmpeg，把已经写盘的处理后帧序列重新编码成 mp4，
    // 能用最小改动把“结果视频真实包含检测框”这条链路先落地。
    command << "ffmpeg -y -loglevel error -framerate " << fps_stream.str()
            << " -i '" << shellEscapeSingleQuotes(input_pattern) << "'"
            << " -c:v libx264 -pix_fmt yuv420p"
            << " '" << shellEscapeSingleQuotes(output_path) << "'";

    int exit_code = std::system(command.str().c_str());
    if (exit_code != 0 || !pathExists(output_path))
    {
        error_message = "ffmpeg 合成结果视频失败，exit_code=" + std::to_string(exit_code);
        return false;
    }

    return true;
}

bool saveFrameAsPpm(const AVFrame *rgb_frame, int width, int height, const std::string &path)
{
    std::ofstream output(path.c_str(), std::ios::binary);
    if (!output.is_open())
    {
        return false;
    }

    output << "P6\n" << width << " " << height << "\n255\n";
    for (int y = 0; y < height; ++y)
    {
        const uint8_t *row = rgb_frame->data[0] + y * rgb_frame->linesize[0];
        output.write(reinterpret_cast<const char *>(row), static_cast<std::streamsize>(width * 3));
    }

    output.flush();
    return output.good();
}

bool readVideoMetadata(const std::string &path, VideoMetadata &metadata)
{
    AVFormatContext *format_context = nullptr;
    // 打开输入视频，FFmpeg 会根据文件内容自动判断封装格式。
    if (avformat_open_input(&format_context, path.c_str(), nullptr, nullptr) < 0)
    {
        return false;
    }

    // 读取流信息；没有这一步就拿不到时长、视频流、帧率等元数据。
    if (avformat_find_stream_info(format_context, nullptr) < 0)
    {
        avformat_close_input(&format_context);
        return false;
    }

    if (format_context->duration != AV_NOPTS_VALUE)
    {
        metadata.duration_seconds = static_cast<double>(format_context->duration) / AV_TIME_BASE;
    }

    // 找到“最合适”的视频流。一个容器里可能有多个流，这里只关心视频流。
    int video_stream_index = av_find_best_stream(format_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_index < 0)
    {
        avformat_close_input(&format_context);
        return false;
    }

    AVStream *video_stream = format_context->streams[video_stream_index];
    AVCodecParameters *codecpar = video_stream->codecpar;
    metadata.width = codecpar->width;
    metadata.height = codecpar->height;

    // 优先使用 avg_frame_rate；若为空，再退回 r_frame_rate。
    AVRational frame_rate = video_stream->avg_frame_rate.num != 0 ? video_stream->avg_frame_rate
                                                                  : video_stream->r_frame_rate;
    if (frame_rate.num != 0 && frame_rate.den != 0)
    {
        metadata.fps = av_q2d(frame_rate);
    }

    avformat_close_input(&format_context);
    return metadata.width > 0 && metadata.height > 0;
}

bool extractFrames(const std::string &path,
                   int frame_interval,
                   long long task_id,
                   const InferenceModelContext &model_context,
                   FrameExtractionResult &result)
{
    if (frame_interval <= 0)
    {
        return false;
    }

    AVFormatContext *format_context = nullptr;
    if (avformat_open_input(&format_context, path.c_str(), nullptr, nullptr) < 0)
    {
        return false;
    }

    if (avformat_find_stream_info(format_context, nullptr) < 0)
    {
        avformat_close_input(&format_context);
        return false;
    }

    int video_stream_index = av_find_best_stream(format_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_index < 0)
    {
        avformat_close_input(&format_context);
        return false;
    }

    AVStream *video_stream = format_context->streams[video_stream_index];
    const AVCodec *decoder = avcodec_find_decoder(video_stream->codecpar->codec_id);
    if (decoder == nullptr)
    {
        avformat_close_input(&format_context);
        return false;
    }

    AVCodecContext *codec_context = avcodec_alloc_context3(decoder);
    if (codec_context == nullptr)
    {
        avformat_close_input(&format_context);
        return false;
    }

    if (avcodec_parameters_to_context(codec_context, video_stream->codecpar) < 0)
    {
        avcodec_free_context(&codec_context);
        avformat_close_input(&format_context);
        return false;
    }

    if (avcodec_open2(codec_context, decoder, nullptr) < 0)
    {
        avcodec_free_context(&codec_context);
        avformat_close_input(&format_context);
        return false;
    }

    SwsContext *sws_context = sws_getContext(codec_context->width,
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
        avcodec_free_context(&codec_context);
        avformat_close_input(&format_context);
        return false;
    }

    AVFrame *decoded_frame = av_frame_alloc();
    AVFrame *rgb_frame = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();
    if (decoded_frame == nullptr || rgb_frame == nullptr || packet == nullptr)
    {
        if (packet != nullptr)
        {
            av_packet_free(&packet);
        }
        if (decoded_frame != nullptr)
        {
            av_frame_free(&decoded_frame);
        }
        if (rgb_frame != nullptr)
        {
            av_frame_free(&rgb_frame);
        }
        sws_freeContext(sws_context);
        avcodec_free_context(&codec_context);
        avformat_close_input(&format_context);
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
        av_packet_free(&packet);
        av_frame_free(&decoded_frame);
        av_frame_free(&rgb_frame);
        sws_freeContext(sws_context);
        avcodec_free_context(&codec_context);
        avformat_close_input(&format_context);
        return false;
    }

    result.frame_directory = buildFrameOutputDirectory(task_id);
    // 提前记录本次任务绑定的模型上下文，这样即使视频没有成功抽出任何帧，任务摘要里也能看到模型信息。
    result.inference_model_name = model_context.model_name;
    result.inference_framework = model_context.framework;
    result.inference_model_file_exists = model_context.file_exists;
    if (!createDirectoryIfNeeded(result.frame_directory))
    {
        av_freep(&rgb_frame->data[0]);
        av_packet_free(&packet);
        av_frame_free(&decoded_frame);
        av_frame_free(&rgb_frame);
        sws_freeContext(sws_context);
        avcodec_free_context(&codec_context);
        avformat_close_input(&format_context);
        return false;
    }

    int decoded_index = 0;
    int extracted_index = 0;
    int total_detection_count = 0;

    auto flush_decoder = [&](bool draining) -> bool
    {
        while (true)
        {
            int receive_result = avcodec_receive_frame(codec_context, decoded_frame);
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
                sws_scale(sws_context,
                          decoded_frame->data,
                          decoded_frame->linesize,
                          0,
                          codec_context->height,
                          rgb_frame->data,
                          rgb_frame->linesize);

                FrameBuffer frame_buffer;
                frame_buffer.width = codec_context->width;
                frame_buffer.height = codec_context->height;
                frame_buffer.linesize = rgb_frame->linesize[0];
                frame_buffer.data = rgb_frame->data[0];

                InferenceResult inference_result;
                if (!YoloInference::processFrame(frame_buffer, model_context, inference_result))
                {
                    return false;
                }
                total_detection_count += inference_result.detection_count;
                result.inference_model_name = inference_result.model_name;
                result.inference_framework = inference_result.model_framework;
                result.inference_model_file_exists = model_context.file_exists;
                result.real_inference_ran = inference_result.real_inference_ran;
                result.inference_runtime_message = inference_result.runtime_message;

                std::ostringstream frame_path;
                frame_path << result.frame_directory << "/frame_" << extracted_index << ".ppm";
                if (!saveFrameAsPpm(rgb_frame, codec_context->width, codec_context->height, frame_path.str()))
                {
                    return false;
                }
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
        if (packet->stream_index == video_stream_index)
        {
            if (avcodec_send_packet(codec_context, packet) < 0)
            {
                av_packet_unref(packet);
                av_freep(&rgb_frame->data[0]);
                av_packet_free(&packet);
                av_frame_free(&decoded_frame);
                av_frame_free(&rgb_frame);
                sws_freeContext(sws_context);
                avcodec_free_context(&codec_context);
                avformat_close_input(&format_context);
                return false;
            }

            if (!flush_decoder(false))
            {
                av_packet_unref(packet);
                av_freep(&rgb_frame->data[0]);
                av_packet_free(&packet);
                av_frame_free(&decoded_frame);
                av_frame_free(&rgb_frame);
                sws_freeContext(sws_context);
                avcodec_free_context(&codec_context);
                avformat_close_input(&format_context);
                return false;
            }
        }
        av_packet_unref(packet);
    }

    avcodec_send_packet(codec_context, nullptr);
    if (!flush_decoder(true))
    {
        av_freep(&rgb_frame->data[0]);
        av_packet_free(&packet);
        av_frame_free(&decoded_frame);
        av_frame_free(&rgb_frame);
        sws_freeContext(sws_context);
        avcodec_free_context(&codec_context);
        avformat_close_input(&format_context);
        return false;
    }

    result.extracted_frame_count = extracted_index;
    result.total_detection_count = total_detection_count;

    av_freep(&rgb_frame->data[0]);
    av_packet_free(&packet);
    av_frame_free(&decoded_frame);
    av_frame_free(&rgb_frame);
    sws_freeContext(sws_context);
    avcodec_free_context(&codec_context);
    avformat_close_input(&format_context);
    return true;
}
}

bool VideoProcessor::processTask(const TaskEntity &task, TaskEntity &out_result)
{
    if (!pathExists(task.input_video_path))
    {
        return false;
    }

    if (!createDirectoryIfNeeded(AppConfig::VIDEO_OUTPUT_DIR))
    {
        return false;
    }

    out_result = task;
    out_result.output_video_path = buildOutputPath(task.input_video_path, task.id);

    VideoMetadata metadata;
    // 这里已经真正调用 FFmpeg 读取视频基础信息，后续抽帧和推理都会建立在这一步之上。
    if (!readVideoMetadata(task.input_video_path, metadata))
    {
        return false;
    }

    InferenceModelContext model_context;
    std::string model_error_message;
    if (!YoloInference::buildModelContext(task.model_id, model_context, model_error_message))
    {
        out_result.error_message = model_error_message;
        return false;
    }
    model_context.confidence_threshold = static_cast<float>(task.confidence_threshold);

    FrameExtractionResult extraction_result;
    if (!extractFrames(task.input_video_path, task.frame_interval, task.id, model_context, extraction_result))
    {
        return false;
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    bool built_from_frames = false;
    std::string build_video_error;
    if (extraction_result.extracted_frame_count > 0)
    {
        built_from_frames = buildResultVideoFromFrames(extraction_result.frame_directory,
                                                       metadata.fps,
                                                       out_result.output_video_path,
                                                       build_video_error);
    }
    extraction_result.result_video_generated = built_from_frames;
    extraction_result.video_build_mode = built_from_frames ? "ffmpeg_reencode" : "copy_fallback";

    if (!built_from_frames)
    {
        if (!copyFileBinary(task.input_video_path, out_result.output_video_path))
        {
            return false;
        }
    }

    long long output_size = getFileSize(out_result.output_video_path);
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
    out_result.video_build_mode = built_from_frames ? "ffmpeg_reencode" : "copy_fallback";
    out_result.inference_runtime_message = extraction_result.inference_runtime_message;

    std::ostringstream oss;
    oss << (built_from_frames ? "处理完成，已基于处理后帧重建结果视频；类型=" :
                                "处理完成，但结果视频暂时回退为原视频复制；类型=")
        << task.task_type
        << "，时长=" << metadata.duration_seconds << " 秒"
        << "，分辨率=" << metadata.width << "x" << metadata.height
        << "，帧率=" << metadata.fps
        << "，抽帧间隔=" << task.frame_interval
        << "，抽帧数量=" << extraction_result.extracted_frame_count
        << "，帧输出目录=" << extraction_result.frame_directory
        << "，推理模型=" << extraction_result.inference_model_name
        << "，推理框架=" << extraction_result.inference_framework
        << "，模型文件状态=" << (extraction_result.inference_model_file_exists ? "已找到" : "未找到，当前走占位模式")
        << "，真实前向状态=" << (extraction_result.real_inference_ran ? "已执行" : "未执行")
        << "，推理运行信息=" << extraction_result.inference_runtime_message
        << "，模型加载信息=" << model_context.load_message
        << "，结果视频生成方式=" << (built_from_frames ? "ffmpeg 帧序列重编码" : "原视频复制回退")
        << (build_video_error.empty() ? "" : "，视频合成信息=" + build_video_error)
        << "，帧已通过 YoloInference 完成单帧处理"
        << "，总检测框数量=" << extraction_result.total_detection_count
        << "，置信度阈值=" << task.confidence_threshold
        << "，结果文件大小=" << output_size << " 字节";

    out_result.result_summary = oss.str();
    out_result.status = "COMPLETED";
    out_result.error_message.clear();
    return true;
}
