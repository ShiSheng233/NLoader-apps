# DOOM 示例与 WAD 检查

本目录包含三个不同的前台程序：

- `WADCHECK.NLB` 是独立 WAD 文件诊断，从配置的 NLoader 卡目录读取 `DOOM.WAD`，
  不包含引擎、不写卡、不调用图形接口。下面的格式说明与构建命令属于这个程序。
- `DOOM.NLB` 使用固定在 `engine/` 的完整 `doomgeneric` 引擎和 `port/` 移植层，
  读取实际 `GAME.WAD`，进入第一关并持续接收真实 SDK 按键，游戏菜单可正常退出。
- `DOOMREND.NLB` 是独立的无输入、90 帧绘图诊断。先前 run-016 验证的是这种绘图路径；
  持续交互程序已另外通过实际 QEMU 按键验收。构建与按键见 [移植说明](port/README.md)。

游戏资源不随源码提供；合成 WAD 检查与完整引擎运行分别记录。

`wad.c` 独立实现 WAD 文件访问。格式依据为 id Software 公布的
[w_wad.h](https://github.com/id-Software/DOOM/blob/master/linuxdoom-1.10/w_wad.h)
中的头与目录结构，以及
[w_wad.c](https://github.com/id-Software/DOOM/blob/master/linuxdoom-1.10/w_wad.c)
中的小端字段读取、末项优先查找规则；未复制 DOOM 引擎实现。

## 检查范围

- 接受 `IWAD` 和 `PWAD`；检查 12 字节头与每项 16 字节目录。
- 计数、目录位置、条目位置和长度按非负 32 位有符号整数处理。
- 先用剩余文件长度检查目录和非空内容范围，避免乘加溢出。
- 允许零条目文件、零长 marker、重复名称和重叠内容。零长 marker 的非负位置
  可以超过文件末尾，因为它不指向任何实际字节。
- 目录每批最多读取 512 字节，不申请与 WAD 或目录大小相等的内存。
- 本诊断最多接受 65,536 项；这是工作量上限，不是 WAD 格式本身的限制。
- `nlr_wad_find` 接受 1–8 个可打印 ASCII 字符，不区分大小写，从后向前查找。
- `nlr_wad_read` 读取指定条目内的一段内容，短读继续读取；失败、零进展和
  不合理返回长度均报告错误。SDK 不支持用 EOF 结束循环，因此按已知长度读取。
- 打开检查失败时尝试关闭；关闭失败保留句柄，交给调用者或 loader 清理。
  已打开对象的后续操作由调用者统一关闭，文件在此次读取期间必须保持不变。

`WADCHECK` 校验全部目录后，分别选首个和末个非空条目，读取其开头最多 64 字节。
同一条目兼为首尾时读取两次；若没有非空条目，两份摘要均为零。每份摘要使用
32 位 FNV-1a，初值 2166136261，乘数 16777619。

成功日志为 `W <项数> <首摘要十六进制> <尾摘要十六进制>`，长度不超过 SDK 的
32 字节日志上限。失败日志为 `wad rejected: <返回值>`。程序提交 `WAD ready`
或 `WAD rejected` 文本后自然返回；实际文字显示取决于菜单的自然刷新，不等待
截图，也不伪造 `ui_present` 成功。格式和样本读取通过不表示包含运行游戏所需
的全部资源，更不表示完整文件内容已经逐字节核对。

| 返回值 | 含义 |
|---|---|
| 0 | 目录和首尾样本检查通过，文件已关闭 |
| 1 | SDK 接口不可用 |
| 2 | 文件打开失败 |
| 3 | 读取或定位失败 |
| 4 | 标识或有符号字段不合法 |
| 5 | 范围越界或文件太短 |
| 6 | 超过目录项工作量上限 |
| 7 | 查找名称未找到，仅模块接口使用 |
| 8 | 调用参数不合法 |
| 9 | 文件关闭失败，不能报告成功 |
| 10 | 重复打开仍持有文件的对象，仅模块接口使用 |

## 构建与本地检查

在仓库根目录指定尚不存在的输出：

```sh
tool/nloader/target/debug/nloader program \
  --decl sdk/examples/doom/wad_check.json \
  --source sdk/crt0.s \
  --source sdk/examples/doom/wad.c \
  --source sdk/examples/doom/wad_check.c \
  --include sdk \
  --out build/<本次材料>/WADCHECK.NLB \
  --manifest build/<本次材料>/MANIFEST
```

使用具备 AArch64 支持的 Clang；必要时用 `--cc` 指定路径。将程序放到
`NLOADER/APPS/WADCHECK.NLB`，`DOOM.WAD` 放到 `NLOADER/`。两者都通过正式卡目录
配置访问，不在程序中写死盘符。

在 `tool/nloader/` 下运行：

```sh
cargo test --locked -p nloader-build --test wad -- --ignored
```

测试以独立合成字节覆盖合法格式、重复名称、短读、越界、负值、截断、目录批次
边界、定位错误、读取错误和关闭失败，分别运行普通、优化和 ASan/UBSan 构建。
另用 AArch64 构建两次，比较程序与 MANIFEST，并独立读取执行头中的类型与版本。
测试不包含商业 WAD，不访问相机，也不替代 QEMU 或真实游戏数据验收。

## 完整引擎的现状与边界

引擎固定为 `doomgeneric@e257a09527062bfb986b6e5e0780839b6cbd5b71`，
保留上游 GPLv2 许可。2026-10-14 移植改用 ABI4 的时间、等待和 Y0Y1UV+A8 图形接口，
逐帧通过 `ui_present` 提交，结束时归还画面；使用明确的按键位映射。
已补齐所需 C 库及 data→data/BSS 指针支持，完整引擎主机运行通过。
以下2026-10-13的构建和 QEMU 结果属于旧 ABI3/RGBA 候选，不抵扣新接口验证。
当前使用 2 MiB 引擎内存池；512 KiB 程序载入区属于本次 QEMU 隔离配置，不代表
生产 B: 配置或真机内存已经验收。

实际 Freedoom 0.13.0 Phase 2 已通过原厂 NVMe/exFAT 流程读取。最终 `run-016`
使用 `visible-menu-candidate`，同一会话两次各完成 90 帧、退出 0，文件和全部
程序/图形分配清理，模拟器已停止。两次退出后的整个列表 RGB 均与运行前逐字节
相同，随后原厂 Setup 仍可导航，不能将此结果缩写成仅有返回码成功。

两张游戏原 PNG 均完整显示场景、武器和状态栏，可查看
[第一次游戏画面](../../../build/m3-doom-qemu-20261013/run-016/scenario-12-doom-1-frame-86.png)、
[第二次游戏画面](../../../build/m3-doom-qemu-20261013/run-016/scenario-23-doom-2-frame-82.png)及
[退出后的列表](../../../build/m3-doom-qemu-20261013/run-016/scenario-25-returned-list.png)。
完整结果见 `build/m3-doom-qemu-20261013/run-016/execution-check.json`。
此前遮挡、导航错误和退出黑屏样本仍保留，诊断原因与最终修复见
[M3 记录](../../../docs/M3.md)。

交互版另在 `build/m3-doom-input-20261013/run-001/` 通过12个原厂按键、实际
移动/转向/射击及松键、暂停、超过600帧持续运行、正常Quit与三种退出组合。
同会话四次进入/退出资源清理通过，四张返回列表的整个 RGB 与运行前一致；
模拟器已停止，结果与截图见 [M3记录](../../../docs/M3.md)。
独占全屏、完整引擎与扩展共存、性能和全地图预算均未验，M3 尚未完成。
完成计数不能代替实际原图核对；本地与 QEMU
结果也不代表真机或物理卡通过。
