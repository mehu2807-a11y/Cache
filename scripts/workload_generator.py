#!/usr/bin/env python3
import argparse
import random


def gen_zipfian(num_keys, num_requests, skew, write_ratio, rng):
    ranks = list(range(1, num_keys + 1))
    weights = [1.0 / (r ** skew) for r in ranks]
    total = sum(weights)
    weights = [w / total for w in weights]
    keys = rng.choices(ranks, weights=weights, k=num_requests)
    lines = []
    for k in keys:
        if rng.random() < write_ratio:
            lines.append(f"SET key{k} val{rng.randint(0, 999999)}")
        else:
            lines.append(f"GET key{k}")
    return lines


def gen_sequential(num_keys, num_requests, write_ratio, rng):
    lines = []
    i = 0
    while len(lines) < num_requests:
        k = (i % num_keys) + 1
        i += 1
        if rng.random() < write_ratio:
            lines.append(f"SET key{k} val{rng.randint(0, 999999)}")
        else:
            lines.append(f"GET key{k}")
    return lines


def gen_uniform(num_keys, num_requests, write_ratio, rng):
    lines = []
    for _ in range(num_requests):
        k = rng.randint(1, num_keys)
        if rng.random() < write_ratio:
            lines.append(f"SET key{k} val{rng.randint(0, 999999)}")
        else:
            lines.append(f"GET key{k}")
    return lines


GENERATORS = {
    "zipfian": gen_zipfian,
    "sequential": gen_sequential,
    "uniform": gen_uniform,
}


def build_trace(pattern, num_keys, num_requests, skew, write_ratio, seed):
    rng = random.Random(seed)
    warmup = [f"SET key{k} val0" for k in range(1, num_keys + 1)]

    if pattern == "zipfian":
        main = gen_zipfian(num_keys, num_requests, skew, write_ratio, rng)
    else:
        main = GENERATORS[pattern](num_keys, num_requests, write_ratio, rng)

    return warmup + main


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pattern", choices=sorted(GENERATORS.keys()), required=True)
    ap.add_argument("--num-keys", type=int, default=2000)
    ap.add_argument("--num-requests", type=int, default=50000)
    ap.add_argument("--skew", type=float, default=1.2)
    ap.add_argument("--write-ratio", type=float, default=0.1)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    lines = build_trace(args.pattern, args.num_keys, args.num_requests, args.skew, args.write_ratio, args.seed)
    with open(args.out, "w") as f:
        f.write("\n".join(lines) + "\n")

    print(f"wrote {len(lines)} lines to {args.out}")


if __name__ == "__main__":
    main()
