# Scan benchmark

当前 benchmark 只保留第一层：测量一块固定大小的随机内存上，
`MemoryScanner::scanExact` 的扫描速度。

## 代码结构

```text
scan_benchmark.cpp            程序入口
scan_benchmark_helpers.*      生成测试数据、报告吞吐量
scan_exact_benchmark.cpp      一个基础 scanExact benchmark
```

运行流程只有四步：

```text
生成随机 buffer
    ↓
包装成 MemoryRead
    ↓
反复调用 MemoryScanner::scanExact
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

也可以使用脚本：

```bash
./benchmarks/quick_perf.sh
```

## 当前 benchmark

`BM_ScanExactRandom` 使用 4 MiB 的随机数据和一个 `uint32_t` 搜索目标。
数据只在 benchmark 开始前生成一次，不把准备数据的时间算进扫描时间。

后续再逐层增加：

1. 不同数据分布；
2. 不同目标长度；
3. 与其他算法对比；
4. 进程内存读取和大 region 场景。

这些内容暂时不放进第一层，避免 benchmark 的基本原理被辅助代码淹没。
