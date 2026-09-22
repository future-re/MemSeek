# Scan benchmark

Benchmark 只测真实扫描路径：`MemoryScanner::scanRegion`，即对进程 region 走
`process_vm_readv` + 分块 + 匹配的完整流程。

`scanBuffer` 只是扫描算法本身，不涉及进程内存读取，因此它的正确性放在
`tests/integration/`，benchmark 里不再单独测量。

## 代码结构

```text
scan_benchmark.cpp            程序入口
scan_benchmark_helpers.*      生成测试数据、报告吞吐量
scan_region_benchmark.cpp     scanRegion 基准（256 MiB / 500 MiB / 1 GiB）
```

运行流程：

```text
生成随机 buffer,在其中植入若干目标值
    ↓
找到该 buffer 所属的进程 region
    ↓
反复调用 MemoryScanner::scanRegion(getpid(), region, target)
    ↓
报告耗时和 bytes/s
```

## 构建和运行

```bash
cmake -S . -B build \
  -DMEMSEEK_BUILD_BENCHMARKS=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --target scan_benchmark --parallel

./build/benchmarks/scan_benchmark
```

也可以使用脚本（脚本会自动切到 Release，否则测的是未优化的库）：

```bash
./benchmarks/quick_perf.sh
```

## 当前 benchmark

- `BM_ScanRegion256MB` / `BM_ScanRegion500MB` / `BM_ScanRegion1GB`：分别在
  256 MiB / 500 MiB / 1 GiB 的进程 region 上植入目标值，反复执行完整的
  `scanRegion`（读取 + 分块 + 扫描），并校验植入的匹配都能被找到。

数据只在 benchmark 开始前生成一次，不把准备数据的时间算进扫描时间。

## 外部工具对比 harness

除了进程内 benchmark，`benchmarks/tools/` 还提供了一个与真实外部扫描工具
（scanmem）在**同一份数据**上对比的 harness：

```text
tools/scan_fixture.cpp     固定数据的 fixture 进程(植入目标值后等待信号)
tools/memseek_scan.cpp     用 MemoryScanner::scanProcess 扫描 fixture 的参考实现
tools/compare_tools.sh     启动 fixture,分别运行各工具并报告 wall/scan 时间与匹配数
```

用法：

```bash
# 需要先安装 scanmem: sudo apt install scanmem
./benchmarks/tools/compare_tools.sh

# 或通过 CMake target
cmake --build build --target compare_tools

# 自定义参数
SIZE=$((512*1024*1024)) ITER=5 SCANMEM=/path/to/scanmem \
  ./benchmarks/tools/compare_tools.sh
```

注意：

- scanmem 与 memseek 都扫描 fixture 进程的所有**可读写 region**，数据完全一致；
- scanmem 的 `scan(ms)` 由「总耗时 − 空跑基线(启动 + attach)」估算，memseek 的
  `scan(ms)` 为进程内计时；
- scanmem 是单线程实现，memseek 默认并行，因此结果主要反映并行度与算法差异；
- 未安装 scanmem 时脚本会跳过该项，只跑 memseek。

后续再逐层增加：

1. 不同数据分布；
2. 不同目标长度；
3. 不同 region 大小与并行度对比；
4. 更多外部工具适配（PINCE 等）。
