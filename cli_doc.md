# MemSeek CLI 命令与功能说明

MemSeek 提供一个有状态的交互式 CLI。同一进程内会保存目标 PID、最近一次扫描条件、候选基线以及最近一次筛选前的候选值，因此首次 `scan` 后可以连续执行多次 `filter`。

```bash
memseek [--workers <数量>] [--byte-order native|little|big]
```

```text
MemSeek interactive CLI 0.1.0
Type 'help' for commands.
memseek> attach 1234
Attached to process 1234
memseek[1234]> scan --type u32 --value 100
Found 1842 candidates
memseek[1234]> filter changed
1842 -> 37 candidates
memseek[1234]> results --limit 20
...
memseek[1234]> quit
```

## 全局选项

| 选项 | 功能 |
| --- | --- |
| `--help` | 显示启动参数帮助并退出 |
| `--version` | 显示 MemSeek 版本并退出 |
| `--workers <数量>` | 指定至少为 1 的扫描线程数；省略时使用硬件并发数，无法探测时使用 1 |
| `--byte-order <顺序>` | 数值扫描的默认字节序，可选 `native`、`little`、`big`；默认 `native` |

`--byte-order` 只影响数值匹配。字符串和原始字节数组始终按输入字节原样匹配。

## 交互式命令

命令、类型名、模式名和选项值均区分大小写。字符串含空格时必须使用单引号或双引号；引号内可以用反斜杠转义下一个字符。

### `attach <PID>`

验证目标进程的内存映射可读后切换会话。成功 attach（包括重新 attach 相同 PID）会清除旧基线；验证失败时保留原会话。

```text
memseek> attach 1234
memseek[1234]>
```

### `scan`：创建候选基线

```text
scan --type TYPE [--mode exact] --value VALUE [过滤选项]
scan --type TYPE --mode greater|less --value VALUE [过滤选项]
scan --type TYPE --mode range --min MIN --max MAX [过滤选项]
```

示例：

```text
scan --type u32 --value 100
scan --type string --value "hello world"
scan --type bytes --value "48 8B 05"
scan --type i64 --mode greater --value -10 --region heap --protection write
scan --type u16 --mode range --min 10 --max 20
scan --type double --mode range --min 1.5 --max 2.5 --region anonymous
```

支持的值类型为 `u8`、`u16`、`u32`、`u64`、`i8`、`i16`、`i32`、`i64`、`float`、`double`、`string` 和 `bytes`。字节数组接受空格、冒号或逗号分隔的两位十六进制字节。

扫描模式：

- `exact`：精确匹配，支持全部值类型。
- `greater`：匹配大于目标值的数值。
- `less`：匹配小于目标值的数值。
- `range`：匹配闭区间 `[min, max]` 内的数值，要求 `min <= max`。

区域过滤器只能指定一次：

- `--region heap|stack|anonymous|module`：省略时扫描全部已知区域类型。
- `--protection read|write|execute`：省略时扫描可读区域。扫描始终要求区域可读，因此 `write` 表示可读且可写，`execute` 表示可读且可执行。

### `filter changed|unchanged`

重新读取候选地址，并在成功后用幸存候选创建下一份基线。

```text
filter changed
filter unchanged
```

- `changed`：保留字节值发生变化的地址。
- `unchanged`：保留字节值没有变化的地址。

尚未成功执行 `scan` 时会返回 `no baseline available; run scan first`。失败的 filter 不会修改当前基线。

### `results`：查看或导出候选

```text
results
results 20
results --limit 20
results --format text
results --format json
results --limit 100 --format json
```

文本格式默认最多显示 20 条。每条结果包含地址、最近一次成功 scan/filter 捕获的当前字节、最近一次 filter 前的字节（如果可用）、当前所属映射、区域类型、权限和路径。结果不会被隐式实时刷新；已经不在当前映射中的地址显示为 `unmapped`。

JSON 格式在未指定 `--limit` 时导出全部候选。地址使用十六进制字符串，避免 JSON 数字精度丢失；无法解析的区域为 `null`。

```json
{
  "pid": 1234,
  "candidate_count": 1,
  "returned_count": 1,
  "results": [
    {
      "address": "0x7ffd1234",
      "current_bytes": "64 00 00 00",
      "previous_bytes": "63 00 00 00",
      "region": {
        "start": "0x7ffd0000",
        "end": "0x7ffe0000",
        "size": 65536,
        "protection": "rw-",
        "type": "stack",
        "path": "[stack]"
      }
    }
  ]
}
```

位置 limit 和 `--limit` 不能同时出现；`--format` 只接受 `text` 或 `json`。

### `regions`：查看内存映射

```text
regions
regions --region heap
```

列出当前进程中符合条件的可读映射，包含起止地址、字节大小、权限、区域类型和路径。

### `info`

显示当前 PID、`attached`/`scanned` 状态、候选数量、最近一次成功扫描条件和启动时选择的字节序。

### `reset`

清除候选基线、上一轮字节和最近扫描条件，但保留当前 PID。

### `help`、`quit`、`exit`

`help` 显示交互命令帮助；`quit` 和 `exit` 结束会话。输入空行或纯空白行不会执行命令。

## 推荐流程

```text
memseek> attach 1234
memseek[1234]> scan --type u32 --value 100 --region heap
memseek[1234]> filter changed
memseek[1234]> filter unchanged
memseek[1234]> results --limit 20
memseek[1234]> reset
memseek[1234]> quit
```

MemSeek 当前依赖 Linux `/proc` 和进程内存读取接口。扫描其他进程需要相应权限；进程不存在、映射不可读或权限不足时会输出错误，并保留上一份有效会话状态。
