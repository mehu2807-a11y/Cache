#!/usr/bin/env python3
import argparse
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd


def plot_hit_rate_vs_cache_size(df, out_dir):
    patterns = sorted(df["pattern"].unique())
    fig, axes = plt.subplots(1, len(patterns), figsize=(5 * len(patterns), 4), sharey=True)
    if len(patterns) == 1:
        axes = [axes]

    for ax, pattern in zip(axes, patterns):
        sub = df[df["pattern"] == pattern]
        for policy, g in sub.groupby("policy"):
            g = g.groupby("cache_size", as_index=False)["hit_rate"].mean().sort_values("cache_size")
            ax.plot(g["cache_size"], g["hit_rate"], marker="o", label=policy)
        ax.set_title(pattern)
        ax.set_xlabel("cache size (keys)")
        ax.grid(alpha=0.3)
    axes[0].set_ylabel("hit rate")
    axes[0].legend()
    fig.suptitle("Hit rate vs cache size, by access pattern")
    fig.tight_layout()
    fig.savefig(out_dir / "hit_rate_vs_cache_size.png", dpi=150)
    plt.close(fig)


def plot_throughput_vs_concurrency(df, out_dir):
    max_cache = df["cache_size"].max()
    sub_all = df[df["cache_size"] == max_cache]
    patterns = sorted(sub_all["pattern"].unique())
    fig, axes = plt.subplots(1, len(patterns), figsize=(5 * len(patterns), 4), sharey=True)
    if len(patterns) == 1:
        axes = [axes]

    for ax, pattern in zip(axes, patterns):
        sub = sub_all[sub_all["pattern"] == pattern]
        for policy, g in sub.groupby("policy"):
            g = g.sort_values("concurrency")
            ax.plot(g["concurrency"], g["throughput_rps"], marker="o", label=policy)
        ax.set_title(pattern)
        ax.set_xlabel("concurrent clients")
        ax.set_xscale("log", base=2)
        ax.grid(alpha=0.3)
    axes[0].set_ylabel("throughput (req/s)")
    axes[0].legend()
    fig.suptitle(f"Throughput vs concurrency, cache_size {max_cache}")
    fig.tight_layout()
    fig.savefig(out_dir / "throughput_vs_concurrency.png", dpi=150)
    plt.close(fig)


def plot_latency_distribution(df, out_dir):
    max_conc = df["concurrency"].max()
    sub = df[df["concurrency"] == max_conc]
    grouped = sub.groupby(["pattern", "policy"])[["p50_us", "p95_us", "p99_us"]].mean().reset_index()

    patterns = sorted(grouped["pattern"].unique())
    policies = sorted(grouped["policy"].unique())
    metrics = ["p50_us", "p95_us", "p99_us"]

    fig, axes = plt.subplots(1, len(patterns), figsize=(5 * len(patterns), 4), sharey=True)
    if len(patterns) == 1:
        axes = [axes]

    width = 0.8 / len(metrics)
    for ax, pattern in zip(axes, patterns):
        sub_p = grouped[grouped["pattern"] == pattern].set_index("policy").reindex(policies)
        x = range(len(policies))
        for i, metric in enumerate(metrics):
            offsets = [xi + (i - 1) * width for xi in x]
            ax.bar(offsets, sub_p[metric].values, width=width, label=metric)
        ax.set_xticks(list(x))
        ax.set_xticklabels(policies)
        ax.set_title(pattern)
        ax.grid(alpha=0.3, axis="y")
    axes[0].set_ylabel("latency (microseconds)")
    axes[0].legend()
    fig.suptitle(f"Latency percentiles per policy at concurrency {max_conc}")
    fig.tight_layout()
    fig.savefig(out_dir / "latency_distribution.png", dpi=150)
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", default="../results/results.csv")
    ap.add_argument("--out-dir", default="../results")
    args = ap.parse_args()

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    df = pd.read_csv(args.results)
    if df.empty:
        raise SystemExit(f"{args.results} has no rows")

    plot_hit_rate_vs_cache_size(df, out_dir)
    plot_throughput_vs_concurrency(df, out_dir)
    plot_latency_distribution(df, out_dir)

    print(f"wrote plots to {out_dir}")


if __name__ == "__main__":
    main()
