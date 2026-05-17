#!/usr/bin/env python3
import argparse
import csv
import json
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request


def request_json(method, url, body=None):
    data = None
    headers = {}
    if body is not None:
        data = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"

    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            text = resp.read().decode("utf-8")
            return resp.getcode(), json.loads(text)
    except urllib.error.HTTPError as exc:
        text = exc.read().decode("utf-8")
        try:
            payload = json.loads(text)
        except Exception:
            payload = {"code": exc.code, "msg": text}
        return exc.code, payload


def parse_duration(summary, label):
    pattern = re.escape(label) + r"=(\d+)"
    match = re.search(pattern, summary)
    return int(match.group(1)) if match else None


def parse_text_field(summary, label):
    pattern = re.escape(label) + r"=([^，；]+)"
    match = re.search(pattern, summary)
    return match.group(1) if match else ""


def wait_for_task(base_url, task_id, poll_interval, timeout_seconds):
    deadline = time.time() + timeout_seconds
    terminal_prefixes = ("COMPLETED", "FAILED_", "REJECTED_")
    while time.time() < deadline:
        url = base_url + "/api/task/status?id=" + urllib.parse.quote(str(task_id))
        status_code, payload = request_json("GET", url)
        if status_code != 200 or payload.get("code") != 200:
            raise RuntimeError("查询任务状态失败: {}".format(payload.get("msg", "unknown error")))

        data = payload.get("data", {})
        status = data.get("status", "")
        if status == "COMPLETED" or any(status.startswith(prefix) for prefix in terminal_prefixes[1:]):
            return data
        time.sleep(poll_interval)

    raise TimeoutError("任务 {} 在 {} 秒内未结束".format(task_id, timeout_seconds))


def run_single_case(base_url, model_id, submitted_by, video_id, task_type, frame_interval, confidence_threshold):
    switch_code, switch_payload = request_json(
        "POST",
        base_url + "/api/model/switch",
        {"model_id": model_id},
    )
    if switch_code != 200 or switch_payload.get("code") != 200:
        raise RuntimeError("切换模型 {} 失败: {}".format(model_id, switch_payload.get("msg", "unknown error")))

    submit_body = {
        "task_name": "benchmark_model_{}".format(model_id),
        "task_type": task_type,
        "submitted_by": submitted_by,
        "input_video_id": video_id,
        "frame_interval": frame_interval,
        "confidence_threshold": confidence_threshold,
        "model_id": model_id,
    }
    submit_code, submit_payload = request_json("POST", base_url + "/api/task/submit", submit_body)
    if submit_code != 200 or submit_payload.get("code") != 200:
        return {
            "model_id": model_id,
            "submit_http_code": submit_code,
            "submit_msg": submit_payload.get("msg", ""),
            "status": "SUBMIT_FAILED",
        }

    task_id = submit_payload["data"]["task_id"]
    task_data = wait_for_task(base_url, task_id, poll_interval=2.0, timeout_seconds=7200)
    summary = task_data.get("result_summary", "")
    return {
        "model_id": model_id,
        "task_id": task_id,
        "status": task_data.get("status", ""),
        "used_model_name": task_data.get("used_model_name", ""),
        "used_model_framework": task_data.get("used_model_framework", ""),
        "processed_frame_count": task_data.get("processed_frame_count", 0),
        "detection_count": task_data.get("detection_count", 0),
        "result_video_generated": task_data.get("result_video_generated", False),
        "error_message": task_data.get("error_message", ""),
        "total_duration_ms": parse_duration(summary, "总耗时"),
        "metadata_duration_ms": parse_duration(summary, "元数据"),
        "extraction_duration_ms": parse_duration(summary, "抽帧推理"),
        "decode_receive_duration_ms": parse_duration(summary, "解码取帧"),
        "render_scale_duration_ms": parse_duration(summary, "原图转RGB"),
        "inference_scale_duration_ms": parse_duration(summary, "推理图缩放"),
        "onnx_forward_duration_ms": parse_duration(summary, "ONNX前向"),
        "postprocess_duration_ms": parse_duration(summary, "后处理"),
        "draw_duration_ms": parse_duration(summary, "画框"),
        "encode_write_duration_ms": parse_duration(summary, "编码写出"),
        "video_duration_seconds": parse_text_field(summary, "时长").replace(" 秒", ""),
    }


def print_markdown_table(rows):
    headers = [
        "model_id",
        "used_model_name",
        "status",
        "processed_frame_count",
        "detection_count",
        "total_duration_ms",
        "onnx_forward_duration_ms",
        "encode_write_duration_ms",
        "submit_http_code",
        "submit_msg",
    ]
    print("| " + " | ".join(headers) + " |")
    print("|" + "|".join(["---"] * len(headers)) + "|")
    for row in rows:
        print(
            "| " + " | ".join(str(row.get(header, "")) for header in headers) + " |"
        )


def write_csv_file(path, rows):
    if not rows:
        return
    fieldnames = sorted({key for row in rows for key in row.keys()})
    with open(path, "w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description="Run multi-model benchmark against VisionInfer backend.")
    parser.add_argument("--base-url", default="http://127.0.0.1:9527")
    parser.add_argument("--model-ids", required=True, help="Comma-separated model ids, e.g. 1,2,3")
    parser.add_argument("--video-id", required=True, type=int)
    parser.add_argument("--submitted-by", required=True)
    parser.add_argument("--task-type", default="vehicle_detection")
    parser.add_argument("--frame-interval", default=10, type=int)
    parser.add_argument("--confidence-threshold", default=0.6, type=float)
    parser.add_argument("--output-csv", default="")
    args = parser.parse_args()

    model_ids = [int(item.strip()) for item in args.model_ids.split(",") if item.strip()]
    if not model_ids:
        raise SystemExit("至少提供一个 model_id")

    rows = []
    for model_id in model_ids:
        print("Running benchmark for model_id={}...".format(model_id), file=sys.stderr)
        row = run_single_case(
            base_url=args.base_url.rstrip("/"),
            model_id=model_id,
            submitted_by=args.submitted_by,
            video_id=args.video_id,
            task_type=args.task_type,
            frame_interval=args.frame_interval,
            confidence_threshold=args.confidence_threshold,
        )
        rows.append(row)

    print_markdown_table(rows)
    if args.output_csv:
        write_csv_file(args.output_csv, rows)
        print("\nCSV written to {}".format(args.output_csv), file=sys.stderr)


if __name__ == "__main__":
    main()
