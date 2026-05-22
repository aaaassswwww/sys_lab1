# 系统程序设计 Lab 1 Part A 实验报告模板

## 1. 实验目的

本实验旨在通过对 `read()`、`getc()` / `fgetc()`、`fread()`、自实现 `my_fread()`、`write()`（有无 `O_SYNC`）的性能测试，理解以下问题：

- 系统调用开销与缓冲区大小之间的关系
- 标准 I/O 库的用户态缓冲机制如何影响性能
- 自定义缓冲读取函数与标准库实现之间的差异
- `O_SYNC` 对写入延迟与吞吐量的影响

## 2. 实验环境

- 操作系统：
- 内核版本：
- 文件系统类型：
- 运行环境：物理机 / WSL2 / 虚拟机
- CPU：
- 内存：
- 存储设备类型：SSD / HDD
- 编译器版本：`gcc --version`
- 编译选项：`-O2 -Wall -Wextra -Wpedantic -std=c11`

说明：

- 如果使用的是 WSL2 或虚拟机，需要说明这会对缓存行为、磁盘路径和 I/O 时延带来一定影响。
- 如果测试文件位于 `/mnt/c/...` 之类的 Windows 挂载路径，需要明确写出，因为这会显著影响结果。

## 3. 实验内容与方法

### 3.1 测试对象

本实验测试以下 6 组 I/O 路径：

- `read()`
- `getc()`
- `fgetc()`
- `fread()`
- `my_fread()`
- `write()` with / without `O_SYNC`

### 3.2 测试数据规模

- 测试文件大小：
- 写入测试总字节数：
- `BUFFSIZE` 取值：

建议填写为：

```text
1, 2, 4, 8, 16, 32, 64, 128, 256, 512,
1024, 2048, 4096, 8192, 16384, 32768, 65536, 16777216
```

### 3.3 测试方法

- 使用 `clock_gettime(CLOCK_MONOTONIC)` 统计 wall clock time
- 使用 `getrusage(RUSAGE_SELF)` 统计 user time 和 system time
- 每组测试至少运行：
- 对结果的处理方式：单次记录 / 多次平均 / 去掉异常值后平均

### 3.4 实验命令

编译：

```bash
make
```

生成测试文件：

```bash
dd if=/dev/urandom of=testfile bs=1M count=500
```

运行测试：

```bash
./parta_bench --input ./testfile --output results.csv --write-dir .
```

绘图：

```bash
python3 plot_results.py results.csv figures
```

## 4. 实验结果

### 4.1 原始数据表

在此插入 `results.csv` 的整理结果。建议按以下维度分表：

- `read()` vs `BUFFSIZE`
- `fread()` vs `BUFFSIZE`
- `my_fread()` vs `BUFFSIZE`
- `write()` no `O_SYNC` vs `BUFFSIZE`
- `write()` with `O_SYNC` vs `BUFFSIZE`
- `getc()` vs `fgetc()`

### 4.2 图表

建议插入以下图：

- `read_throughput.png`
- `read_wall_time.png`
- `write_throughput.png`
- `write_wall_time.png`
- `getc_vs_fgetc.png`

图注示例：

> 图 1 展示了 `read()`、`fread()` 与 `my_fread()` 在不同缓冲区大小下的吞吐量变化趋势。

## 5. 结果分析

### 5.1 `read()` 与缓冲区大小的关系

可以从以下角度展开：

- 小缓冲区时系统调用次数很多，导致系统调用开销主导总时间
- 随着 `BUFFSIZE` 增大，吞吐量先明显上升
- 超过一定阈值后收益减弱，甚至可能出现波动
- 分析最佳区间是否接近页大小、文件系统块大小或 stdio 默认缓冲大小

### 5.2 为什么 `getc()` / `fgetc()` 比逐字节 `read()` 快

可以从以下角度展开：

- `getc()` / `fgetc()` 对用户代码表现为逐字符读取，但底层并不是每次都触发系统调用
- stdio 在用户态维护缓冲区，只有缓冲耗尽时才调用底层 `read()`
- 因此它避免了逐字节 `read()` 的高频内核态切换

### 5.3 `getc()` 与 `fgetc()` 的差异

可分析：

- `getc()` 常被实现为宏，可能减少函数调用开销
- `fgetc()` 通常为函数形式，可能略慢
- 如果差异很小，也应解释原因，例如现代编译器优化、磁盘 I/O 已掩盖函数开销等

### 5.4 `fread()` 与 `my_fread()` 的差异

可分析：

- `fread()` 作为成熟标准库实现，缓冲、分支、边界处理通常更完善
- `my_fread()` 若内部缓冲大小固定，可能在部分区间表现接近 `fread()`
- 如果差距明显，可讨论内部缓冲策略、拷贝次数、实现细节等原因

### 5.5 `write()` 与 `O_SYNC` 的影响

重点分析：

- 不带 `O_SYNC` 时，数据通常先进入页缓存，`write()` 返回更快
- 带 `O_SYNC` 时，每次写入都更接近“落盘完成”语义，延迟显著增大
- 小缓冲区下 `O_SYNC` 影响通常更夸张，因为同步提交次数更多
- 讨论内核缓冲区、脏页回写和吞吐量下降的原因

## 6. 思考与讨论

### 6.1 实验误差来源

- 页面缓存未完全清空
- 多次运行之间系统负载不同
- WSL2 / 虚拟机引入附加 I/O 层
- 测试文件所在磁盘或挂载路径不同
- 后台程序、杀毒软件或索引服务影响磁盘行为

### 6.2 结果是否符合预期

可以围绕以下问题作答：

- 为什么曲线不是严格单调的？
- 为什么某些超大 `BUFFSIZE` 没有继续提升？
- 为什么 wall time 与 user/sys time 的比例会变化？

### 6.3 可以如何改进实验

- 每个配置重复多次并求平均值、标准差
- 区分冷缓存与热缓存实验
- 增加 `fsync()`、`fdatasync()` 等对照组
- 记录 CPU 利用率、上下文切换次数等额外指标

## 7. 结论

在此用 1 到 3 段总结你的核心结论，例如：

- `read()` 的性能对缓冲区大小高度敏感，小块读受到系统调用开销限制
- stdio 的用户态缓冲显著提升了逐字符接口的实际性能
- `fread()` 与 `my_fread()` 的差距反映了库实现优化的重要性
- `O_SYNC` 显著降低写入吞吐量，但增强了数据持久化保证

## 8. 附录

### 8.1 关键命令

```bash
make
./parta_bench --input ./testfile --output results.csv --write-dir .
python3 plot_results.py results.csv figures
```

### 8.2 提交文件建议

```text
partA/
├── main.c
├── Makefile
├── README.md
├── plot_results.py
├── results.csv
├── figures/
└── report.pdf
```
