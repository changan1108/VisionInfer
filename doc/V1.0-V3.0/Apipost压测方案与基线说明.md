# VisionInfer 基于 Apipost 的压测方案与基线说明

## 1. 适用范围

本文档用于指导你使用 `Apipost 客户端` 对当前 `VisionInfer` 后端进行接口压测，并建立一套可复现的基线数据，用于后续性能优化前后对比。

基于我在 `2026-05-14` 查到的 Apipost 官方文档，当前有两类与压测相关的能力：

- HTTP 接口上的 `一键压测`
- 自动化测试中的 `性能测试`

其中官方明确说明：

- `一键压测` 仅支持客户端，Web 端暂不支持高并发和测试数据
- `自动化测试 -> 性能测试` 在 `8.1.4` 版本起支持，且也仅限客户端

## 2. 为什么这次优先推荐 Apipost

对你当前的毕设后端来说，Apipost 有三个优点：

- 你已经有成型的接口文档，导入和维护接口集合比较方便
- 它能直接看到总请求数、失败率、平均响应时间、P90/P95/P99 等指标
- 它支持 `csv/txt` 测试数据，后面做参数化任务提交压测比较顺手

但也要注意它的定位：

- 更适合做“接口层和任务提交层”的压测
- 不适合直接替代系统级极限压测平台
- 对视频上传这类超大体积请求，不建议一开始就拿它猛压

## 3. 本次压测目标

本次不是直接找极限，而是先建立基线，重点回答这几个问题：

- 当前版本的读接口延迟是否平稳
- 数据库连接池改造后，读接口的尾延迟是否收敛
- 任务线程池有界队列是否生效
- 队列打满时系统是否会稳定返回 `REJECTED_QUEUE_FULL` / 忙碌保护，而不是卡死
- 当前任务链的主要瓶颈更偏向排队、推理还是结果编码

## 4. 压测前准备

### 4.1 服务端准备

确认后端已经启动，并且以下接口可正常访问：

- `GET /api/system/status`
- `GET /api/task/list?limit=10`
- `GET /api/task/stats`
- `GET /api/video/list?limit=10`

如果要压任务提交，还需要准备：

- 至少一个可用 `video_id`
- 最好存在一个激活模型

### 4.2 Apipost 客户端准备

建议使用 `Apipost 客户端`，不要使用 Web 版做这次压测。  
原因：官方文档说明 Web 端暂不支持高并发和测试数据。

### 4.3 建议准备一个测试环境

建议在 Apipost 中建一个环境，例如：

- `base_url = http://127.0.0.1:9527`

这样后面接口路径都可以写成：

- `{{base_url}}/api/system/status`

## 5. Apipost 使用说明

## 5.1 方式一：HTTP 接口的一键压测

这是最适合你当前阶段的方式，先用它做单接口基线。

操作流程：

1. 在 Apipost 客户端中创建或导入接口
2. 打开某个 HTTP 接口
3. 进入该接口的 `一键压测` 页面
4. 设置压测参数
5. 点击执行压测并观察报告

官方文档说明的关键点：

- 可以单独设置压测时使用的请求参数，不会影响调试和设计中的参数
- 支持上传 `csv` 或 `txt` 测试数据做参数化压测
- 并发数最高可输入 `10000`
- 支持按 `轮次` 或 `压测时长` 进行
- 可以开启日志，压测时会在本地下载日志文件

适合场景：

- `GET /api/system/status`
- `GET /api/task/list`
- `GET /api/task/stats`
- `POST /api/task/submit`

## 5.2 方式二：自动化测试里的性能测试

如果你后面想把多个接口串成完整流程再压，比如：

- 登录
- 查当前模型
- 查视频列表
- 提交任务
- 轮询任务状态

那就可以考虑 Apipost 自动化测试中的 `性能测试`。

官方文档说明的关键点：

- `8.1.4` 版本起支持
- 目前支持两种模式：
  - 固定模式
  - 爬坡模式
- 点击 `执行` 或 `保存并执行` 即可开始
- 只有 `保存并执行` 才会保存报告
- 压测中可以实时查看请求数、失败数、响应时间等
- 压测中如果强行关闭弹窗，相当于停止压测

适合场景：

- 多接口编排的链路压测
- 模拟真实前端访问顺序
- 做小规模场景化压测

## 5.3 测试数据怎么用

官方文档说明：

- 支持 `text`、`csv`
- 数据通过 `{{变量名}}` 引用
- `csv` 如果有中文乱码，建议转成 `UTF-8`

对你这个项目，建议优先用 `csv`，字段可以是：

```csv
submitted_by,video_id,task_name,task_type,frame_interval,confidence_threshold,model_id
chenwanyao,1,load_case_001,knife_detection,10,0.6,1
chenwanyao,1,load_case_002,knife_detection,10,0.6,1
chenwanyao,1,load_case_003,vehicle_detection,5,0.5,1
```

然后在任务提交接口里引用：

```json
{
  "task_name": "{{task_name}}",
  "task_type": "{{task_type}}",
  "submitted_by": "{{submitted_by}}",
  "input_video_id": {{video_id}},
  "frame_interval": {{frame_interval}},
  "confidence_threshold": {{confidence_threshold}},
  "model_id": {{model_id}}
}
```

## 6. 本项目推荐的压测顺序

建议按“从轻到重”的顺序来，不要一开始就压视频上传。

### 阶段 A：读接口基线

目标：

- 验证网络层、路由层、数据库连接池是否稳定
- 建立低风险基线

推荐接口：

- `GET /api/system/status`
- `GET /api/task/list?limit=10`
- `GET /api/task/stats`
- `GET /api/video/list?limit=10`

推荐压测方式：

- 一键压测
- 固定并发
- 按轮次执行

推荐并发梯度：

1. `10`
2. `20`
3. `50`
4. `100`

每档建议：

- 轮次 `100` 到 `300`

观察重点：

- 平均响应时间
- P95
- P99
- 失败率
- 是否有大量非 2xx

同时在服务端看：

- `current_active_connections`
- `cpu_usage`
- `memory_usage`
- `rejected_tasks` 理论上应保持不变

### 阶段 B：混合读接口压测

目标：

- 模拟前端首页/列表页频繁查询
- 观察数据库层和监控接口是否成为瓶颈

建议做法：

- 用自动化测试性能测试编排多个 GET 接口
- 采用固定模式

推荐链路：

1. `GET /api/system/status`
2. `GET /api/task/list?limit=10`
3. `GET /api/task/stats`
4. `GET /api/video/list?limit=10`

推荐并发梯度：

1. `10`
2. `20`
3. `30`

每档建议持续：

- `60s`

观察重点：

- P95 是否明显劣化
- 失败率是否升高
- 服务端监控接口本身是否变慢

### 阶段 C：任务提交压测

目标：

- 验证任务队列上限和拒绝策略
- 观察系统在“可控过载”下是否稳定

不建议直接压 `/api/video/upload`，建议压：

- `POST /api/task/submit`

原因：

- 上传接口当前仍包含 multipart 解析和文件落盘
- 对毕设当前阶段来说，真正需要先验证的是“任务受理与调度稳定性”

推荐接口体：

```json
{
  "task_name": "load_case_001",
  "task_type": "knife_detection",
  "submitted_by": "chenwanyao",
  "input_video_id": 1,
  "frame_interval": 10,
  "confidence_threshold": 0.6,
  "model_id": 1
}
```

推荐并发梯度：

1. `2`
2. `5`
3. `10`
4. `20`

推荐模式：

- 固定模式优先
- 每档 `30s` 或按轮次 `20/40/80`

观察重点：

- 受理是否成功
- 是否开始出现 `REJECTED_QUEUE_FULL`
- 是否出现 `REJECTED_DISK_FULL`
- 出现 `REJECTED_QUEUE_FULL` 时系统是否仍稳定
- `waiting_tasks`
- `queue_usage`
- `avg_queue_wait_ms`
- `max_queue_wait_ms`

同时结合 HTTP 状态码判断：

- `503`：通常表示视频处理队列已满
- `507`：通常表示磁盘剩余空间不足

### 阶段 D：爬坡压测

目标：

- 找出系统从稳定区进入拥塞区的大致拐点

建议方式：

- 使用自动化测试中的 `性能测试`
- 选择 `爬坡模式`

建议参数思路：

- 初始并发：`2`
- 初始持续：`30s`
- 爬坡到：`5`
- 爬坡时长：`30s`
- 再爬坡到：`10`
- 最大持续：`30s`

如果系统仍稳定，再尝试：

- 最大并发 `20`

观察重点：

- 哪个并发档开始明显出现排队
- 哪个并发档开始出现 `REJECTED_QUEUE_FULL`
- 哪个并发档 P95 / P99 急剧上升

## 7. 当前版本推荐的基线表

每轮压测建议都记录在同一张表里，方便后续做“优化前 vs 优化后”对比。

| 场景 | 工具模式 | 并发 | 轮次/时长 | Avg(ms) | P95(ms) | P99(ms) | 失败率 | rejected_tasks | avg_queue_wait_ms | 结论 |
|---|---|---:|---|---:|---:|---:|---:|---:|---:|---|
| system/status | 一键压测-固定 | 10 | 200轮 |  |  |  |  |  |  |  |
| system/status | 一键压测-固定 | 50 | 200轮 |  |  |  |  |  |  |  |
| mixed_read | 自动化性能-固定 | 20 | 60s |  |  |  |  |  |  |  |
| task/submit | 一键压测-固定 | 5 | 40轮 |  |  |  |  |  |  |  |
| task/submit | 一键压测-固定 | 10 | 80轮 |  |  |  |  |  |  |  |
| task/submit | 自动化性能-爬坡 | 2->10 | 90s |  |  |  |  |  |  |  |

## 8. 结果怎么解释

### 8.1 读接口一开始就慢

优先怀疑：

- 网络层主循环被阻塞
- 数据库查询路径仍有热点
- `/api/system/status` 本身统计逻辑太重

### 8.2 提交任务很快，但队列等待时间高

说明接入层稳定，但工作线程池已成为瓶颈。  
后续优先优化：

- `VideoProcessor`
- `YoloInference`
- 结果视频生成

### 8.3 很快出现 `REJECTED_*`

这不一定是坏事。  
这通常说明“有界队列 + 背压保护”已经生效。

要看的是：

- 系统是否还能继续响应查询接口
- 已受理任务是否仍能正常完成
- `REJECTED_QUEUE_FULL` 比例是否过高
- 是否夹杂 `REJECTED_DISK_FULL`

### 8.4 P95/P99 很高，但平均值还行

说明系统存在明显尾延迟问题。  
常见原因：

- 某些任务排队太久
- 某些结果视频重编码耗时很长
- 数据库偶发抖动

## 9. 本次不建议优先压的视频上传接口

当前阶段不建议把 `/api/video/upload` 作为第一波主压测对象，原因有三点：

- 它包含 multipart 解析
- 它包含大 body 接收
- 它包含同步文件写盘

如果现在直接重压上传接口，测出来的结果会把“网络接收瓶颈”和“磁盘写入瓶颈”混在一起，不利于判断优化收益。

更合理的顺序是：

1. 先压读接口
2. 再压任务提交
3. 最后单独压上传接口

## 10. 建议的执行顺序

你这周如果要正式开始用 Apipost 建基线，我建议就按下面顺序跑：

1. `GET /api/system/status` 固定并发 `10 / 20 / 50`
2. `GET /api/task/list` 固定并发 `10 / 20 / 50`
3. 多 GET 混合链路固定并发 `20`
4. `POST /api/task/submit` 固定并发 `2 / 5 / 10`
5. `POST /api/task/submit` 爬坡 `2 -> 5 -> 10`

## 10.1 建议同步记录的新状态字段

当前版本任务状态已经细化，建议在 Apipost 压测记录中额外统计：

- `PENDING`
- `QUEUED`
- `PROCESSING`
- `COMPLETED`
- `REJECTED_QUEUE_FULL`
- `REJECTED_DISK_FULL`
- `FAILED_INPUT_NOT_FOUND`
- `FAILED_METADATA`
- `FAILED_INFERENCE`
- `FAILED_ENCODE`
- `FAILED_OUTPUT_COPY`
- `FAILED_RUNTIME`

如果写实验表，推荐额外加这些列：

- `503_count`
- `507_count`
- `completed_count`
- `rejected_queue_full_count`
- `rejected_disk_full_count`
- `failed_inference_count`
- `failed_encode_count`

## 11. 官方参考链接

以下是我本次使用的 Apipost 官方文档：

- 如何使用压测：https://wiki.apipost.cn/docs/FAQ/loadtesting/
- 性能测试：https://wiki.apipost.cn/docs/test/load-testing/
- 压测中测试数据如何使用：https://wiki.apipost.cn/docs/FAQ/stresstestdata/
