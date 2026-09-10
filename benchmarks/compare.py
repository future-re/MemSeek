#!/usr/bin/env python3
"""解析 scan_benchmark 的 JSON 结果,打印可读的吞吐量表格和前后对比。

用法:
  compare.py --show results.json        # 打印单次结果的吞吐量表
  compare.py                            # 对比 .benchmarks/ 下 before.json 与 after.json
  compare.py before.json after.json     # 显式指定两个文件对比
"""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / ".benchmarks"

# json 中人类可读的指标名
BYTES_PER_SEC = "bytes_per_second"


def load_results(path: Path) -> dict:
    with open(path) as f:
        return json.load(f)


def fmt_bytes_per_sec(v: float) -> str:
    if v >= 1024**3:
        return f"{v / 1024**3:6.2f} Gi/s"
    if v >= 1024**2:
        return f"{v / 1024**2:6.1f} Mi/s"
    return f"{v / 1024:6.1f} Ki/s"


def fmt_time(ns: float) -> str:
    if ns >= 1e6:
        return f"{ns / 1e6:8.2f} ms"
    if ns >= 1e3:
        return f"{ns / 1e3:8.1f} us"
    return f"{ns:8.0f} ns"


def extract(path: Path) -> dict:
    """返回 {基准名: (时间ns, bytes_per_second)}"""
    data = load_results(path)
    unit_to_ns = {"ns": 1.0, "us": 1e3, "ms": 1e6, "s": 1e9}
    out = {}
    for bench in data.get("benchmarks", []):
        name = bench["name"]
        raw_time = bench.get("real_time") or 0.0
        time_ns = raw_time * unit_to_ns.get(bench.get("time_unit", "ns"), 1.0)
        bps = bench.get(BYTES_PER_SEC)
        # SizeScaling 的多个 Arg 都在同一个名字里,CMake 已用 /N 区分
        out[name] = (time_ns, bps)
    return out


def show_single(path: Path) -> None:
    results = extract(path)
    if not results:
        print(f"警告: {path} 中没有基准结果")
        return
    print(f"\n{'基准场景':<42} {'耗时':>10} {'吞吐量':>12}")
    print("-" * 70)
    for name, (time_ns, bps) in sorted(results.items()):
        throughput = fmt_bytes_per_sec(bps) if bps else "        -"
        print(f"{name:<42} {fmt_time(time_ns):>10} {throughput:>12}")

    # ---- 自己的实现 vs 行业标准算法/成熟工具 ----
    # 按场景后缀自动配对: BM_ScanExactU32Random
    #   ↔ BM_BaselineMemmemU32Random / BM_BaselineStdSearchU32Random /
    #     BM_BaselineHorspoolU32Random
    BASELINES = {
        "BM_BaselineMemmem": "glibc memmem",
        "BM_BaselineStdSearch": "std::search",
        "BM_BaselineHorspool": "Boyer-Moore-Horspool",
    }
    scan = {n[len("BM_ScanExact"):]: v[0] for n, v in results.items()
            if n.startswith("BM_ScanExact")}
    baselines = {
        label: {n[len(prefix):]: v[0] for n, v in results.items()
                if n.startswith(prefix)}
        for prefix, label in BASELINES.items()
    }

    rows = []
    for key in sorted(scan):
        t = scan[key]
        for label, table in baselines.items():
            if key in table and table[key] > 0:
                rows.append((key, label, table[key] / t))
    if rows:
        print("\n与行业标准算法对比 (>1.00x = 自己的实现更快)")
        print("-" * 70)
        print(f"{'场景':<26} {'参照算法':<26} {'倍数':>8}")
        for key, label, ratio in rows:
            print(f"{key:<26} {label:<26} {ratio:7.2f}x")
    print()


def compare(before: Path, after: Path) -> None:
    old = extract(before)
    new = extract(after)
    # SizeScaling 这类多参数基准按名字精确匹配
    common = sorted(set(old) & set(new))
    if not common:
        print("两个文件没有共同的基准,无法对比。")
        return

    print(f"\n{'基准场景':<42} {'之前':>10} {'之后':>10} {'提升':>9}")
    print("-" * 76)
    total_speedup = 0.0
    count = 0
    for name in common:
        old_t, old_bps = old[name]
        new_t, new_bps = new[name]
        if old_t > 0:
            speedup = old_t / new_t if new_t > 0 else float("inf")
            total_speedup += speedup
            count += 1
            color = ""
            if sys.stdout.isatty():
                color = "\033[32m" if speedup >= 1.05 else (
                    "\033[31m" if speedup <= 0.95 else "")
                reset = "\033[0m"
            else:
                reset = ""
            tag = f"{color}{speedup:8.2f}x{reset}" if speedup else "     -"
            print(f"{name:<42} {fmt_time(old_t):>10} {fmt_time(new_t):>10} {tag:>9}")
        if old_bps and new_bps:
            gain = (new_bps - old_bps) / old_bps * 100
            print(f"{'  └ 吞吐量变化':<42} "
                  f"{fmt_bytes_per_sec(old_bps):>10} "
                  f"{fmt_bytes_per_sec(new_bps):>10} {gain:>+8.1f}%")
    if count:
        print("-" * 76)
        print(f"平均加速比: {total_speedup / count:.2f}x\n")


def main() -> None:
    args = sys.argv[1:]
    if "--show" in args:
        idx = args.index("--show")
        show_single(Path(args[idx + 1]))
        return
    if len(args) >= 2:
        compare(Path(args[0]), Path(args[1]))
        return
    print("用法:")
    print("  compare.py --show results.json")
    print("  compare.py before.json after.json")


if __name__ == "__main__":
    main()
