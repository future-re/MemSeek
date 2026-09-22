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

后续再逐层增加：

1. 不同数据分布；
2. 不同目标长度；
3. 不同 region 大小与并行度对比。
