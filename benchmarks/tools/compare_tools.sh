#!/usr/bin/env bash
# 外部扫描工具对比 harness。
#
# 启动一个固定数据的 fixture 进程(scan_fixture),然后对同一个进程分别运行
# scanmem 与 memseek_scan,报告 wall time 和匹配数。
#
# 用法:
#   ./benchmarks/tools/compare_tools.sh
#   SIZE=$((512*1024*1024)) ITER=5 ./benchmarks/tools/compare_tools.sh
#   SCANMEM=/path/to/scanmem ./benchmarks/tools/compare_tools.sh
#
# 环境变量:
#   SIZE      fixture 缓冲区大小(字节),默认 256 MiB
#   VALUE     目标值,默认 0xDEADBEEF
#   ITER      每个工具重复次数,默认 3
#   SCANMEM   scanmem 可执行文件路径;默认在 PATH 中查找,找不到则跳过
#   BUILD_DIR 构建目录,默认 <repo>/build

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build}"
TOOLS_DIR="${BUILD_DIR}/benchmarks"
FIXTURE="${TOOLS_DIR}/scan_fixture"
MEMSEEK_SCAN="${TOOLS_DIR}/memseek_scan"

SIZE="${SIZE:-$((256 * 1024 * 1024))}"
VALUE="${VALUE:-0xDEADBEEF}"
ITER="${ITER:-3}"
SCANMEM="${SCANMEM:-$(command -v scanmem || true)}"

# ---------------------------------------------------------------- 参数解析
while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)
            echo "未知参数: $1 (用 --help 查看用法)"; exit 1 ;;
    esac
done

# ---------------------------------------------------------------- 构建
need_build=0
[[ -x "${FIXTURE}" ]] || need_build=1
[[ -x "${MEMSEEK_SCAN}" ]] || need_build=1
if [[ "${need_build}" -eq 1 ]]; then
    echo "==> 构建 scan_fixture / memseek_scan (Release)..."
    cmake -S "${ROOT}" -B "${BUILD_DIR}" \
          -DMEMSEEK_BUILD_BENCHMARKS=ON \
          -DCMAKE_BUILD_TYPE=Release > /dev/null
    cmake --build "${BUILD_DIR}" \
          --target scan_fixture memseek_scan --parallel
fi

if [[ ! -x "${FIXTURE}" || ! -x "${MEMSEEK_SCAN}" ]]; then
    echo "错误: 找不到 ${FIXTURE} 或 ${MEMSEEK_SCAN}" >&2
    exit 1
fi

# ---------------------------------------------------------------- 计时工具
now_ms() { date +%s%3N; }

median() {
    sort -n | awk '{a[NR]=$1} END {
        if (NR == 0) { print 0; exit }
        if (NR % 2) { print a[(NR + 1) / 2] }
        else { print int((a[NR / 2] + a[NR / 2 + 1]) / 2) }
    }'
}

# 运行命令 ITER 次,输出 wall time 中位数(ms)
time_wall_ms() {
    local times=()
    for ((i = 0; i < ITER; ++i)); do
        local start end
        start="$(now_ms)"
        "$@" > /dev/null 2>&1 || true
        end="$(now_ms)"
        times+=("$((end - start))")
    done
    printf '%s\n' "${times[@]}" | median
}

# ---------------------------------------------------------------- 启动 fixture
INFO_FILE="$(mktemp)"
FIXTURE_PID=""
cleanup() {
    [[ -n "${FIXTURE_PID}" ]] && kill "${FIXTURE_PID}" 2>/dev/null || true
    rm -f "${INFO_FILE}"
}
trap cleanup EXIT

echo "==> 启动 fixture (size=${SIZE}, value=${VALUE})..."
"${FIXTURE}" --size "${SIZE}" --value "${VALUE}" > "${INFO_FILE}" &
FIXTURE_PID=$!

for _ in $(seq 1 100); do
    [[ -s "${INFO_FILE}" ]] && break
    sleep 0.1
done
INFO="$(head -n1 "${INFO_FILE}")"
TARGET_PID="$(printf '%s' "${INFO}" | sed -E 's/.*"pid":([0-9]+).*/\1/')"

if [[ -z "${TARGET_PID}" || "${TARGET_PID}" == "0" ]]; then
    echo "错误: 无法读取 fixture 信息: ${INFO}" >&2
    exit 1
fi
echo "    fixture pid=${TARGET_PID}  ${INFO}"

# ---------------------------------------------------------------- 运行工具
declare -a ROWS=()

run_memseek() {
    local out matches scan_ms wall_ms
    out="$("${MEMSEEK_SCAN}" --pid "${TARGET_PID}" --value "${VALUE}")"
    matches="$(printf '%s' "${out}" | sed -E 's/.*matches=([0-9]+).*/\1/')"
    scan_ms="$(printf '%s' "${out}" | sed -E 's/.*elapsed_ms=([0-9.]+).*/\1/')"
    wall_ms="$(time_wall_ms "${MEMSEEK_SCAN}" --pid "${TARGET_PID}" --value "${VALUE}")"
    ROWS+=("memseek|${wall_ms}|${scan_ms}|${matches}")
}

run_scanmem() {
    local total base out matches scan_ms
    total="$(time_wall_ms "${SCANMEM}" -p "${TARGET_PID}" -c \
        "option scan_data_type uint32; ${VALUE}; quit")"
    base="$(time_wall_ms "${SCANMEM}" -p "${TARGET_PID}" -c "quit")"
    scan_ms=$((total - base))
    ((scan_ms < 0)) && scan_ms=0

    out="$("${SCANMEM}" -p "${TARGET_PID}" -c \
        "option scan_data_type uint32; ${VALUE}; quit" 2>&1 || true)"
    matches="$(printf '%s\n' "${out}" \
        | grep -oE 'we currently have [0-9]+ matches' \
        | grep -oE '[0-9]+' | tail -1 || true)"
    [[ -z "${matches}" ]] && matches="0"
    ROWS+=("scanmem|${total}|${scan_ms}|${matches}")
}

if [[ -x "${MEMSEEK_SCAN}" ]]; then
    run_memseek
fi

if [[ -n "${SCANMEM}" && -x "${SCANMEM}" ]]; then
    run_scanmem
else
    echo "==> 未找到 scanmem,已跳过 (可 apt install scanmem 或用 SCANMEM= 指定路径)"
fi

# ---------------------------------------------------------------- 输出
echo
printf '%-10s %14s %14s %10s\n' "工具" "wall(ms)" "scan(ms)" "matches"
printf '%-10s %14s %14s %10s\n' "----------" "--------------" "--------------" "----------"
for row in "${ROWS[@]}"; do
    IFS='|' read -r name wall scan matches <<< "${row}"
    printf '%-10s %14s %14s %10s\n' "${name}" "${wall}" "${scan}" "${matches}"
done

echo
echo "说明: scanmem 的 scan(ms) = total - 空跑基线(启动+attach);"
echo "      memseek 的 scan(ms) 为进程内计时,wall(ms) 含进程启动。"
echo "      两者都扫描 fixture 进程的所有可读写 region,数据完全一致。"
