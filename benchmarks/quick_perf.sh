#!/usr/bin/env bash
# 快速扫描性能测试脚本
#
# 一次运行即可看到:
#   1. 自己的实现 (MemoryScanner::scanExact) 各场景的耗时与吞吐量
#   2. 行业标准算法 (glibc memmem / std::search / Boyer-Moore-Horspool) 的参照
#   3. 自动配对的倍数对比
#
# 用法:
#   ./benchmarks/quick_perf.sh                                    # 全部基准
#   ./benchmarks/quick_perf.sh --filter BM_ScanExactU32Random     # 只跑指定基准

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build"
BENCH_DIR="${BUILD_DIR}/benchmarks"
BINARY="${BENCH_DIR}/scan_benchmark"

FILTER="BM_Scan|BM_Baseline"

# ---------------------------------------------------------------- 参数解析
while [[ $# -gt 0 ]]; do
    case "$1" in
        --filter)
            FILTER="${2:?--filter 需要一个基准名}"; shift 2 ;;
        -h|--help)
            sed -n '2,11p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)
            echo "未知参数: $1 (用 --help 查看用法)"; exit 1 ;;
    esac
done

# ---------------------------------------------------------------- 构建
if [[ ! -x "${BINARY}" ]]; then
    echo "==> 基准测试程序不存在,开始配置并构建 (Release)..."
    cmake -S "${ROOT}" -B "${BUILD_DIR}" \
          -DMEMSEEK_BUILD_BENCHMARKS=ON \
          -DCMAKE_BUILD_TYPE=Release > /dev/null
    cmake --build "${BUILD_DIR}" --target scan_benchmark --parallel
fi

# ---------------------------------------------------------------- 运行 + 打印表格
echo "==> 运行扫描性能基准 (${FILTER}) ..."
echo ">>> 自研实现: BM_ScanExact* -> MemoryScanner::scanExact"
echo ">>> 对照实现: BM_Baseline* -> glibc memmem / std::search / Boyer-Moore-Horspool"
RAW="$(mktemp)"
"${BINARY}" \
    --benchmark_filter="${FILTER}" \
    --benchmark_out="${RAW}" \
    --benchmark_out_format=json \
    --benchmark_min_time=0.3s > /tmp/bench_console.log 2>&1 || {
        cat /tmp/bench_console.log; exit 1; }

python3 "${ROOT}/benchmarks/compare.py" --show "${RAW}"
rm -f "${RAW}"
