#ifndef YOLO_INFERENCE_H
#define YOLO_INFERENCE_H

#include <string>
#include <vector>

struct DetectionBox
{
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    std::string label;
    float confidence = 0.0f;
};

struct FrameBuffer
{
    int width = 0;
    int height = 0;
    int linesize = 0;
    unsigned char *data = nullptr;
};

struct InferenceResult
{
    int detection_count = 0;
    std::vector<DetectionBox> boxes;
    std::string summary;
};

class YoloInference
{
public:
    static bool processFrame(FrameBuffer &frame, InferenceResult &result);
};

#endif // YOLO_INFERENCE_H
