# rasuart_lvgl

基于 LVGL 的树莓派串口数据可视化程序，运行在当前桌面环境的 SDL 窗口中。

## 构建

在树莓派上执行：

```sh
make
```

如果当前目录没有 `lvgl/`，`Makefile` 会自动拉取官方 LVGL 源码并生成 `rasuart_lvgl`。
首次构建需要树莓派已安装 `git`、`gcc`、`make`、`pkg-config`、`libsdl2-dev`，并且能联网拉取 LVGL。
首次构建时间会比较长，因为需要编译整套 LVGL；后续再次执行 `make` 会走增量编译，明显更快。

## 运行

```sh
./rasuart_lvgl /dev/ttyUSB0 --baudrate 9600
```

程序会在当前树莓派桌面环境中打开一个窗口。界面会显示：

- 最新收到的一帧文本
- 最近若干帧文本日志
- 从每帧中提取到的最后一个数值的趋势曲线
