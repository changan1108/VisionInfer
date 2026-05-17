#!/usr/bin/env python3
import argparse
import json
import statistics
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed


def now_ms():
    return int(time.time() * 1000)


def percentile(sorted_values, ratio):
    if not sorted_values:
        return 0.0
    index = int((len(sorted_values) - 1) * ratio)
    return sorted_values[index]


class HttpClient:
    def __init__(self, base_url, timeout):
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout

    def request(self, method, path, body=None, headers=None):
        url = self.base_url + path
        data = None
        actual_headers = {} if headers is None else dict(headers)
        if body is not None:
            data = json.dumps(body).encode("utf-8")
            actual_headers["Content-Type"] = "application/json"

        request = urllib.request.Request(url=url, data=data, method=method, headers=actual_headers)
        started = time.perf_counter()
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                raw = response.read()
                latency_ms = (time.perf_counter() - started) * 1000.0
                payload = {}
                if raw:
                    try:
                        payload = json.loads(raw.decode("utf-8"))
                    except Exception:
                        payload = {"raw": raw.decode("utf-8", errors="replace")}
                return {
                    "ok": True,
                    "status": response.status,
                    "latency_ms": latency_ms,
                    "payload": payload,
                    "error": "",
                }
        except urllib.error.HTTPError as exc:
            latency_ms = (time.perf_counter() - started) * 1000.0
            raw = exc.read()
            payload = {}
            if raw:
                try:
                    payload = json.loads(raw.decode("utf-8"))
                except Exception:
                    payload = {"raw": raw.decode("utf-8", errors="replace")}
            return {
                "ok": False,
                "status": exc.code,
                "latency_ms": latency_ms,
                "payload": payload,
                "error": str(exc),
            }
        except Exception as exc:
            latency_ms = (time.perf_counter() - started) * 1000.0
            return {
                "ok": False,
                "status": 0,
                "latency_ms": latency_ms,
                "payload": {},
                "error": str(exc),
            }


def build_case(case_name, args, iteration):
    if case_name == "status":
        return ("GET", "/api/system/status", None)
    if case_name == "list_tasks":
        return ("GET", "/api/task/list?limit=10", None)
    if case_name == "list_videos":
        return ("GET", "/api/video/list?limit=10", None)
    if case_name == "task_stats":
        return ("GET", "/api/task/stats", None)
    if case_name == "mixed_read":
        paths = [
            "/api/system/status",
            "/api/task/list?limit=10",
            "/api/task/stats",
            "/api/video/list?limit=10",
        ]
        return ("GET", paths[iteration % len(paths)], None)
    if case_name == "submit_tasks":
        if args.video_id <= 0:
            raise ValueError("--video-id must be greater than 0 for submit_tasks")
        payload = {
            "task_name": "load_test_{0}_{1}".format(now_ms(), iteration),
            "task_type": args.task_type,
            "submitted_by": args.submitted_by,
            "input_video_id": args.video_id,
            "frame_interval": args.frame_interval,
            "confidence_threshold": args.confidence_threshold,
        }
        if args.model_id > 0:
            payload["model_id"] = args.model_id
        return ("POST", "/api/task/submit", payload)
    raise ValueError("unsupported case: {0}".format(case_name))


def fetch_snapshot(client):
    result = client.request("GET", "/api/system/status")
    if result["ok"]:
        return result["payload"]
    return {"error": result["error"], "status": result["status"]}


def worker(client, case_name, args, start_barrier, results, index):
    try:
        method, path, payload = build_case(case_name, args, index)
    except Exception as exc:
        results[index] = {
            "ok": False,
            "status": 0,
            "latency_ms": 0.0,
            "payload": {},
            "error": str(exc),
        }
        return

    start_barrier.wait()
    results[index] = client.request(method, path, payload)


def summarize(results, wall_time_seconds):
    latencies = sorted(item["latency_ms"] for item in results if item is not None)
    success_count = sum(1 for item in results if item is not None and item["ok"])
    error_count = len(results) - success_count
    statuses = {}
    for item in results:
        if item is None:
            continue
        statuses[item["status"]] = statuses.get(item["status"], 0) + 1

    summary = {
        "total_requests": len(results),
        "success_count": success_count,
        "error_count": error_count,
        "wall_time_seconds": round(wall_time_seconds, 3),
        "qps": round(len(results) / wall_time_seconds, 3) if wall_time_seconds > 0 else 0.0,
        "avg_latency_ms": round(statistics.mean(latencies), 3) if latencies else 0.0,
        "min_latency_ms": round(min(latencies), 3) if latencies else 0.0,
        "p50_latency_ms": round(percentile(latencies, 0.50), 3) if latencies else 0.0,
        "p95_latency_ms": round(percentile(latencies, 0.95), 3) if latencies else 0.0,
        "p99_latency_ms": round(percentile(latencies, 0.99), 3) if latencies else 0.0,
        "max_latency_ms": round(max(latencies), 3) if latencies else 0.0,
        "statuses": statuses,
    }
    return summary


def print_json(title, payload):
    print("=== {0} ===".format(title))
    print(json.dumps(payload, ensure_ascii=False, indent=2))


def parse_args():
    parser = argparse.ArgumentParser(description="VisionInfer dependency-free load test runner")
    parser.add_argument("--base-url", default="http://127.0.0.1:9527", help="server base url")
    parser.add_argument(
        "--case",
        default="status",
        choices=["status", "list_tasks", "list_videos", "task_stats", "mixed_read", "submit_tasks"],
        help="load test case",
    )
    parser.add_argument("--concurrency", type=int, default=20, help="number of concurrent workers")
    parser.add_argument("--requests", type=int, default=100, help="total requests")
    parser.add_argument("--timeout", type=float, default=10.0, help="per-request timeout seconds")
    parser.add_argument("--submitted-by", default="load_test_user", help="submitted_by for task submit case")
    parser.add_argument("--video-id", type=int, default=0, help="input_video_id for task submit case")
    parser.add_argument("--model-id", type=int, default=0, help="optional model_id for task submit case")
    parser.add_argument(
        "--task-type",
        default="knife_detection",
        choices=["violation_detection", "vehicle_detection", "knife_detection"],
        help="task_type for submit_tasks case",
    )
    parser.add_argument("--frame-interval", type=int, default=10, help="frame_interval for submit_tasks case")
    parser.add_argument("--confidence-threshold", type=float, default=0.6, help="confidence threshold for submit_tasks case")
    return parser.parse_args()


def main():
    args = parse_args()
    if args.concurrency <= 0 or args.requests <= 0:
        raise SystemExit("concurrency and requests must be greater than 0")

    client = HttpClient(args.base_url, args.timeout)
    before_snapshot = fetch_snapshot(client)
    print_json("before_snapshot", before_snapshot)

    results = [None] * args.requests
    start_barrier = threading.Barrier(args.concurrency + 1)

    started = time.perf_counter()
    with ThreadPoolExecutor(max_workers=args.concurrency) as executor:
        futures = []
        for index in range(args.requests):
            futures.append(executor.submit(worker, client, args.case, args, start_barrier, results, index))

        start_barrier.wait()
        for future in as_completed(futures):
            future.result()
    wall_time_seconds = time.perf_counter() - started

    summary = summarize(results, wall_time_seconds)
    after_snapshot = fetch_snapshot(client)

    print_json("summary", summary)
    print_json("after_snapshot", after_snapshot)

    errors = [item for item in results if item is not None and not item["ok"]]
    if errors:
        print("=== sample_errors ===")
        for item in errors[:5]:
            print(json.dumps(item, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
