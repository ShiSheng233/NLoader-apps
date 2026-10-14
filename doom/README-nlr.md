# PR DOOM 示例

本目录 `build.sh` 构建使用通用 `sdk/libc` 的 PR DOOM 示例，读取配置根内的
`GAME.WAD`。该通用文件名由引擎按实际 `MAP01` 或 `E1M1` 内容识别游戏类型，
避免把 Freedoom2 等 Doom 2 资源因 `DOOM.WAD` 名称强制认作 Doom 1。
程序以 `-warp 1 1 -skill 2` 进入第一关；缺少游戏必需资源仍报告错误。
`port/build.sh` 构建的另一套移植也读取 `GAME.WAD`，两者单独验证。

PR 示例要求 ABI 4，使用 2 MiB 引擎区并关闭声音和音乐。程序先取得绘图界面，
提交黑色画面，再请求 4 MiB 工作区，拒绝时降至 3 MiB 和 2.5 MiB；
全部拒绝则显示 `memory unavailable`、归还绘图界面并返回 2，不启动引擎。
引擎初始化复用已持有的界面。取得界面失败会显示原始错误码，布局不支持时
分别报告格式、尺寸或行宽原因；初次提交画面失败不会申请引擎工作区。
这项配置不改变 loader
或原厂分配器的限制，也不构成内存申请一定成功的保证。

构建与主机回归：

```sh
NLR_CLI="$PWD/tool/nloader/target/release/nloader" \
  sh sdk/examples/doom/build.sh build/<新目录>
bash sdk/examples/doom/test_nlr.sh build/<新测试目录>
```

主机测试检查界面与工作区的申请顺序、重复初始化、具体错误提示、失败时归还、
引擎参数、320×200 色彩和输入。
实际卡访问、原厂内存申请及游戏画面另以新 QEMU 会话验证。
