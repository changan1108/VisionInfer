# Windows端 VisionTrain 目标检测训练工具设计文档

## 1. 文档目的

本文档用于指导在 **Windows + NVIDIA GPU** 环境下，使用 Python 生态构建一个 **尽量简洁的目标检测训练工具 VisionTrain**。

该工具的目标不是替代当前后端项目 [VisionInfer](/home/chenwanyao/graduation_project)，而是作为一个独立的小型训练辅助程序，专门完成以下工作：

- 读取目标检测数据集配置
- 调用 YOLO 官方训练能力进行模型训练
- 导出训练结果
- 导出 ONNX 模型
- 生成可供 VisionInfer 上传使用的模型产物

该工具应尽量保持简单、独立、易于在 Windows 上运行，不承担推理服务或后端业务逻辑。

## 2. 为什么建议单独写一个 Python 训练工具

结论是：**建议单独写，但不要写复杂。**

原因如下：

1. 训练环境与后端运行环境分离  
   当前 VisionInfer 后端主要运行在 Linux 虚拟机中，重点是任务调度、视频处理和推理服务；而模型训练更适合在 Windows 宿主机上利用 NVIDIA GPU 进行。

2. Python 训练生态更成熟  
   YOLO 训练、数据处理、导出 ONNX、查看训练日志等，Python 生态明显更丰富，工程成本更低。

3. 便于后续做多模型实验  
   后面不仅要训练基线模型，还可能训练轻量模型和量化前模型。独立工具更方便复用。

4. 不污染主工程  
   VisionInfer 的主目标仍然是后端推理系统，不建议把训练逻辑直接混进主后端项目中。

因此，这个程序本质上应该是一个：

- 轻量训练包装器
- 面向实验
- 面向 YOLO 模型产出
- 面向 ONNX 导出

## 3. 训练目标定义

第一阶段目标先收敛为：

- 训练一个 **YOLO 目标检测模型**
- 模型输出：
  - 框
  - 类别
  - 置信度

该工具第一版应支持至少两类训练任务：

- 车辆检测
- 刀具检测

推荐的车辆类别集合：

- `car`
- `bus`
- `truck`
- `motorcycle`
- `bicycle`

推荐的刀具类别集合：

- 最简方案：`knife`

如果后续数据足够，也可以扩展：

- `kitchen_knife`
- `folding_knife`
- `dagger`

说明：

- 这些类别属于 **同一个检测模型内部的检测类别**
- 前端任务类型可以保持为：
  - `vehicle_detection`
  - `knife_detection`
- 工具本身不写死任务类型，而是由数据集和配置决定训练目标

## 4. 数据集要求

### 4.1 推荐数据格式

建议采用 **YOLO 检测数据集格式**：

- `images/train`
- `images/val`
- `labels/train`
- `labels/val`
- 一个 `dataset.yaml`

标签文件每行格式：

```text
class_id x_center y_center width height
```

坐标归一化到 `0~1`。

### 4.2 dataset.yaml 示例

车辆检测：

```yaml
path: D:/datasets/vehicle_detection
train: images/train
val: images/val

names:
  0: car
  1: bus
  2: truck
  3: motorcycle
  4: bicycle
```

刀具检测：

```yaml
path: D:/datasets/knife_detection
train: images/train
val: images/val

names:
  0: knife
```

### 4.3 第一阶段建议

第一阶段不要引入颜色、车型属性标签，只做：

- 目标框
- 目标类别

也就是只训练“目标检测模型”，不训练颜色识别模型、车型属性模型或刀具属性模型。

## 5. 技术路线建议

推荐直接基于 **Ultralytics YOLO Python API** 实现，而不是自己从零写训练循环。

推荐依赖：

- Python 3.10+
- `ultralytics`
- `torch`
- `torchvision`
- `onnx`
- `onnxruntime`
- `pyyaml`

推荐路线：

1. 用 Ultralytics 官方预训练权重作为初始化
2. 基于自定义目标检测数据集微调训练
3. 训练完成后导出：
   - `best.pt`
   - `best.onnx`
4. 将导出的 `best.onnx` 上传到 VisionInfer

## 6. 工具设计目标

VisionTrain 应满足以下设计目标：

- 尽量简单
- 命令行使用
- 只做训练、验证、导出
- 不做 GUI
- 不做复杂数据标注功能
- 不做数据库交互
- 不与 VisionInfer 后端直接耦合

## 7. 工具功能范围

### 7.1 必做功能

1. 读取训练配置
2. 启动 YOLO 训练
3. 保存训练输出目录
4. 导出 ONNX 模型
5. 输出最终模型路径

### 7.2 建议功能

1. 支持选择模型规模
   - `yolov8n`
   - `yolov8s`

2. 支持配置常用训练参数
   - `epochs`
   - `imgsz`
   - `batch`
   - `device`
   - `workers`

3. 训练结束后生成一个简要实验摘要
   - 模型名称
   - 数据集路径
   - 类别数
   - 训练轮数
   - 导出文件位置

### 7.3 暂不纳入第一版的功能

1. GUI 界面
2. 自动下载和清洗数据集
3. 自动标注
4. 多任务训练编排
5. INT8 量化生成
6. 直接推送模型到 Linux 服务器

## 8. 推荐程序结构

建议生成一个独立目录，例如：

```text
VisionTrain/
├── app.py
├── config.py
├── train_runner.py
├── export_runner.py
├── utils.py
├── requirements.txt
├── README.md
├── configs/
│   ├── vehicle_yolov8n.yaml
│   └── knife_yolov8n.yaml
└── outputs/
```

### 8.1 文件职责建议

`app.py`

- 命令行入口
- 解析参数
- 调用训练和导出流程

`config.py`

- 加载 YAML 配置
- 做基础校验

`train_runner.py`

- 调用 Ultralytics YOLO API 启动训练

`export_runner.py`

- 调用模型导出 ONNX

`utils.py`

- 路径处理
- 日志打印
- 输出摘要

`configs/vehicle_yolov8n.yaml` / `configs/knife_yolov8n.yaml`

- 训练配置模板

## 9. 推荐配置文件格式

建议使用 YAML 配置文件。

示例：

```yaml
task_name: vehicle_detection_yolov8n
dataset_yaml: D:/datasets/vehicle_detection/dataset.yaml
model_name: yolov8n.pt
epochs: 100
imgsz: 640
batch: 16
device: 0
workers: 4
project_dir: D:/VisionTrain/outputs
run_name: vehicle_yolov8n_exp01
export_onnx: true
onnx_opset: 12
```

刀具检测配置示例：

```yaml
task_name: knife_detection_yolov8n
dataset_yaml: D:/datasets/knife_detection/dataset.yaml
model_name: yolov8n.pt
epochs: 100
imgsz: 640
batch: 16
device: 0
workers: 4
project_dir: D:/VisionTrain/outputs
run_name: knife_yolov8n_exp01
export_onnx: true
onnx_opset: 12
```

## 10. 推荐命令行形式

### 10.1 训练并导出

```bash
python app.py train --config configs/vehicle_yolov8n.yaml
```

### 10.2 仅导出

```bash
python app.py export --weights D:/VisionTrain/outputs/vehicle_yolov8n_exp01/weights/best.pt
```

### 10.3 查看配置

```bash
python app.py show-config --config configs/vehicle_yolov8n.yaml
```

## 11. 核心训练逻辑建议

训练程序不要手写 PyTorch 训练循环，而是直接使用 Ultralytics 官方 API，例如逻辑上类似：

1. 读取配置
2. 创建 `YOLO(model_name)`
3. 调用 `model.train(...)`
4. 拿到 `best.pt`
5. 如果开启导出，则调用 `model.export(format="onnx", ...)`

第一版只要把这个流程封装稳定即可。

## 12. 推荐输出结果

训练完成后，程序应输出：

- `best.pt` 路径
- `best.onnx` 路径
- 本次训练配置摘要
- 输出目录路径

建议同时生成一个 `summary.json`，内容类似：

```json
{
  "task_name": "vehicle_detection_yolov8n",
  "model_name": "yolov8n.pt",
  "dataset_yaml": "D:/datasets/vehicle_detection/dataset.yaml",
  "epochs": 100,
  "imgsz": 640,
  "batch": 16,
  "best_pt": "D:/VisionTrain/outputs/vehicle_yolov8n_exp01/weights/best.pt",
  "best_onnx": "D:/VisionTrain/outputs/vehicle_yolov8n_exp01/weights/best.onnx"
}
```

## 13. 与 VisionInfer 的衔接方式

VisionTrain 与 VisionInfer 的关系应保持简单：

1. Windows 上完成训练
2. 产出 `best.onnx`
3. 将 `best.onnx` 拷贝到可上传位置
4. 使用 VisionInfer 的模型上传接口上传
5. 在任务提交时指定对应 `model_id`

也就是说，这个工具只负责：

- “把模型训练好并导出来”

不负责：

- 部署推理服务
- 接入视频任务
- 管理 Linux 后端运行

## 14. 第一版建议只支持的模型

为了尽量降低复杂度，第一版建议只支持：

- `yolov8n.pt`
- `yolov8s.pt`

原因：

- 与当前后端 YOLOv8 风格后处理最匹配
- 生态成熟
- 更适合后续做“基线模型 vs 轻量模型”对比

## 15. Windows 环境建议

建议在 Windows 上：

- 使用独立 Python 虚拟环境
- 由 NVIDIA GPU 提供训练加速
- 将训练目录放在非系统盘的固定路径

推荐：

- Python 虚拟环境单独管理
- CUDA 和 PyTorch 版本匹配
- 数据集路径、输出路径都使用可配置方式，不要写死

## 15.1 建议的训练配置实例

建议第一版至少准备两份配置：

- `configs/vehicle_yolov8n.yaml`
- `configs/knife_yolov8n.yaml`

如果后续做对比实验，再追加：

- `configs/vehicle_yolov8s.yaml`
- `configs/knife_yolov8s.yaml`

## 16. 程序设计约束

Windows 端的 Codex 在生成该程序时，应遵守以下约束：

1. 使用 Python 实现
2. 优先调用 Ultralytics 官方 API
3. 保持 CLI 工具形态
4. 保持代码简洁，避免过度设计
5. 配置驱动，不要把训练参数写死
6. 第一版支持通用目标检测训练，不把业务写死为车辆检测
7. 第一版不要引入 GUI
8. 第一版不要引入数据库
9. 第一版不要引入远程部署逻辑

## 17. 推荐给 Windows 端 Codex 的任务描述

可以把下面这段话直接发给 Windows 端 Codex，用来生成程序：

“请使用 Python 为我生成一个名为 VisionTrain 的简洁目标检测训练工具，运行环境是 Windows + NVIDIA GPU。该工具应基于 Ultralytics YOLO 官方 Python API，支持通过 YAML 配置文件进行训练，支持 `yolov8n.pt` 和 `yolov8s.pt` 两种基础模型，支持读取 YOLO 检测数据集配置 `dataset.yaml`，支持执行训练并导出 `best.onnx`，支持输出训练摘要文件。程序应采用 CLI 方式，不做 GUI，不做复杂数据处理，不与后端服务直接耦合，目录结构尽量清晰简单，并附带 `requirements.txt`、`README.md` 以及至少两份示例配置文件：一份用于车辆检测，一份用于刀具检测。” 

## 18. 后续第二阶段扩展方向

第一版完成后，后续可以再考虑：

1. 增加模型验证命令
2. 增加批量实验命令
3. 增加量化前模型导出支持
4. 增加实验结果汇总
5. 增加更多目标检测任务配置模板

但这些都建议放在第一版完成之后。

## 19. 最终结论

对于你当前的毕设目标，最合适的做法是：

- 在 Windows 上单独做一个轻量 Python 训练工具 VisionTrain
- 主要使用 Ultralytics 官方训练能力
- 第一阶段服务于“通用目标检测模型训练”
- 导出 ONNX 后再接入 VisionInfer

这是当前最稳、最省工程成本、最利于后续实验对比的一条路线。
