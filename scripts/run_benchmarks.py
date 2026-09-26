#!/usr/bin/env python3
import argparse
import csv
import socket
import subprocess
import sys
import time
from pathlib import Path

DEFAULT_POLICIES = ["lru", "lfu", "hybrid"]
DEFAULT_PATTERNS = ["zipfian", "sequential", "uniform"]
DEFAULT_CACHE_RATIOS = [0.1, 0.25, 0.5, 0.75, 1.0]
DEFAULT_CONCURRENCY = [1, 4, 16, 64]
CSV_HEADER = ["policy", "pattern", "cache_size", "concurrency",
              "throughput_rps", "p50_us", "p95_us", "p99_us", "hit_rate"]


def wait_for_port(host, port, timeout=5.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.2):
                return True
        except OSError:
            time.sleep(0.05)
    return False


def run_one(server_bin, client_bin, host, port, policy, pattern, cache_size,
            shards, threads, trace_file, concurrency):
    server = subprocess.Popen(
        [server_bin, "--port", str(port), "--capacity", str(cache_size),
         "--shards", str(shards), "--threads", str(threads), "--policy", policy],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    try:
        if not wait_for_port(host, port):
            print(f"server did not come up for policy {policy} port {port}", file=sys.stderr)
            return None
        result = subprocess.run(
            [client_bin, host, str(port), str(trace_file), str(concurrency),
             policy, pattern, str(cache_size)],
            capture_output=True, text=True, timeout=120,
        )
        if result.returncode != 0:
            print(f"bench_client failed: {result.stderr.strip()}", file=sys.stderr)
            return None
        return result.stdout.strip()
    finally:
        server.terminate()
        try:
            server.wait(timeout=3)
        except subprocess.TimeoutExpired:
            server.kill()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server-bin", default="../build/cachebench_server")
    ap.add_argument("--client-bin", default="../build/bench_client")
    ap.add_argument("--traces-dir", default="../traces")
    ap.add_argument("--trace-template", default="{pattern}.txt")
    ap.add_argument("--out", default="../results/results.csv")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--base-port", type=int, default=7100)
    ap.add_argument("--num-keys", type=int, default=2000)
    ap.add_argument("--shards", type=int, default=4)
    ap.add_argument("--threads", type=int, default=64)
    ap.add_argument("--policies", nargs="+", default=DEFAULT_POLICIES)
    ap.add_argument("--patterns", nargs="+", default=DEFAULT_PATTERNS)
    ap.add_argument("--cache-ratios", nargs="+", type=float, default=DEFAULT_CACHE_RATIOS)
    ap.add_argument("--concurrency", nargs="+", type=int, default=DEFAULT_CONCURRENCY)
    args = ap.parse_args()

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    traces_dir = Path(args.traces_dir)

    rows = []
    port = args.base_port
    total = len(args.policies) * len(args.patterns) * len(args.cache_ratios) * len(args.concurrency)
    done = 0

    for pattern in args.patterns:
        trace_file = traces_dir / args.trace_template.format(pattern=pattern)
        if not trace_file.exists():
            print(f"missing trace file {trace_file}, skipping pattern {pattern}", file=sys.stderr)
            continue
        for policy in args.policies:
            for ratio in args.cache_ratios:
                cache_size = max(1, int(ratio * args.num_keys))
                for conc in args.concurrency:
                    done += 1
                    port += 1
                    print(f"{done}/{total} policy {policy} pattern {pattern} "
                          f"cache_size {cache_size} concurrency {conc}")
                    row = run_one(args.server_bin, args.client_bin, args.host, port,
                                  policy, pattern, cache_size, args.shards, args.threads,
                                  trace_file, conc)
                    if row:
                        rows.append(row.split(","))

    with open(out_path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(CSV_HEADER)
        writer.writerows(rows)

    print(f"wrote {len(rows)} result rows to {out_path}")


if __name__ == "__main__":
    main()
