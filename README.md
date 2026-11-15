# fourkey_oled · 四键蓝牙键盘 ZMK 固件

> **当前状态（2026-11-15，B24）**：USB HID/BLE、4 键功能、Alt+Tab 宏、SSD1306 混合自定义屏、电量百分比、ZMK Studio 在线改键。
> B24 把内置三 widget 换成**混合自定义状态屏**：顶栏状态 + 2×2 键位卡片 + 底部提示；卡片标签从**运行时 keymap** 读取，Studio 改键后屏幕自动同步。
> 硬件：SuperMini nRF52840（nice!nano v2 兼容克隆板）+ 4 键直连 + 0.96" I2C OLED + 锂电池。
> 仓库：`https://github.com/Feady/fourkey-zmk-config`，固件经 GitHub Actions 编译，无需本地 Zephyr 环境。

---

## 1. 硬件与引脚

| 键位 | 位置 | GPIO | 板丝印 | 接线 |
|---|---|---|---|---|
| K1 | 左上 | P1.06 | D9 | 按下 → GND |
| K2 | 右上 | P1.00 | D6 | 按下 → GND |
| K3 | 左下 | P1.04 | D8 | 按下 → GND |
| K4 | 右下 | P0.11 | D7 | 按下 → GND |

- 4 键为**直连接线**（`zmk,kscan-gpio-direct`），内部上拉、低电平有效；物理位置按 **2×2 矩阵**排布，位置索引 0..3 = K1..K4（先第一排从左到右，再第二排）。
- 虚拟矩阵变换 2 行 × 2 列，`physical_layout` 同步为 2×2，并用 `chosen { zmk,physical-layout = &physical_layout0; }` 声明给 ZMK Studio。
- **OLED**：I2C `0x3C`，SDA = D2/P0.17、SCL = D3/P0.20（nice!nano 的 `&pro_micro_i2c` 默认脚），128×64，SSD1306/SSD1315 家族。
- 早期文档里的 D8/D5/D7/D6 引脚表已作废；P0.24 已从固件完全移除。

## 2. 键位、层与组合键（B24「层即页面」）

只有 4 个物理键，**不加第 5 个键**：用 `zmk,combos` 把 **K1+K2 同时按** 当作逻辑功能键（`&to 1` → Menu 页）。松开后页面保持，所以每个页面 4 个键都可用。

| 层 | 页面 | K1 左上 | K2 右上 | K3 左下 | K4 右下 |
|---|---|---|---|---|---|
| 0 | **Default**（主页面） | Ctrl+C | Ctrl+V | Ctrl+D | Alt+Tab |
| 1 | **Menu**（菜单） | → Num | → Media | → Sys | → Home |
| 2 | **Num** | 1 | 2 | 3 | → Home |
| 3 | **Media** | Vol− | Vol+ | Mute | → Home |
| 4 | **Sys** | BLE 配置 0 | BLE 配置 1 | → Fn | → Home |
| 5 | **Fn** | 解锁 Studio | 清除配对 | BLE 配置 2 | → Home |
| 6,7 | reserved | — | — | — | — |

- **进菜单**：在 Default 页同时按 K1+K2（combo `timeout-ms=30`、`require-prior-idle-ms=80`，避免连续快按误触发）。
- **回主页面**：任何子页面按 K4（`&to 0`）。
- **Alt+Tab**：ZMK 官方宏，`&macro_press &kp LALT` → `&macro_tap &kp TAB` → `&macro_pause_for_release` → `&macro_release &kp LALT`。短按切换一次；长按 Alt 保持按下，任务切换器持续显示，**松手确认切换**。
- 为什么不用 hold-tap 做功能键：官方 `&lt` 默认 `flavor = "tap-preferred"`（见 `app/dts/behaviors/layer_tap.dtsi`），快速「按住 K1 再点其它键」会被判成 tap；且被按住的 K1 在菜单页上无法再按，四键会少一个。combo 没有这两个问题。
- 6、7 层为 `status = "reserved"` 空层，Studio 里可启用并自由改键。
- **进 UF2 刷机走硬件通道**：双击板边 RST 键（固件按键里不再放 `&bootloader`）。

## 3. 显示与连接

### B24 混合自定义状态屏（128×64 单色）

```text
┌────────────────────────────────────┐
│ USB        Default           +87% │  ← 输出通道 | 当前层名 | 电量百分比
├────────────────────────────────────┤
│ ┌──────────┐  ┌──────────┐        │
│ │  C+C     │  │  C+V     │        │  ← 2×2 键位卡片，与实物位置一一对应
│ └──────────┘  └──────────┘        │     按下时整卡反白
│ ┌──────────┐  ┌──────────┐        │
│ │  C+D     │  │ Alt+Tab  │        │
│ └──────────┘  └──────────┘        │
│ K1+K2=MENU                        │  ← 底部提示（子页面为 K4=HOME）
└────────────────────────────────────┘
```

- 实现：`config/boards/shields/fourkey_oled/custom_status_screen.c`；`CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM=y` 覆盖 `app/src/display/main.c` 的 weak `zmk_display_status_screen()`。
- **键位准确性**：卡片文字不是写死的「层号→标签」表，而是每 200 ms 调用官方 `zmk_keymap_get_layer_binding_at_idx(layer_id, position)` 读运行时 `behavior_dev / param1 / param2`，再渲染成短标签（`C+C`、`->Num`、`BT0`、`Alt+Tab` …）。`&trans` 会按 ZMK 派发逻辑向下一层解析；未知 behavior 直接显示设备名或十六进制 usage，宁可朴素也不报错信息。
- **支持在线改键**：ZMK Studio 改键、改层名、重排层后，屏幕 200 ms 内自动同步（层名来自 `zmk_keymap_layer_name()`，标签解析的是同一份运行时 keymap）。刷新跑在 ZMK 显示工作队列里（`k_work` + `zmk_display_work_q()`），LVGL 调用线程安全。
- 顶栏：`zmk_endpoint_get_selected()` → `USB / BLE / ---`；`zmk_battery_state_of_charge()` → 百分比，`+` 前缀表示 USB 供电中；层名取运行时真实层名。
- 按下反白：订阅 `zmk_position_state_changed`，只写标志，样式切换交给显示队列。
- 字体 `lv_font_montserrat_8`（8 px，ASCII 纯文本，不依赖 LVGL 符号字体）；空闲自动熄屏、按键唤醒沿用 ZMK 默认行为。

### 输出与 Studio

- **输出策略**：ZMK 默认 —— 插 USB 走 USB，拔掉后蓝牙兜底。蓝牙设备名 **"FourKey OLED"**。
- **VIA 连不上是正常的**：VIA 是 QMK 专属协议，ZMK 不响应；对应方案是 [ZMK Studio](https://zmk.studio)。

**连接步骤**

1. 确认刷的是 **B23 或之后**的固件（B12~B22 期间 Studio 被临时关闭，网页会报 `Failed to connect to the chosen device`）。
2. USB 数据线接电脑；**关掉串口助手等占用 COM 口的程序**。
3. 打开 https://zmk.studio → Connect → 选 USB 串口设备。B23 起**只有一个 CDC 串口**（Studio RPC），不会选错。
4. 解锁改键权限：Default 页 **K1+K2** → Menu 页按 **K3**（Sys）→ Sys 页按 **K3**（Fn）→ Fn 页按 **K1**（`&studio_unlock`）。改完按 K4 回主页面。
5. 若同时连着 USB 和 BLE，Studio 走 USB 时也要让键盘输出走 USB（`&out OUT_USB`），否则可能连不上。

> ⚠️ Studio 保存过改键后，重刷固件里的 keymap 不会覆盖，需在 Studio 里点 *Restore Stock Settings*。
> ⚠️ combo 是 devicetree 定义，Studio 改不了；在 Studio 里改了 Default 层 K1/K2 也不会影响「K1+K2 打开菜单」。

## 4. 文件结构与关键配置

```text
ZMK_Firmware/
├── build.yaml                    # 构建矩阵；板名 //zmk + studio snippet + cmake-args
├── .github/workflows/build.yml   # 官方 reusable workflow（build-user-config.yml@main）
└── config/
    ├── west.yml
    ├── fourkey_oled.conf         # 功能开关（自定义屏 / 字体 / 内存 / 堆）
    └── boards/shields/fourkey_oled/
        ├── fourkey_oled.keymap   # 6 个页面 + combo + Alt+Tab 宏 + reserved 层
        ├── fourkey_oled.overlay  # kscan / 矩阵变换 / 物理布局 / I2C + OLED 节点
        ├── custom_status_screen.c # B24 混合自定义屏（运行时读 keymap 画 2×2 卡片）
        ├── CMakeLists.txt        # zephyr_library() 接入上面的 C 文件
        ├── Kconfig.defconfig     # 键盘名 + 官方单色屏 LVGL 参数 + 自定义屏默认
        └── Kconfig.shield
```

**几个不能动的要点**（都是踩坑换来的，详见第 6 节）：

| 位置 | 关键值 | 作用 |
|---|---|---|
| `build.yaml` | `nice_nano@2.0.0//zmk` | zmk 变体才带 `CONFIG_ZMK_USB/BLE/SETTINGS_NVS`；裸板名构建 USB 根本没编进去 |
| `build.yaml` | `snippet: studio-rpc-usb-uart` + `cmake-args: -DCONFIG_ZMK_STUDIO=y` | snippet 只提供 RPC 串口与 `ZMK_BEHAVIORS_KEEP_ALL`，**不含** `ZMK_STUDIO`，必须 cmake-args 补上（官方写法） |
| `overlay chosen` | `zmk,physical-layout = &physical_layout0` | Studio 靠它识别键位几何；Studio 要求**不要**用 `chosen zmk,matrix-transform` |
| `Kconfig.defconfig` | `LV_Z_BITS_PER_PIXEL=1`、`LV_Z_VDB_SIZE=64`、`LV_COLOR_DEPTH_1` | 与官方 corne/lily58/nibble/nice_view 一致；缺失会导致满屏雪花/硬死 |
| `conf` | `CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM=y` + `LV_FONT_MONTSERRAT_8=y` + `LV_Z_MEM_POOL_SIZE=8192` | 自定义屏开关、8px 字体、nice_view 官方内存池取值 |
| `CMakeLists.txt` | `zephyr_library()` + `zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)` + `zephyr_library_sources(custom_status_screen.c)` | Zephyr 自动 include shield 目录 CMakeLists；写法照抄官方 nice_view / englmaxi dongle_display |
| `overlay &i2c0` | `nordic,nrf-twim` + `concat-buf-size=<1024>` + `flash-buf-max-size=<1024>` | TWIM 有传输超时不会挂死；1024 拼接缓冲是 SSD1306 整帧写入的硬性要求 |
| `overlay oled` | `solomon,ssd1306fb`、`0x3C`、128×64 | 屏型判定见第 6 节 |
| `conf` | `HEAP_MEM_POOL_SIZE=32768`、独立显示线程 3072、主栈 2048 | LVGL 走系统堆，堆为 0 会硬错误 |

## 5. 编译与刷写

**编译**：push 到 GitHub 后自动触发 Actions → `Build ZMK firmware` → 成功后从 Artifacts 下载 zip，解出 `fourkey_oled-nice_nano@2.0.0__zmk-zmk.uf2`。构建日志里会打印最终 `.config`，B24 建议核对：

```text
CONFIG_ZMK_STUDIO=y
CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM=y
CONFIG_LV_FONT_MONTSERRAT_8=y
CONFIG_LV_Z_MEM_POOL_SIZE=8192
CONFIG_LV_Z_BITS_PER_PIXEL=1
```

若新增的 C 文件没被编进去，现象是屏全黑 + 日志 `No status screen provided`；优先检查 `CMakeLists.txt` 是否被 Zephyr 加载。

**刷写**（Windows，实测最可靠）：

1. 双击板边 RST 键 → 出现 `NICENANO` 盘符；
2. 用 PowerShell 直写，盘符消失 = 写入成功：
   ```powershell
   Copy-Item -LiteralPath ".\fourkey_oled-nice_nano@2.0.0__zmk-zmk.uf2" -Destination "E:\" -Force
   ```
3. 板子自动重启运行新固件。

> 不建议：串口 DFU（`adafruit-nrfutil`，数据流必挂）、资源管理器拖拽（曾静默失败）。
> 刷写不擦除 NVS，蓝牙配对与 Studio 改键会保留。

**抓启动日志（默认关闭）**：B23 起为了不让两个 CDC 串口干扰 Studio，USB 日志默认关闭，overlay 里的 `cdc_acm_uart0` 节点也一并移除。需要抓日志时临时恢复：

1. `config/fourkey_oled.conf` 末尾取消注释 `CONFIG_ZMK_USB_LOGGING=y`；
2. `fourkey_oled.overlay` 末尾加回：
   ```dts
   &zephyr_udc0 {
       cdc_acm_uart0: cdc_acm_uart0 { compatible = "zephyr,cdc-acm-uart"; };
   };
   / { chosen { zephyr,console = &cdc_acm_uart0; zephyr,shell-uart = &cdc_acm_uart0; }; };
   ```
3. 编译刷入后，**先打开串口再单击 RST**（CDC 日志不缓存）；抓完把两处改回来，避免双 COM 口。

## 6. 排障沉淀（根因链）

从 B1 到 B23 的所有坑，按层次归类，避免重复试错：

| 层 | 现象 | 根因 / 修复 |
|---|---|---|
| 构建 | USB 完全不枚举、HID 无输出 | 板名缺 `//zmk` 变体 → `CONFIG_ZMK_USB` 未编入。换官方 reusable workflow + 变体板名（B8） |
| 启动 | 显示一开整机硬死 | 系统堆 0 + 主栈 1KB + LVGL 16-bit 色深对 1-bit 屏。B16 堆/独立线程/主栈，B17 `LV_COLOR_DEPTH_1`，B20 官方单色 Kconfig（BPP=1/VDB=64） |
| I2C | 满屏雪花（屏亮但无内容） | TWIM 默认拼接缓冲 16B，SSD1306 整帧写要 641B → 每次 `display_write` 被拒，GDDRAM 从未写入。B21 设 `concat-buf-size=1024` |
| 屏型 | 试 SH1106 驱动后黑屏 | SH1106 路径用 0xAD DCDC 供电，本屏只认 0x8D 充电泵 → 屏是 SSD1306/SSD1315 家族，非 SH1106（B18/B19） |
| Studio | `Failed to connect to the chosen device` | B12 二分时移除 snippet + `ZMK_STUDIO` 后未恢复；且缺 `zmk,physical-layout` chosen、双 CDC 串口易选错。B23 全部补齐 |
| 蓝牙 | 搜不到设备 | 缺 `Kconfig.defconfig` 里的 `ZMK_KEYBOARD_NAME` → 广播名为空。补 "FourKey OLED" |
| 键位 | 阵列不对 | 固件配 D8/D5/D7/D6，实物是 D9/D6/D8/D7，K1 的 P1.06 根本没被扫描 |

**B24 新增排查点**：屏全黑先看 `CMakeLists.txt`（自定义屏没编进去会报 `No status screen provided`）；卡片标签显示成原始行为名，说明 ZMK 该 behavior 的设备名不在 `render_binding()` 的映射表里 —— 补一条即可，不影响功能。

**SSD1315 说明**：Zephyr 4.5 才新增 `solomon,ssd1315` 绑定（官方参考 M5Stack Unit MiniOLED），但仍由同一个 SSD1306 驱动处理；ZMK main 锁 Zephyr 4.1，用 `solomon,ssd1306fb` 是正确写法。

## 7. 后续待办

- [x] **更好看的界面**（B24 已实现）：混合布局（顶栏状态 + 2×2 键位卡片 + 底部提示），自定义状态屏走官方 nice_view 路径。
- [x] **电量百分比**（B22 内置 widget → B24 自定义屏直接取 `zmk_battery_state_of_charge()`）。
- [ ] **B24 上板实测**：K1+K2 combo 手感/误触发率、2×2 卡片可读性、按下反白、停留子页面时的熄屏唤醒。
- [ ] **Studio 在线改键实测**：改一个键后确认卡片 200 ms 内变化；改层名/重排层后确认顶栏与 `->XXX` 标签跟随。
- [ ] 若 Studio 里新建了 `&to`/`&bt` 之外的 behavior，视需要在 `custom_status_screen.c` 的 `render_binding()` 补短标签映射。
- [ ] 确认电池百分比读数与实际电压的偏差（日志实测 VDDH 4460 mV → 100%），必要时覆盖板级 vbatt 分压定义。
- [ ] 验证空闲熄屏 / 按键唤醒、`&soft_off` 软关机唤醒。

## 8. 相关文档

| 文档 | 内容 |
|---|---|
| `NRF52840_四键蓝牙键盘_底板设计规格_RevD.md` | 硬件设计规格（五章结构，投板版） |
| `Schematic/投板前检查清单_RevE.md` | Rev E 投板前检查 |
| `DisplayValidation/`、`QuickValidation/` | 早期裸机验证固件与接线表（历史产物，正式固件不受其影响） |
| `换屏评估_SSD1306_OLED.md` | 由 ST7735S IPS 屏改 I2C OLED 的评估记录 |
| `.workbuddy/memory/` | 项目长期笔记与逐日排障日志 |
