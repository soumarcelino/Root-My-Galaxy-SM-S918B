# PORTING.md — 新架构 payload 适配方法论（闭源引擎 + 偏移补丁）

> 本文档解释 rmg-f731u 仓库中的 `cve-2026-43499-app.so`（131072B，md5 `3c82d4f678bd58846facf3e4ad356a33`）是怎么来的，
> 以及如何把它适配到任意三星机型。

## 1. 背景：闭源引擎是什么、从哪来

本仓库的 `cve-2026-43499-app.so`（131072B）是 **s9180-root-kit 多机型工具包**（原包 `payloads.pack`）中的
官方闭源 CVE-2026-43499 (GhostLock) 新架构引擎。该引擎特性：

- **不用 KernelSnitch / futex 时序侧信道**（老架构，三星上不可靠）
- **`/proc/slabinfo` 直接定位 mm_struct**（解析 `mm_struct %lu %lu...` 行）
- **`/proc/sys/kernel/random/boot_id` 泄 KASLR**
- **configfs/ashmem 写原语 + LD_PRELOAD 注入 + KernelSU late-load**
- 阶段标记：`preparing-kernel-access` → `locating-kernel` → `kernel-location-ready` →
  `verifying-kernel-access` → `starting-temporary-root` → `temporary-root-ready`

**引擎是通用的**：同一引擎换偏移表即可适配任意机型。证据：F731U 闭源 payload 与 S9180 闭源 payload
只差 36 字节（0.03%），全部是偏移常量 + 少量指令补丁。

## 2. 仓库文件说明

```
app-src/app/src/main/assets/cve-2026-43499-app.so   # App 实际加载的 payload（闭源引擎 + F731U 偏移）
app-src/app/src/main/assets/ksud-f731u-kdp           # KernelSU daemon（KDP 排除版）
app-src/app/src/main/jniLibs/arm64-v8a/libcve43499root.so  # root helper（闭源 root 组件）
kernelsu/ksud-f731u-kdp                              # 备份
kernelsu/android13-5.15.189_kernelsu.ko              # KernelSU 内核模块
support/cve-2026-43499-app.so                        # 备份
artifacts/f731u-F731USQS8GZF1/cve-2026-43499-app.so # 备份
artifacts/t870-T870XXS8DXH1/*                        # Tab S7（4.19）开源编译版（老架构，仅供对照）
```

## 3. q5q → F731U 补丁全过程（26 字节 / 17 处）

**基准**：`q5q-F9460TBS9GZF1__payload`（Z Fold 5 国行，131072B，md5 `31fab32a...`）
**产物**：`q5q-F731U-patched5.so`（131072B，md5 `3c82d4f6...`）

### 3.1 符号偏移修正（14 处，每处 1 字节，全部 +0x40）

F731U 与 q5q 内核均为 5.15.189 系列，符号布局整体偏移 **+0x40（64 字节）**。
在二进制中，每个符号引用是一条 `mov wN, #imm` / `mov xN, #-imm` 指令，立即数低位即符号偏移。

| 文件偏移 | 指令 | q5q 值 | F731U 值 | 含义 |
|---|---|---|---|---|
| 0x0067ed | `mov w26, #0xd2f8` | 0xd2f8 | **0xd338** | 符号偏移 +0x40 |
| 0x0068b9 | `mov x10, #-0x2d08` | -0x2d08 | **-0x2cc8** | 符号偏移 +0x40 |
| 0x006965 | `mov x10, #-0x2d08` | -0x2d08 | **-0x2cc8** | 同上 |
| 0x006b7d | `mov x8, #-0x2d08` | -0x2d08 | **-0x2cc8** | 同上 |
| 0x006bdd | `mov x8, #-0x2d08` | -0x2d08 | **-0x2cc8** | 同上 |
| 0x007471 | `mov w8, #0xf2a0` | 0xf2a0 | **0xf2e0** | anon_pipe_buf_ops +0x40 |
| 0x007479 | `mov x9, #-0xd60` | -0xd60 | **-0xd20** | 符号偏移 +0x40 |
| 0x00754d | `mov w13, #0xf2a0` | 0xf2a0 | **0xf2e0** | anon_pipe_buf_ops +0x40 |
| 0x007555 | `mov x10, #-0xd60` | -0xd60 | **-0xd20** | 符号偏移 +0x40 |
| 0x00765d | `mov w10, #0xf2a0` | 0xf2a0 | **0xf2e0** | anon_pipe_buf_ops +0x40 |
| 0x00766d | `mov x22, #-0x800` | -0x800 | **-0x7c0** | 符号偏移 +0x40 |
| 0x00789d | `mov x22, #-0xbd48` | -0xbd48 | **-0xbd08** | 符号偏移 +0x40 |
| 0x0078ad | `mov w21, #0x42b8` | 0x42b8 | **0x42f8** | physmap 偏移 +0x40 |
| 0x007af5 | `mov w9, #0xf2a0` | 0xf2a0 | **0xf2e0** | anon_pipe_buf_ops +0x40 |
| 0x007afd | `mov x13, #-0xd60` | -0xd60 | **-0xd20** | 符号偏移 +0x40 |

> ⚠️ 注意：`mov wN, #imm` 的立即数在 ARM64 指令中是 16 位编码（低位 16 位 + 移位）。
> 当偏移值变化 < 64KB 时，只改指令的低 16 位编码字节（通常是第二个字节）。
> 使用 capstone 反汇编定位 `mov wN, #imm` / `mov xN, #-imm` 指令后，
> 重新编码立即数并写回即可（推荐用 keystone 重新汇编整条指令，或手工按 ARM64 MOV 编码规则改字节）。

### 3.2 机型/授权检查绕过（2 处）

| 文件偏移 | q5q 指令 | F731U 指令 | 作用 |
|---|---|---|---|
| 0x0026dd | `cmp w0,#0; cset w8,ne`（7B，编码 `000071e8079f1a`） | `nop; mov w8,#0`（7B，编码 `2003d508008052`） | 强制 w8=0，绕过条件检查 |
| 0x009480 | `b.ne #0xa9b8`（4B，编码 `c1290054`） | `nop`（4B，编码 `1f2003d5`） | 强制跳过 grku 授权校验分支（`cmp w0,#1` 后） |

> 这两个补丁点与 s9180-root-kit 的 `patch_map.txt` 一致（payload 0x9480 处 `b.ne -> nop`）。
> 0x0026dd 是另一个机型相关检查（0x36dc 处 `cmp w0,#0; cset`），F731U 需要强制通过。

### 3.3 可复现补丁

仓库提供 `tools/patch_payload.py` + `tools/q5q-to-f731u.spec.json`，可精确重现 F731U 版：

```bash
python3 tools/patch_payload.py \
  q5q-F9460TBS9GZF1__payload.orig \
  tools/q5q-to-f731u.spec.json \
  cve-2026-43499-app.so
# 输出 md5 = 3c82d4f678bd58846facf3e4ad356a33（与仓库 assets 完全一致）
```

### 3.4 补丁后验证

1. **ELF 完整性**：`readelf -h` / `file` 确认仍是合法 ELF
2. **符号偏移自校验**：引擎内部有 `verifying-kernel-access` 阶段，偏移不对会安全拦截（不 panic）
3. **真机验证**：`temporary-root-ready` 日志出现 = 成功

## 4. 适配新机型通用流程

### 4.1 准备
- 目标机型固件（AP 文件）→ boot.img → 内核 Image → vmlinux-to-elf 恢复符号
- 参考仓库 `src/targets/<机型>/target.h`（开源符号偏移，可交叉验证）

### 4.2 找基准引擎
- 从 s9180-root-kit 包 `payloads/` 选**内核版本最接近**的机型 payload（5.15 系列选 q5q/dm3q，6.1 选 e3q 等）
- 或直接用本仓库 `support/cve-2026-43499-app.so`（F731U 版）作为基准

### 4.3 计算偏移差
- 目标机型符号偏移 vs 基准机型符号偏移 → 差值
- 如果差值 ≤ 0xFFFF（单 MOV 立即数范围）：只需改 MOV 指令立即数
- 如果差值 > 0xFFFF：需要改 `movk` 高 16 位（`lsl #16`）指令

### 4.4 打补丁
1. 用 capstone 反汇编 .text，找所有 `mov wN/xN, #imm` 引用目标符号的位置
2. 计算新立即数（基准值 ± 偏移差）
3. 用 keystone 重新汇编对应指令，写回 .so
4. 处理机型检查（参考 3.2：找到检查点，改成 nop / 恒 true）

### 4.5 配套组件
- **ksud**：不同内核需要不同 KDP 版（仓库提供 5.15 版，6.1/6.6 在 s9180-root-kit tools/ 有）
- **KernelSU 内核模块**：需与目标内核匹配（编译或用包内现成的）

### 4.6 测试
- CLI 直接跑 payload（`SLIDE_P0_OFFSET` 可强制偏移）→ 看阶段日志
- App 模式：替换 assets 里的 .so + targets-v3.json

## 5. 常见问题

- **`kernel page prepare mode=` 后重启**：KASLR slide 未匹配 / p0 偏移错误（KDP/RKP 拦截）
- **`[grku] verification failed E...`**：授权校验没绕干净，检查 3.2 的补丁点
- **偏移不匹配安全拦截**：引擎自校验，不会 panic，但也不会成功——说明还有偏移没改对
- **`ro.arch=exynos9810` 之类检测**：部分引擎有硬件检测，需绕过（F731U 没遇到，b5q 有）

## 6. 与开源老架构的区别

| | 开源老架构（KernelSnitch） | 闭源新架构（本仓库） |
|---|---|---|
| mm_struct 定位 | futex 时序侧信道盲扫（概率性） | /proc/slabinfo 直读（确定性） |
| KASLR 泄漏 | pipe oracle 碰撞 | boot_id + tracefs |
| 写原语 | pipe/configfs 碰撞 | configfs/ashmem 直接写 |
| 成功率 | 三星上低（1/24 甚至更低） | 高（attempt 1 即成功） |
| 稳定性 | 误判写坏内核对象 → 重启 | 自校验，偏移错安全拦截 |

**建议**：所有三星机型适配都优先用闭源新架构（本仓库的方法），不要走 KernelSnitch 老路径。

---
*整理：2026-08-13 · 依据 rmg-f731u-repo v0.2.36 实际补丁过程*
