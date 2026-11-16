#!/usr/bin/env python3
"""Scalability study for the report.

1. Index build time vs number of threads (POST /api/index), speedup S = T1/Tp
   and efficiency E = S/p.
2. Search throughput (requests/s) vs number of concurrent clients
   (POST /api/search, each client is a thread with its own keep-alive connection).

Results go to docs/results/: CSV files and PNG charts.

    pip install matplotlib
    python3 scripts/benchmark.py --max-threads 16 --max-clients 32
"""
import argparse
import csv
import http.client
import json
import os
import statistics
import threading
import time
from urllib.parse import urlparse

QUERIES = ["space movie", "galaxy telescope", "football team", "pasta recipe",
           "parallel algorithm", "ancient empire", "hospital doctor", "jazz concert"]


def request(conn, method, path, body=None):
    conn.request(method, path, json.dumps(body) if body is not None else None,
                 {"Content-Type": "application/json"})
    res = conn.getresponse()
    data = res.read()
    if res.status != 200:
        raise RuntimeError(f"{method} {path}: {res.status} {data[:200]}")
    return json.loads(data)


def connect(url, timeout=3600):
    u = urlparse(url)
    return http.client.HTTPConnection(u.hostname, u.port or 80, timeout=timeout)


def powers_of_two(limit):
    values, v = [], 1
    while v < limit:
        values.append(v)
        v *= 2
    return values + [limit]


def bench_index(url, max_threads, repeats):
    conn = connect(url)
    request(conn, "POST", "/api/index", {"threads": max_threads})  # warm the page cache
    rows = []
    for p in powers_of_two(max_threads):
        times = [request(conn, "POST", "/api/index", {"threads": p})["time_ms"] for _ in range(repeats)]
        t = statistics.median(times)
        rows.append({"threads": p, "time_ms": t})
        print(f"index  threads={p:3d}  time={t:8.0f} ms  runs={times}")
    t1 = rows[0]["time_ms"]
    for r in rows:
        r["speedup"] = round(t1 / r["time_ms"], 3)
        r["efficiency"] = round(r["speedup"] / r["threads"], 3)
    return rows


def bench_throughput(url, max_clients, seconds):
    rows = []
    for clients in powers_of_two(max_clients):
        stop = time.monotonic() + seconds
        counts, latencies = [0] * clients, []
        lock = threading.Lock()

        def client(i):
            conn = connect(url, timeout=60)
            local = []
            while time.monotonic() < stop:
                q = QUERIES[(i + counts[i]) % len(QUERIES)]
                started = time.perf_counter()
                request(conn, "POST", "/api/search", {"query": q, "mode": "keyword"})
                local.append((time.perf_counter() - started) * 1000)
                counts[i] += 1
            with lock:
                latencies.extend(local)

        threads = [threading.Thread(target=client, args=(i,)) for i in range(clients)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        rps = sum(counts) / seconds
        rows.append({"clients": clients, "rps": round(rps, 1),
                     "latency_ms": round(statistics.mean(latencies), 2)})
        print(f"search clients={clients:3d}  throughput={rps:8.1f} req/s  "
              f"avg latency={rows[-1]['latency_ms']} ms")
    return rows


def sample_queries(url):
    conn = connect(url)
    lines = ["| Запит | Знайдено (топ-3) | Час, мс |", "|---|---|---|"]
    for q in QUERIES:
        r = request(conn, "POST", "/api/search", {"query": q, "mode": "keyword"})
        top = ", ".join(x["doc_id"] for x in r["results"][:3]) or "—"
        lines.append(f"| {q} | {top} | {r['time_ms']} |")
    return "\n".join(lines)


def save_csv(path, rows):
    with open(path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def plot(index_rows, tp_rows, out):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    p = [r["threads"] for r in index_rows]
    fig, (a, b) = plt.subplots(1, 2, figsize=(11, 4))
    a.plot(p, [r["time_ms"] for r in index_rows], "o-")
    a.set(title="Час побудови індексу", xlabel="Кількість потоків", ylabel="Час, мс")
    a.set_xscale("log", base=2)
    a.set_xticks(p, labels=[str(x) for x in p])
    a.grid(alpha=.3)
    b.plot(p, [r["speedup"] for r in index_rows], "o-", label="Фактичне")
    b.plot(p, p, "--", color="gray", label="Ідеальне (лінійне)")
    b.set(title="Прискорення S = T1 / Tp", xlabel="Кількість потоків", ylabel="S")
    b.legend()
    b.grid(alpha=.3)
    fig.tight_layout()
    fig.savefig(os.path.join(out, "index_time_vs_threads.png"), dpi=150)

    fig, ax = plt.subplots(figsize=(6, 4))
    c = [r["clients"] for r in tp_rows]
    ax.plot(c, [r["rps"] for r in tp_rows], "o-")
    ax.set(title="Пропускна здатність пошуку", xlabel="Кількість одночасних клієнтів",
           ylabel="Запитів / с")
    ax.set_xscale("log", base=2)
    ax.set_xticks(c, labels=[str(x) for x in c])
    ax.grid(alpha=.3)
    fig.tight_layout()
    fig.savefig(os.path.join(out, "throughput_vs_clients.png"), dpi=150)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://localhost:8080")
    ap.add_argument("--max-threads", type=int, default=os.cpu_count())
    ap.add_argument("--max-clients", type=int, default=32)
    ap.add_argument("--repeats", type=int, default=3)
    ap.add_argument("--seconds", type=float, default=10)
    ap.add_argument("--out", default="docs/results")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    index_rows = bench_index(args.url, args.max_threads, args.repeats)
    tp_rows = bench_throughput(args.url, args.max_clients, args.seconds)
    save_csv(os.path.join(args.out, "index_time.csv"), index_rows)
    save_csv(os.path.join(args.out, "throughput.csv"), tp_rows)
    with open(os.path.join(args.out, "sample_queries.md"), "w") as f:
        f.write(sample_queries(args.url) + "\n")
    plot(index_rows, tp_rows, args.out)
    print(f"results saved to {args.out}/")


if __name__ == "__main__":
    main()