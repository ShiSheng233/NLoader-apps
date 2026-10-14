# DOOM 移植

`DOOM.NLB` 使用完整 doomgeneric 引擎，读取 `GAME.WAD`，直接进入第一关并持续运行。
它通过 ABI4 接收按键的按下、松开与当前按住状态；不再到 90 帧自动退出。
独立的 `DOOMREND.NLB` 保留无输入、90 帧后退出的绘图诊断，不能用诊断结果替代交互验证。

上游固定为 `ozkl/doomgeneric@e257a09527062bfb986b6e5e0780839b6cbd5b71`，保留 GPLv2 许可。
80 个原始源文件列在 `engine-sources.txt`，没有删改引擎源码。格式化器固定为
`mpaland/printf@d3b984684bb8a8bdc48cc7a1abecb93ce59bbe3e`；MIT 许可、来源与逐文件
SHA256 位于 `third_party/printf/`。

## 构建

从仓库根目录执行；输出与工作目录必须尚不存在：

```sh
bash sdk/examples/doom/port/build.sh \
  --out firmware/nlb/DOOM.NLB --manifest build/doom-play/MANIFEST \
  --work-dir build/doom-play/objects

bash sdk/examples/doom/port/build.sh --diagnostic \
  --out firmware/nlb/DOOMREND.NLB --manifest build/doom-render/MANIFEST \
  --work-dir build/doom-render/objects
```

构建需要 Git、支持 AArch64 的 Clang、`shasum`、Bash 和 Rust `nloader` CLI。
默认 CLI 为 `tool/nloader/target/release/nloader`；可用 `--nloader`、`--cc` 指定其他路径。
正式构建不依赖 Python 或研究目录。移植层使用 `-Wall -Wextra -Werror`，上游历史警告
保存在工作目录的日志中。

`doom_program.json` 与 `doom_render.json` 分别声明交互程序和绘图诊断，均要求 ABI4。
ABI3 的画面、按键和计时布局不兼容，不能用旧程序替代本次重新构建的程序。
引擎固定版本的 `I_Quit` 在通用平台只运行关闭回调；构建仅为 `i_system.c` 重命名该函数，
由移植层保留其关闭回调并调用 SDK 的退出接口。无需改写上游文件。

## 按键

| 相机按键 | 游戏中 | 菜单中 |
|---|---|---|
| 上 / 下 | 前进 / 后退 | 上 / 下选择 |
| 左 / 右 | 转向；按住 FULL 时侧移 | 调整选项 |
| AF-ON | 射击 | — |
| OK | 开门 / 使用 | Enter，选择与确认 |
| DISP | 打开游戏菜单 | Escape，取消或关闭 |
| INFO | 地图 | — |
| HALF | 按住奔跑 | — |
| FULL | 按住侧移 | — |
| 放大 / 缩小 | 下一 / 上一武器 | — |
| PLAY | 暂停 / 继续 | — |

表中的 INFO、HALF 和 FULL 仅在该机型的输入服务已经确认并提供这些事件时生效。
目前 Z8 已确认的输入不提供这三项；旧 FN1/FN2 没有经确认的新对应关系，不能据此改写映射。

正常退出：DISP 打开菜单，选择 `Quit Game`，再按 OK 确认。引擎提示中的字母确认键
也使用 OK。MENU 和删除键不映射到游戏，始终保留给 NLoader。

输入使用固定大小队列，重复按下不会重复产生边沿。队列溢出时，丢弃不完整历史并根据
`input_raw` 的当前按住状态补齐松开与按下；不按每次采样切换状态。失去输入焦点时释放
已交付的按住键。启动时已按住的键不会被当作新按下，直到松开或收到真实新边沿。
缺少输入服务或无效事件会明确失败，绘图诊断则不消费按键。

## 卡输入与能力边界

合法完整 IWAD 放在 profile 配置的 NLoader 根目录，命名为 `GAME.WAD`。
不要将引擎参数改为 `DOOM.WAD`：上游会据文件名固定选择 Doom 1，而忽略 Freedoom
Phase 2 的实际关卡类型。独立 `WADCHECK` 继续使用自己的 `DOOM.WAD`。

当前保留已测首关的 `-mb 2` 引擎内存池与无声参数。它不保证其他关卡或长时间运行预算。
文件层只允许读取，存档、配置写入、删除、改名及外部命令均不支持；不提供音频或网络。
游戏菜单仍来自原引擎，选择其未支持的保存功能不会获得写卡能力。

引擎拥有 320×200 整数像素缓冲，转换为 SDK 的 Y0Y1UV 与 A8 两个平面，使用 BT.601
的亮度偏移 16、色度偏移 128，相邻两像素共享色度。较大的画面使用居中整数倍放大，
边框为不透明黑色。每帧等待 `ui_present` 完成后才写下一帧；同一次运行持有画面，
正常结束时调用 `gfx_end` 释放，不把该调用当作逐帧显示。缺少平面、格式或行跨度不符
均明确失败。
文件、引擎分配和正常菜单退出都走原清理路径；不提前释放 loader 的图形存储。
SDK 图形完成不确定时由 loader 保留相关存储，不能因程序结束而报告释放成功。

## 本地检查

```sh
bash sdk/examples/doom/port/tests/run.sh --out build/doom-input-check --sanitize
bash sdk/examples/doom/port/tests/run.sh \
  --out build/doom-engine-check --wad /path/to/freedoom2.wad
```

测试覆盖只读文件与内存层、全部按键映射、按下/松开、启动按住键、重复事件、溢出恢复、
失焦释放和有界轮询；画面测试核对独立的颜色期望值、双平面、带填充的行跨度、整数放大、
计时回绕、等待转交、逐帧显示、单次释放与 ABI3 拒绝。完整引擎测试保留 90 帧诊断及
画面获取、显示和释放失败测试，并增加超过 90 帧的
交互运行、真实角色移动与弹药变化，以及游戏菜单 Quit 通过 SDK 正常退出。
这里的 SDK 与图形消费方是明确的本机测试实现，不等于原厂输入或 QEMU 通过。
成功的本机引擎测试输出 `.y0y1uva8`：先按行写入 Y0Y1UV 平面，再写入 A8 平面。

`observation.h` 定义只读观察记录，`observation.c` 从实际输入状态与引擎的 `gametic`、
角色位置、朝向、武器和弹药复制数据。构建将该对象排在最前，固定标识位于初始化数据开头，
不需要追读引擎堆指针。记录在每次完成画面后更新；暂停观察时须核对标识、版本、大小，
以及前后不变的偶数序列。它只辅助核对真实状态，不能代替游戏截图或按键行为。

## 已有 QEMU 记录

以下记录来自此次 ABI4 迁移前的 ABI3 程序，保留其当时的验证范围，不作为新程序通过的证据。

`build/m3-doom-qemu-20261013/run-016/` 验证的是先前 90 帧绘图版本：同一会话两次
各 90 帧、退出 0、完整游戏画面、返回列表 RGB 与运行前一致、原厂 Setup 可导航，以及
文件和全部程序/图形存储释放。其候选为 `visible-menu-candidate`，不是本次持续交互程序。

持续交互版另在 `build/m3-doom-input-20261013/run-001/` 完成12个原厂按键、
移动/转向/射击及松键、暂停、超过600帧运行、游戏菜单Quit和三种MENU＋DELETE
顺序验收。同会话四次运行无残键，退出后资源清空、列表整个RGB与运行前一致，
原厂Setup仍可导航，模拟器已停止。独立核对见同父目录
`run-001-independent-review.json`，原图复核见会话内
`pixel-and-final-snapshot-check.json`。
内容区显示不等于独占全屏；本地与 QEMU 结果均不代表真机、物理卡或全地图验收。
M3 的整体状态以 [M3 记录](../../../../docs/M3.md) 为准。
