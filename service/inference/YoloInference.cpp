#include "service/inference/YoloInference.h"

namespace
{
void drawDetectionBox(FrameBuffer &frame, const DetectionBox &box)
{
    int thickness = 4;
    for (int y = 0; y < frame.height; ++y)
    {
        unsigned char *row = frame.data + y * frame.linesize;
        for (int x = 0; x < frame.width; ++x)
        {
            bool is_top_or_bottom = (y >= box.top && y < box.top + thickness) ||
                                    (y <= box.bottom && y > box.bottom - thickness);
            bool is_left_or_right = (x >= box.left && x < box.left + thickness) ||
                                    (x <= box.right && x > box.right - thickness);
            bool inside_vertical = (y >= box.top && y <= box.bottom);
            bool inside_horizontal = (x >= box.left && x <= box.right);

            if ((is_top_or_bottom && inside_horizontal) || (is_left_or_right && inside_vertical))
            {
                // RGB24: red bounding box.
                row[x * 3 + 0] = 255;
                row[x * 3 + 1] = 0;
                row[x * 3 + 2] = 0;
            }
        }
    }
}
}

bool YoloInference::processFrame(FrameBuffer &frame, InferenceResult &result)
{
    if (frame.data == nullptr || frame.width <= 0 || frame.height <= 0)
    {
        return false;
    }

    // 占位推理：先生成一个居中的检测框，后续接入真实 ONNX Runtime 时只替换这里。
    DetectionBox box;
    box.left = frame.width / 3;
    box.top = frame.height / 3;
    box.right = box.left + frame.width / 3;
    box.bottom = box.top + frame.height / 3;
    box.label = "placeholder_target";
    box.confidence = 0.85f;

    drawDetectionBox(frame, box);

    result.detection_count = 1;
    result.boxes.push_back(box);
    result.summary = "占位推理完成，已对当前帧绘制检测框";
    return true;
}
