# fourkey_oled · 四键蓝牙键盘 ZMK 固件

> **当前状态（2026-11-15，B23）**：USB HID、蓝牙、4 键功能、Alt+Tab 宏、SSD1306 状态屏、电量百分比全部实测通过；ZMK Studio 已重新启用。
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

## 2. 键位与行为

| 键 | 短按 | 长按 |
|---|---|---|
| K1 左上 | Ctrl+C | 按住进入 Fn 层 |
| K2 右上 | Ctrl+V | — |
| K3 左下 | Ctrl+D | — |
| K4 右下 | Alt+Tab（切换一次窗口） | Alt 保持按住，任务切换器持续显示，**松手确认切换** |

**Fn 层**（按住 K1 再按）：K1 蓝牙配对设备 0 / K2 设备 1 / K3 清除配对 / **K4 解锁 ZMK Studio**。

- Alt+Tab 用 ZMK 官方宏实现，无需自写 C 模块：`&macro_press &kp LALT` → `&macro_tap &kp TAB` → `&macro_pause_for_release` → `&macro_release &kp LALT`。关键是 `pause_for_release` 让释放动作等到按键松开才执行。
- keymap 里另有 2 个 `status = "reserved"` 空层，普通构建忽略，Studio 里可启用并自由改键。
- 改键：编辑 `config/boards/shields/fourkey_oled/fourkey_oled.keymap`。
- **进 UF2 刷机走硬件通道**：双击板边 RST 键（Fn 层 K4 已让给 Studio 解锁）。

## 3. 显示与连接

- 状态屏为 ZMK 内置三 widget：左上输出状态（USB/BLE）、右上电池电量、左下当前图层；空闲自动熄屏，按键唤醒。
- **电量显示百分比**：`CONFIG_ZMK_WIDGET_BATTERY_STATUS_SHOW_PERCENTAGE=y`，显示 `100%` 文本，插 USB 时前面带充电符号。此前默认只画电池图标。
- **输出策略**：ZMK 默认 —— 插 USB 走 USB，拔掉后蓝牙兜底。蓝牙设备名 **"FourKey OLED"**，实测 VDDH 采样/电量上报正常。
- **VIA 连不上是正常的**：VIA 是 QMK 专属协议，ZMK 不响应；ZMK 对应方案是 [ZMK Studio](https://zmk.studio)（Chrome/Edge 网页版或 Windows 原生客户端）。

### ZMK Studio 连接步骤

1. 确认刷的是 **B23 或之后**的固件（B12~B22 期间 Studio 被临时关闭，网页会报 `Failed to connect to the chosen device`）。
2. USB 线接电脑（要数据线，不能只供电）；**关掉串口助手等占用 COM 口的程序**。
3. 打开 https://zmk.studio → Connect → 选 USB 串口设备。B23 起**只有一个 CDC 串口**（Studio RPC），不会选错。
4. 按住 K1 进 Fn 层，再按 K4（`&studio_unlock`）解锁改键权限。
5. 若同时连着 USB 和 BLE，Studio 走 USB 时也要让键盘输出走 USB（`&out OUT_USB`），否则可能连不上。

> ⚠️ Studio 保存过改键后，重刷固件里的 keymap 不会覆盖，需在 Studio 里点 *Restore Stock Settings*。

## 4. 文件结构与关键配置

```text
ZMK_Firmware/
├── build.yaml                    # 构建矩阵；板名 //zmk + studio snippet + cmake-args
├── .github/workflows/build.yml   # 官方 reusable workflow（build-user-config.yml@main）
└── config/
    ├── west.yml
    ├── fourkey_oled.conf         # 功能开关（显示/百分比/内存）
    └── boards/shields/fourkey_oled/
        ├── fourkey_oled.keymap   # 键位 + Alt+Tab 宏 + reserved 层
        ├── fourkey_oled.overlay  # kscan / 矩阵变换 / 物理布局 / I2C + OLED 节点
        ├── Kconfig.defconfig     # 键盘名 + 官方单色屏 LVGL 参数
        ├── Kconfig.shield
        └── CMakeLists.txt
```

**几个不能动的要点**（都是踩坑换来的，详见第 6 节）：

| 位置 | 关键值 | 作用 |
|---|---|---|
| `build.yaml` | `nice_nano@2.0.0//zmk` | zmk 变体才带 `CONFIG_ZMK_USB/BLE/SETTINGS_NVS`；裸板名构建 USB 根本没编进去 |
| `build.yaml` | `snippet: studio-rpc-usb-uart` + `cmake-args: -DCONFIG_ZMK_STUDIO=y` | snippet 只提供 RPC 串口与 `ZMK_BEHAVIORS_KEEP_ALL`，**不含** `ZMK_STUDIO`，必须 cmake-args 补上（官方写法） |
| `overlay chosen` | `zmk,physical-layout = &physical_layout0` | Studio 靠它识别键位几何；Studio 要求**不要**用 `chosen zmk,matrix-transform` |
| `Kconfig.defconfig` | `LV_Z_BITS_PER_PIXEL=1`、`LV_Z_VDB_SIZE=64`、`LV_COLOR_DEPTH_1`、`LV_Z_MEM_POOL_SIZE=5120` | 与官方 corne/lily58/nibble/nice_view 一致；缺失会导致满屏雪花/硬死 |
| `overlay &i2c0` | `nordic,nrf-twim` + `concat-buf-size=<1024>` + `flash-buf-max-size=<1024>` | TWIM 有传输超时不会挂死；1024 拼接缓冲是 SSD1306 整帧写入的硬性要求 |
| `overlay oled` | `solomon,ssd1306fb`、`0x3C`、128×64 | 屏型判定见第 6 节 |
| `conf` | `HEAP_MEM_POOL_SIZE=32768`、独立显示线程 3072、主栈 2048 | LVGL 走系统堆，堆为 0 会硬错误 |

## 5. 编译与刷写

**编译**：push 到 GitHub 后自动触发 Actions → `Build ZMK firmware` → 成功后从 Artifacts 下载 zip，解出 `fourkey_oled-nice_nano@2.0.0__zmk-zmk.uf2`。构建日志里会打印最终 `.config`，改完配置建议 grep 核对是否生效（如 `CONFIG_ZMK_STUDIO=y`、`CONFIG_ZMK_WIDGET_BATTERY_STATUS_SHOW_PERCENTAGE=y`）。

**刷写**（Windows，实测最可靠）：

1. 双击板边 RST 键 → 出现 `NICENANO` 盘符；
2. 用 PowerShell 直写，盘符消失 = 写入成功：
   ```powershell
   Copy-Item -LiteralPath ".\fourkey_oled-nice_nano@2.0.0__zmk-zmk.uf2" -Destination "E:\" -Force
   ```
3. 板子自动重启运行新固件。

> 不建议：串口 DFU（`adafruit-nrfutil`，数据流必挂）、资源管理器拖拽（曾静默失败）。
> 刷写不擦除 NVS，蓝牙配对等设置会保留。

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

**SSD1315 说明**：Zephyr 4.5 才新增 `solomon,ssd1315` 绑定（官方参考 M5Stack Unit MiniOLED），但仍由同一个 SSD1306 驱动处理；ZMK main 锁 Zephyr 4.1，用 `solomon,ssd1306fb` 是正确写法。

## 7. 后续待办

- [ ] **设计更好看的界面**（用户提出）：走官方自定义状态屏路径 —— `CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM=y` + 自写 LVGL 绘制/图片资源，官方范例是 `app/boards/shields/nice_view`（canvas + image widget）。可保留现有三个 widget 数据源，只重做布局与视觉；注意自定义屏需要更大的 `LV_Z_MEM_POOL_SIZE`（nice_view 用 8192），且 Studio 启用后 RAM 更紧张，需核对构建内存。
- [ ] 确认电池百分比读数与实际电压的偏差（日志实测 VDDH 4460 mV → 100%），必要时覆盖板级 vbatt 分压定义。
- [ ] 验证空闲熄屏 / 按键唤醒、`&soft_off` 软关机唤醒。
- [ ] 实测 ZMK Studio 连接与在线改键（B23 起已具备条件）。

## 8. 相关文档

| 文档 | 内容 |
|---|---|
| `NRF52840_四键蓝牙键盘_底板设计规格_RevD.md` | 硬件设计规格（五章结构，投板版） |
| `Schematic/投板前检查清单_RevE.md` | Rev E 投板前检查 |
| `DisplayValidation/`、`QuickValidation/` | 早期裸机验证固件与接线表（历史产物，正式固件不受其影响） |
| `换屏评估_SSD1306_OLED.md` | 由 ST7735S IPS 屏改 I2C OLED 的评估记录 |
| `.workbuddy/memory/` | 项目长期笔记与逐日排障日志 |
