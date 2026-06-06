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

// 视频解码抽帧+单帧推理+视频编码写入mp4
bool extractFrames(const std::string &path,// 输入视频路径
                   int frame_interval,// 抽帧间隔
                   long long task_id,// 任务编号
                   double output_fps,// 结果视频帧率
                   const std::string &output_path,// 结果视频路径
                   const InferenceModelContext &model_context,// 推理模型配置(上下文)
                   std::string &encode_error_message,// 返回编码错误信息
                   FrameExtractionResult &result,// 返回帧数、检测数和耗时
                   const std::function<bool()> &is_cancel_requested)// 随时检查任务取消
{
    if (frame_interval <= 0 || output_path.empty())
    {
        return false;
    }

    // 输入端
    AVFormatContext *format_context = nullptr;// 表示整个输入媒体容器，例如整个 MP4 文件
    AVCodecContext *codec_context = nullptr;// 解码器上下文
    SwsContext *sws_context = nullptr;// 解码帧 -> 原尺寸 RGB
    SwsContext *inference_sws_context = nullptr;// 解码帧 -> 模型尺寸 RGB
    // 图片帧
    AVFrame *decoded_frame = nullptr;// 解码器输出的原始帧
    AVFrame *rgb_frame = nullptr;// 原视频尺寸RGB，负责画框和输出
    AVFrame *inference_rgb_frame = nullptr;// 模型输入尺寸RGB，负责推理

    AVPacket *packet = nullptr;// 从输入文件读取的压缩包

    // 输出端
    AVFormatContext *output_format_context = nullptr;// 表示将要生成的结果MP4容器
    AVCodecContext *encoder_context = nullptr;// 编码器上下文
    SwsContext *encode_sws_context = nullptr; // RGB -> YUV420P
    // 图片帧(经过层层处理后的可以用于编码器的帧)
    AVFrame *encode_frame = nullptr;// YUV420P，送给H.264编码器
    AVPacket *encode_packet = nullptr;// 保存 H.264 编码器生成的压缩数据，随后写入 MP4
    AVStream *output_stream = nullptr; // 表示媒体文件中的一条轨道,即一个MP4有视频流、音频流、字幕流，本次主要抓视频流(输出端，输入端的定义在下面)

    /*数据变化
    视频流的Packet数据（一个视频容器中H.264格式的压缩数据，常规视频中每帧都会压缩，不然视频文件会很大）
    -> decoded_frame（解码后的原始帧：原尺寸 YUV）
    -> rgb_frame（原始帧：原尺寸 YUV -> 原尺寸 RGB）
    -> inference_rgb_frame（原始帧：原尺寸 YUV -> 模型要求的输入尺寸：640×640 RGB）
    -> encode_frame（由画框后的画框后原尺寸 RGB -> 原尺寸 YUV420P）
    -> encode_packet（重新压缩的H.264格式的压缩包）
    
    FFmpeg核心思想是
    解码：Packet -> Frame
    编码：Frame -> Packet
    */

    // 定义一个lambda函数对象：cleanup，用于资源回收
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

    /* 初始化解码器-start */
    // 打开输入视频文件
    if (avformat_open_input(&format_context, path.c_str(), nullptr, nullptr) < 0)
    {
        return false;
    }
    // 读取各种流数据
    if (avformat_find_stream_info(format_context, nullptr) < 0)
    {
        cleanup();
        return false;
    }

    // 找到视频流
    int video_stream_index = av_find_best_stream(format_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_index < 0)
    {
        cleanup();
        return false;
    }

    // 查找与视频编码格式匹配的解码器并打开解码器(如视频是H.264，就寻找H.264解码器；也可以是其他格式，只要是FFmpeg库支持的就行)
    AVStream *video_stream = format_context->streams[video_stream_index];
    const AVCodec *decoder = avcodec_find_decoder(video_stream->codecpar->codec_id);// AVCodec：解码器类型
    if (decoder == nullptr)
    {
        cleanup();
        return false;
    }

    // 创建解码上下文
    codec_context = avcodec_alloc_context3(decoder);
    if (codec_context == nullptr)
    {
        cleanup();
        return false;
    }

    // 将视频流参数复制给解码器
    if (avcodec_parameters_to_context(codec_context, video_stream->codecpar) < 0)
    {
        cleanup();
        return false;
    }

    // 打开解码器
    if (avcodec_open2(codec_context, decoder, nullptr) < 0)
    {
        cleanup();
        return false;
    }
    /* 初始化解码器-end */

    /* 转换原始帧-start */
    // 原尺寸RGB转换(原尺寸 YUV -> 原尺寸 RGB，用于：将检测框画在原分辨率视频上+最后生成结果视频)
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

    // 模型输入要求的尺寸RGB(原尺寸 YUV -> 640×640 RGB，用于：模型推理)
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
    
    // 创建decoded/RGB/inference Frame并分配内存
    decoded_frame = av_frame_alloc(); 
    rgb_frame = av_frame_alloc();
    inference_rgb_frame = av_frame_alloc();
    // 创建packet并分配内存
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
    /* 转换原始帧-end */


    /* 输出编码器初始化-start */
    // 创建输出MP4上下文
    avformat_alloc_output_context2(&output_format_context, nullptr, nullptr, output_path.c_str());
    if (output_format_context == nullptr)
    {
        encode_error_message = "创建输出视频上下文失败";
        cleanup();
        return false;
    }

    // 寻找H.264编码器
    const AVCodec *encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (encoder == nullptr)
    {
        encode_error_message = "找不到 H264 编码器";
        cleanup();
        return false;
    }

    // 创建输出视频流
    output_stream = avformat_new_stream(output_format_context, nullptr);
    // 创建编码上下文
    encoder_context = output_stream == nullptr ? nullptr : avcodec_alloc_context3(encoder);
    if (output_stream == nullptr || encoder_context == nullptr)
    {
        encode_error_message = "创建输出视频流失败";
        cleanup();
        return false;
    }

    // 配置编码参数
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

    // 配置并打开H.264编码器
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
        // 打开输出文件
        if (avio_open(&output_format_context->pb, output_path.c_str(), AVIO_FLAG_WRITE) < 0)
        {
            encode_error_message = "打开结果视频文件失败";
            cleanup();
            return false;
        }
    }

    // 写入MP4文件头
    if (avformat_write_header(output_format_context, nullptr) < 0)
    {
        encode_error_message = "写入结果视频头失败";
        cleanup();
        return false;
    }


    // 创建RGB->YUV420P转换上下文
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
    
    // 创建编码Frame和Packet
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
    /* 输出编码器初始化-end */

    /* 定义辅助lambda-start */
    // 定义一个lambda函数对象
    // 负责从编码器接收encode_packet->写入输出MP4
    auto writeEncodedPackets = [&]() -> bool
    {
        while (true)
        {
            // 获取编码后的压缩数据包
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

            // 写入输出 MP4
            if (av_interleaved_write_frame(output_format_context, encode_packet) < 0)
            {
                av_packet_unref(encode_packet);
                encode_error_message = "写入结果视频数据失败";
                return false;
            }
            av_packet_unref(encode_packet);
        }
    };

    // 负责从解码器接收 decoded_frame，并依次完成抽帧、格式转换、推理、画框和编码。
    // 接收并处理解码器当前可输出的全部视频帧。
    // is_draining=false：正常处理某个输入 Packet 产生的帧；
    // is_draining=true：输入结束并发送空 Packet 后，接收解码器内部缓存的剩余帧。
    auto receiveAndProcessDecodedFrames = [&](bool is_draining) -> bool
    {
        while (true)
        {
            if (shouldCancelTask(is_cancel_requested))
            {
                markCancelled();
                return false;
            }

            auto receive_started_at = std::chrono::steady_clock::now();

            // 从解码器接收原始帧,接收解码后的 Frame
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

            // 抽帧策略
            if (decoded_index % frame_interval == 0) // 取frame_interval的倍数索引
            {
                if (shouldCancelTask(is_cancel_requested))
                {
                    markCancelled();
                    return false;
                }

                auto render_scale_started_at = std::chrono::steady_clock::now();
                // 调用FFmpeg库函数：将解码帧转换为原尺寸但RGB格式(用于在原始尺寸上画框+生成结果视频)
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
                
                // 转换并缩放为模型输入尺寸(用于模型推理)
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

                // 包装为项目自定义的FrameBuffer(原尺寸+RGB)
                FrameBuffer render_frame_buffer;
                render_frame_buffer.width = codec_context->width;
                render_frame_buffer.height = codec_context->height;
                render_frame_buffer.linesize = rgb_frame->linesize[0];
                render_frame_buffer.data = rgb_frame->data[0];

                // 包装为项目自定义的FrameBuffer(模型要求尺寸+RGB)
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

                // 进行YOLO/ONNX单帧处理
                // (其中，inference_frame_buffer：送入ONNX模型；render_frame_buffer：在原尺寸图像上画检测框)
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

                // 完成推理和画框后，要把原尺寸+RGB 转为编码器需要的 原尺寸+YUV420P(画框后的rgb_frame不能直接给H.264编码器，因此先转换为YUV420P)
                sws_scale(encode_sws_context,
                          rgb_frame->data,
                          rgb_frame->linesize,
                          0,
                          codec_context->height,
                          encode_frame->data,
                          encode_frame->linesize);
                // 设置输出帧时间戳
                encode_frame->pts = extracted_index;

                // 把处理后的帧(encode_frame)送入编码器
                if (avcodec_send_frame(encoder_context, encode_frame) < 0)
                {
                    encode_error_message = "发送处理后帧到编码器失败";
                    return false;
                }

                // 调用 writeEncodedPackets()，内部获取压缩Packet
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
            // 正常解码阶段主动解除当前帧引用；排空阶段由后续 receive/free 统一处理。
            if (!is_draining)
            {
                av_frame_unref(decoded_frame);
            }
        }
    };
    /* 定义辅助lambda-end */


    /* 主解码循环-start */
    // 每次读取一个输入Packet
    while (av_read_frame(format_context, packet) >= 0)
    {
        // "任务取消"检查点
        if (shouldCancelTask(is_cancel_requested))
        {
            markCancelled();
            av_packet_unref(packet);
            cleanup();
            return false;
        }

        // 只处理视频流
        if (packet->stream_index == video_stream_index)
        {
            // 将压缩包packet送入解码器
            if (avcodec_send_packet(codec_context, packet) < 0)
            {
                av_packet_unref(packet);
                cleanup();
                return false;
            }

            // 当前 Packet 送入后，接收并处理解码器此时能够输出的全部视频帧。
            // 由于解码器可能缓存和重排序，这些帧不一定只由当前 Packet 产生，也可能暂时没有帧可取。
            if (!receiveAndProcessDecodedFrames(false))
            {
                av_packet_unref(packet);
                cleanup();
                return false;
            }
        }
        av_packet_unref(packet);
    }
    /* 主解码循环-end */


    if (shouldCancelTask(is_cancel_requested))
    {
        markCancelled();
        cleanup();
        return false;
    }

    // 向解码器发送 nullptr，排空剩余帧
    // 通知解码器：输入Packet已经全部发送完，请把内部缓存的剩余Frame全部吐出来
    // 空 Packet 通知解码器输入已经结束，使其输出内部缓存的延迟帧。
    avcodec_send_packet(codec_context, nullptr);
    // 接收并处理解码器排空阶段输出的剩余视频帧。
    if (!receiveAndProcessDecodedFrames(true))
    {
        cleanup();
        return false;
    }

    // 最后额外执行一次avcodec_send_frame(encoder_context, nullptr);+writeEncodedPackets();
    // 用于对编码器内部缓存的数据全部排空
    // 空 Frame 通知编码器不再有新帧，使其输出内部缓存的延迟编码包。
    if (avcodec_send_frame(encoder_context, nullptr) < 0)
    {
        encode_error_message = "刷新结果视频编码器失败";
        cleanup();
        return false;
    }
    // 接收编码器排空阶段输出的剩余 Packet，并全部写入结果 MP4。
    if (!writeEncodedPackets())
    {
        cleanup();
        return false;
    }

    // 写入MP4尾部和索引信息(编码管线完整结束)
    if (av_write_trailer(output_format_context) < 0)
    {
        encode_error_message = "写入结果视频尾部失败";
        cleanup();
        return false;
    }
    // 保存帧数和检测数
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

// 简化重载版，内部测试使用，项目最终实际不使用
bool VideoProcessor::processTask(const TaskEntity &task, TaskEntity &out_result)
{
    return VideoProcessor::processTask(task, out_result, std::function<bool()>());
}

// 第二级线程池线程执行的lambda内的：“任务真正的视频处理函数”(参数一:任务参数实体；参数二:输出处理结果参数；参数三:取消检查函数)
bool VideoProcessor::processTask(const TaskEntity &task,
                                 TaskEntity &out_result,
                                 const std::function<bool()> &is_cancel_requested)
{
    auto task_started_at = std::chrono::steady_clock::now();
    // out_result会先复制原任务的字段，然后处理过程中不断补充其他字段
    out_result = task;

    // “任务取消”的检查点
    if (shouldCancelTask(is_cancel_requested))
    {
        markTaskFailure(out_result, TaskStatus::CANCELLED, "任务已被用户取消");
        return false;
    }

    // 检查"输入视频的路径"是否有效
    if (!pathExists(task.input_video_path))
    {
        markTaskFailure(out_result, TaskStatus::FAILED_INPUT_NOT_FOUND, "输入视频文件不存在");
        return false;
    }

    // 创建"输出目录"
    if (!createDirectoryIfNeeded(AppConfig::VIDEO_OUTPUT_DIR))
    {
        markTaskFailure(out_result, TaskStatus::FAILED_OUTPUT_DIR, "创建结果视频输出目录失败");
        return false;
    }

    // 生成"输出路径"(其中，主要是生成结果视频的文件名:原文件名_task_任务ID_result.mp4)
    out_result.output_video_path = buildOutputPath(task.input_video_path, task.id);

    // 创建视频文件元数据对象
    VideoFileMetadata metadata;
    auto metadata_started_at = std::chrono::steady_clock::now();

    // 真正调用FFmpeg读取当前视频元数据，后续抽帧和推理都会建立在这一步之上(这里只读取基础信息，还没有逐帧解码)
    if (!VideoMetadataHelper::readMetadata(task.input_video_path, metadata))
    {
        markTaskFailure(out_result, TaskStatus::FAILED_METADATA, "读取输入视频元数据失败");
        return false;
    }
    auto metadata_finished_at = std::chrono::steady_clock::now();

    // 再次一个"任务取消"检查点
    if (shouldCancelTask(is_cancel_requested))
    {
        out_result.video_duration = metadata.duration_seconds;
        out_result.video_width = metadata.width;
        out_result.video_height = metadata.height;
        out_result.video_fps = metadata.fps;
        markTaskFailure(out_result, TaskStatus::CANCELLED, "任务已被用户取消");
        return false;
    }

    // 模型推理上下文结构体
    InferenceModelContext model_context;
    std::string model_error_message;

    // 构建推理模型上下文
    if (!YoloInference::buildModelContext(task.model_id, model_context, model_error_message))
    {
        markTaskFailure(out_result, TaskStatus::FAILED_INFERENCE, model_error_message.empty() ? "加载推理模型上下文失败" : model_error_message);
        return false;
    }
    model_context.confidence_threshold = static_cast<float>(task.confidence_threshold);

    // "任务取消"检查点
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
    
    // extractFrames:视频解码抽帧 + 单帧推理/画框 + 重新编码写入 MP4。
    // extraction_result 用于带回帧数、检测数、模型信息和各阶段耗时；
    // encode_error_message 专门带回编码/写文件阶段的详细失败原因。
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
        // extractFrames 返回 false 有三类主要原因：用户取消、编码失败、其他抽帧/推理失败。
        // 第一类：处理过程中收到取消请求。删除可能存在的半成品结果视频，
        // 并保留取消发生前已经完成的帧数、检测数和模型运行信息。
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

        // 第二类：encode_error_message 非空，说明失败点在结果视频编码或写文件阶段。
        if (!encode_error_message.empty())
        {
            markTaskFailure(out_result, TaskStatus::FAILED_ENCODE, encode_error_message);
        }
        // 第三类：没有明确编码错误，统一归类为抽帧/推理失败。
        else
        {
            markTaskFailure(out_result, TaskStatus::FAILED_INFERENCE, "视频抽帧推理失败");
        }
        return false;
    }
    auto extraction_finished_at = std::chrono::steady_clock::now();

    // 同时满足“至少处理了一帧”和“输出文件确实存在”，才认为结果视频由"处理后帧"成功编码得到。

    // extracted_frame_count > 0：至少一帧完成了抽帧、推理/画框和编码；
    // pathExists(...)：编码器确实在磁盘上生成了输出文件。
    // 其他失败情况下，会删除已经生成的半成品视频、清空输出路径
    bool built_from_frames = extraction_result.extracted_frame_count > 0 && pathExists(out_result.output_video_path); //标记是否成功完成

    // 保存 extractFrames 带回的编码提示或错误信息，后面写入任务结果摘要。
    std::string build_video_error = encode_error_message;

    // 记录“确认结果视频或执行回退复制”阶段的开始时间，用于统计该阶段耗时。
    auto video_build_started_at = std::chrono::steady_clock::now();

    // 标记是否真正生成了基于处理后帧的结果视频，而不只是存在一个可返回的视频文件。
    extraction_result.result_video_generated = built_from_frames;

    // ffmpeg_stream_encode：处理后帧由 FFmpeg 流式编码生成结果视频；
    // copy_fallback：没有生成处理后视频，后续复制原视频作为降级结果。
    extraction_result.video_build_mode = built_from_frames ? "ffmpeg_stream_encode" : "copy_fallback";

    // 没有成功生成处理后视频时，用复制原视频作为降级结果，保证结果接口仍有文件可返回。
    // 此时任务可以完成，但 result_video_generated=false，video_build_mode=copy_fallback，
    if (!built_from_frames)
    {
        if (!copyFileBinary(task.input_video_path, out_result.output_video_path))
        {
            markTaskFailure(out_result, TaskStatus::FAILED_OUTPUT_COPY, "结果视频回退复制失败");
            return false;
        }
    }
    auto video_build_finished_at = std::chrono::steady_clock::now();

    // 汇总输出文件大小和各阶段耗时等参数，用于任务详情、性能分析。
    long long output_size = getFileSize(out_result.output_video_path);
    std::uint64_t metadata_duration_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(metadata_finished_at - metadata_started_at).count());
    std::uint64_t extraction_duration_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(extraction_finished_at - extraction_started_at).count());
    std::uint64_t video_build_duration_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(video_build_finished_at - video_build_started_at).count());
    std::uint64_t total_duration_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(video_build_finished_at - task_started_at).count());
    // 把 VideoProcessor 产生的结果写入 out_result。
    // 这里只更新内存对象，数据库更新由上层 TaskService 调用 TaskDao::markTaskCompleted 完成。
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

    // 生成成功后的处理结果摘要
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

    // 写入摘要字段
    out_result.result_summary = oss.str();

    // 将内存结果标记为 COMPLETED 并清空错误信息
    out_result.status = TaskStatus::COMPLETED;
    out_result.error_message.clear();
    return true;
}

std::string VideoProcessor::inferFailureStatus(const TaskEntity &task_result)
{
    // VideoProcessor 已经判断出具体失败阶段时，保留其 FAILED_* 或 CANCELLED 状态。
    if (!task_result.status.empty())
    {
        return task_result.status;
    }
    // 若底层没有给出具体状态，则由上层统一按未知运行时失败处理。
    return TaskStatus::FAILED_RUNTIME;
}
