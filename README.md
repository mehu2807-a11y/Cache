# CacheBench

A Redis-like in-memory key/value cache server written from scratch in
C++17 over raw TCP sockets, with three interchangeable eviction
policies (LRU, LFU, and a novel decayed/"windowed" LFU), plus a full
benchmarking harness that produces quantitative, plottable evidence for
which policy wins under which workload.

The goal isn't to reimplement Redis -- it's to build the concurrency and
eviction-policy internals from first principles and then *measure* them
rigorously, the same way you'd back-test trading strategies: generate
representative workloads, run every policy against every workload, and
let the numbers (hit rate, throughput, tail latency) say which design
choice actually wins, instead of asserting it.

## Table of contents
- [Architecture](#architecture)
- [Build](#build)
- [Run the server](#run-the-server)
- [Protocol](#protocol)
- [Eviction policies](#eviction-policies)
- [Benchmarking](#benchmarking)
- [Design trade-offs](#design-trade-offs)
- [Project layout](#project-layout)

## Architecture

```
                        ┌─────────────────────────┐
   client 1  ──TCP────▶ │                         │
   client 2  ──TCP────▶ │      TcpServer          │  accept loop on the
   client N  ──TCP────▶ │  (accept + dispatch)    │  main thread; each
                        └───────────┬─────────────┘  connection becomes
                                    │ enqueue(conn)     one task
                                    ▼
                        ┌─────────────────────────┐
                        │       ThreadPool        │  fixed-size worker
                        │  (bounded worker count) │  pool draining a
                        └───────────┬─────────────┘  shared task queue
                                    │ handleConnection()
                                    ▼
                        ┌─────────────────────────┐
                        │      ShardedCache       │  hash(key) % N
                        │  N independent shards   │  routes to one
                        └───────────┬─────────────┘  shard, each with
                                    │                 its own lock
                    ┌───────────────┼───────────────┐
                    ▼               ▼               ▼
             ┌────────────┐  ┌────────────┐  ┌────────────┐
             │ CacheStore │  │ CacheStore │  │ CacheStore │  each: mutex +
             │  (shard 0) │  │  (shard 1) │  │  (shard N) │  unordered_map
             │ + Eviction │  │ + Eviction │  │ + Eviction │  + one Eviction-
             │  Policy    │  │  Policy    │  │  Policy    │  Policy instance
             └────────────┘  └────────────┘  └────────────┘
```

**Core data store.** Each shard is a `std::unordered_map<string,
ValueEntry>` for O(1) key lookup. `ValueEntry` holds the value and an
optional TTL timestamp. Eviction bookkeeping (recency lists, frequency
buckets, etc.) lives entirely inside the shard's `EvictionPolicy`
object, not in the map itself -- `CacheStore` just calls
`onInsert`/`onAccess`/`onRemove` on whichever policy it was built with
and asks it for `evictCandidate()` when the shard is full.

**Eviction policies** are the interchangeable "strategy" layer: `LRU`,
`LFU`, and `Hybrid` (windowed/decayed LFU) all implement the same
`EvictionPolicy` interface, so `CacheStore` doesn't know or care which
one it's holding.

**Networking + concurrency.** A single acceptor loop calls `accept()`
and hands each new connection to a fixed-size `ThreadPool` as one task;
that task loops reading/parsing/responding to requests on that
connection until the client disconnects. The keyspace is partitioned
across `N` shards (default 4), each independently locked, so requests
for different keys don't serialize on one global mutex.

**Evaluation suite** (`scripts/` + `bench/bench_client.cpp`) generates
traces under three access-pattern distributions, replays them against a
running server with a configurable number of concurrent client
connections, and records hit rate, throughput, and latency percentiles
-- then plots hit-rate-vs-cache-size, throughput-vs-concurrency, and
latency-distribution charts, faceted by access pattern, with one line
per policy.

## Build

Requires a C++17 compiler and pthreads (Linux/macOS). Two options:

**CMake:**
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j
# produces build/cachebench_server and build/bench_client
```

**Direct g++ (no CMake needed):**
```bash
g++ -std=c++17 -O2 -Iinclude -pthread \
    src/main.cpp src/thread_pool.cpp src/tcp_server.cpp \
    -o cachebench_server

g++ -std=c++17 -O2 -pthread \
    bench/bench_client.cpp -o bench_client
```

## Run the server

```bash
./cachebench_server --port 6380 --capacity 10000 --shards 4 \
    --threads 64 --policy lru
```

| Flag         | Default | Meaning                                   |
|--------------|---------|--------------------------------------------|
| `--port`     | 6380    | TCP port to listen on                      |
| `--capacity` | 10000   | total key capacity, split evenly across shards |
| `--shards`   | 4       | number of independently-locked shards      |
| `--threads`  | 64      | worker-pool size (max concurrent connections) |
| `--policy`   | lru     | `lru`, `lfu`, or `hybrid`                  |

Talk to it with anything that speaks line-based TCP, e.g. `nc`:
```bash
$ nc 127.0.0.1 6380
SET foo bar
OK
GET foo
VALUE bar
EXPIRE foo 5
OK
GET foo          # (after 5s)
NULL
```

## Protocol

Plain-text, one command per line, one response per line:

| Command                        | Response            |
|---------------------------------|----------------------|
| `SET key value [ttl_seconds]`   | `OK`                 |
| `GET key`                        | `VALUE <value>` or `NULL` |
| `DEL key`                        | `OK` or `NOTFOUND`   |
| `EXPIRE key seconds`             | `OK` or `NOTFOUND`   |

Keys and values are single whitespace-free tokens. This is a deliberate
simplification (no quoted/binary-safe values, no RESP framing) --
the point of the project is the cache engine and eviction policies,
not full Redis wire-protocol compatibility.

## Eviction policies

- **LRU** (`include/lru_policy.hpp`) -- classic O(1) design: a doubly
  linked list ordered by recency plus a hashmap into it. Evicts the
  least-recently-touched key.
- **LFU** (`include/lfu_policy.hpp`) -- O(1) amortized frequency-bucketed
  design (per Redis's own approach): keys with equal access counts sit
  in the same bucket, most-recent-within-bucket at the front, so ties
  are broken by recency. Evicts the back of the lowest-frequency
  bucket.
- **Hybrid / windowed-decay LFU** (`include/hybrid_policy.hpp`) -- the
  project's own contribution. Classic LFU's failure mode is that a key
  which was hot last week but has gone cold still outranks a key that
  only just became popular, because raw counts never go down. This
  policy exponentially decays a key's score by a configurable factor
  for every time window that's elapsed since it was last touched,
  *before* incrementing it on access -- so popularity that isn't being
  refreshed gradually sinks toward eviction on its own, with no
  background sweep required. The cost: scores are now real-valued and
  keep changing with wall-clock time, so the O(1) integer frequency
  buckets LFU uses don't work anymore -- this policy keeps a sorted
  `score -> keys` index instead, trading LFU's O(1) amortized get/put
  for O(log n).

## Benchmarking

1. **Generate traces** for each access pattern:
   ```bash
   cd scripts
   for p in zipfian sequential uniform; do
     python3 workload_generator.py --pattern $p --num-keys 2000 \
       --num-requests 50000 --write-ratio 0.1 --out ../traces/$p.txt
   done
   ```
   - `zipfian`: a small fraction of keys get most of the traffic
     (realistic caching workload).
   - `sequential`: keys walked in strict round-robin order -- the
     classic pathology that defeats LRU, since every key looks
     "just used" right before it scrolls out of the window.
   - `uniform`: every key equally likely -- a no-locality control.

   Every trace begins with a warmup phase that `SET`s the full working
   set once, so "cache size" can be expressed as a fraction of a
   well-defined working-set size.

2. **Run the full sweep** (every policy × every pattern × several
   cache-size ratios × several concurrency levels), starting and
   stopping a freshly-configured server for each combination:
   ```bash
   python3 run_benchmarks.py \
       --server-bin ../build/cachebench_server \
       --client-bin ../build/bench_client \
       --traces-dir ../traces --num-keys 2000 \
       --out ../results/results.csv
   ```
   Defaults: policies `{lru, lfu, hybrid}`, cache-size ratios
   `{0.1, 0.25, 0.5, 0.75, 1.0}` of the working set, concurrency
   `{1, 4, 16, 64}`. All are overridable via CLI flags -- see
   `--help`.

3. **Plot the results:**
   ```bash
   python3 plot_results.py --results ../results/results.csv --out-dir ../results
   ```
   Produces three PNGs in `results/`:
   - `hit_rate_vs_cache_size.png` -- hit rate vs. cache size, one line
     per policy, faceted by access pattern.
   - `throughput_vs_concurrency.png` -- throughput vs. concurrent
     client count, one line per policy, faceted by access pattern.
   - `latency_distribution.png` -- p50/p95/p99 latency per policy at
     the highest tested concurrency, grouped by access pattern.

   `results/` in this repo already contains a small sample sweep
   (200-key working set, 2 cache sizes, 2 concurrency levels) so you
   can see the expected shape of the output without running anything;
   re-run the steps above with the full-size defaults for real numbers.

Python dependencies for the benchmarking scripts: `numpy` isn't
required (the workload generator uses the stdlib `random` module for
weighted sampling), but plotting needs `pandas` and `matplotlib`:
```bash
pip install pandas matplotlib
```

## Design trade-offs

**Thread pool vs. epoll.** A bounded thread pool where each accepted
connection is handed to a worker as one long-running task (see
`ThreadPool` in `include/thread_pool.hpp`) is simple to reason about
and lets the sharded-lock hashmap do the concurrency-control work, but
the number of concurrent connections is capped by the pool size, and
context-switch overhead grows with thread count. A single-threaded (or
multi-reactor) `epoll` event loop would scale to far more concurrent
connections without a thread per client, at the cost of turning every
request handler into an explicit state machine instead of a blocking
read loop. This project deliberately started with the thread-pool
design as the more common interview-relevant pattern; the benchmark
suite's throughput-vs-concurrency sweep is exactly the kind of
measurement that would tell you when it's time to switch.

**Sharded locking vs. one global lock.** Splitting the keyspace across
N independently-locked shards cuts contention under concurrent load,
but capacity and eviction order are enforced *per shard*, not globally
-- with very few shards and a heavily skewed (Zipfian) workload, a
shard that happens to hash more hot keys can evict more aggressively
than its neighbors even though the cache overall isn't full. More
shards reduce this effect but increase per-shard bookkeeping and
memory overhead.

**LRU vs. LFU vs. Hybrid.** LRU is right when recency predicts future
access (most real caches); it's exactly wrong for a sequential scan,
where "just accessed" carries zero predictive value. LFU fixes the
scan case but never forgets old popularity. The Hybrid policy is a bet
that decaying frequency over time captures LFU's resistance to scans
while still adapting to a shifting hot set -- whether that bet pays off
under a given workload is precisely what the benchmark suite is built
to check, not something to assume.

## Project layout

```
CacheBench/
├── CMakeLists.txt
├── include/                  # all headers (header-heavy by design --
│   ├── eviction_policy.hpp   # the eviction-policy interface, plus
│   ├── lru_policy.hpp        # LRU,
│   ├── lfu_policy.hpp        # LFU,
│   ├── hybrid_policy.hpp     # and the windowed-decay hybrid.
│   ├── cache_store.hpp       # one capacity-bounded, mutex-guarded shard
│   ├── sharded_cache.hpp     # N shards + policy factory
│   ├── thread_pool.hpp       # fixed worker pool + task queue
│   └── tcp_server.hpp        # accept loop + protocol parsing
├── src/
│   ├── main.cpp              # CLI flags, wiring, signal handling
│   ├── thread_pool.cpp
│   └── tcp_server.cpp
├── bench/
│   └── bench_client.cpp      # concurrent trace-replaying load generator
├── scripts/
│   ├── workload_generator.py # zipfian / sequential / uniform trace generator
│   ├── run_benchmarks.py     # full sweep orchestrator
│   └── plot_results.py       # renders the three deliverable plots
├── traces/                   # generated trace files (git-ignored)
└── results/                  # results.csv + PNGs (sample sweep included)
```

## License

MIT -- see [LICENSE](LICENSE).
