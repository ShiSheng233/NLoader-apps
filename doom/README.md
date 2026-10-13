# WAD 读取与检查

这是 M3 的独立前台诊断程序，不包含 DOOM 引擎或游戏数据。它从配置的
NLoader 卡目录读取 `DOOM.WAD`，不写卡、不调用图形接口，不宣称游戏可玩。

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

## 完整 DOOM 尚缺的条件

当前仅提供 WAD 文件访问。SDK 像素输出、原始输入和时钟/睡眠仍需实现和验证；
已显示的固定诊断图不等于连续、独占全屏绘制。

候选引擎 [doomgeneric](https://github.com/ozkl/doomgeneric) 提供绘帧、按键、
毫秒时钟和睡眠的移植入口，但还依赖 C 标准库，默认游戏内存池为 6 MiB。
现有打包器还不支持其菜单初值中指向可写数据的指针。本示例未引入该引擎；
后续应固定源码版本、保留其许可证，并分别验证内存、打包、输入、连续显示和
退出后返回列表。游戏资源须另外提供，不能把合成 WAD 验证记作 DOOM 验收。
