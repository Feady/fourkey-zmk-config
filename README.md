# fourkey_oled · 四键蓝牙键盘 ZMK 固件

> **当前状态（2026-11-20，B28）**：USB HID/BLE、4 键功能、Alt+Tab 宏、SSD1306 自定义屏、电量百分比、ZMK Studio 在线改键。
> B28 界面：顶栏 10px，中部在**连接状态 / 操作提示**之间每 1.5s 交替闪烁（解决 `K1+K2>MENU` 显示不全），右侧电量常显；四象限键位 **12px**（B27 为 14px），仍用十字分隔省电，标签从**运行时 keymap** 读取。
> B28 蓝牙：新增官方 `settings_reset` 一次性固件，刷入后清空整个 NVS（bond / 首选输出 / Studio 改键 / 层顺序），再刷回正常固件重新配对。
> B27 蓝牙稳定性：开 `ZMK_BLE_EXPERIMENTAL_CONN`（禁 2M PHY）+ `BT_SMP_ALLOW_UNAUTH_OVERWRITE`（同主机可覆盖旧 bond），新增 **K1+K4 = 清配对** 兜底。
> B26 蓝牙/层修复：combo 超时 30→100ms（原太短进不了菜单）；新增 K3+K4 = `&out OUT_BLE` 强制切蓝牙，解决拔 USB 后首选输出残留 USB 导致按键全无效。
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

## 2. 键位、层与组合键（B26「层即页面」）

只有 4 个物理键，**不加第 5 个键**：用 `zmk,combos` 把 **K1+K2 同时按** 当作逻辑功能键（`&to 1` → Menu 页）。松开后页面保持，所以每个页面 4 个键都可用。**K3+K4 同时按 = 强制切 BLE 输出**（`&out OUT_BLE`）；**K1+K4 同时按 = 清配对**（`BT_SEL 0` + `BT_CLR`，B27 新增）。

| 层 | 页面 | K1 左上 | K2 右上 | K3 左下 | K4 右下 |
|---|---|---|---|---|---|
| 0 | **Default**（主页面） | Ctrl+C | Ctrl+V | Ctrl+D | Alt+Tab |
| 1 | **Menu**（菜单） | → Num | → Media | → Sys | → Home |
| 2 | **Num** | 1 | 2 | 3 | → Home |
| 3 | **Media** | Vol− | Vol+ | Mute | → Home |
| 4 | **Sys** | BLE 配置 0 | BLE 配置 1 | → Fn | → Home |
| 5 | **Fn** | 解锁 Studio | 清除配对 | USB 输出 | → Home |
| 6,7 | reserved | — | — | — | — |

- **进菜单**：在 Default 页同时按 K1+K2（combo `timeout-ms=100`、`require-prior-idle-ms=80`）。B24/B25 曾用 30ms，短于人手同步两键的时间，经常只按出一个键 → 进不了菜单、后续整串操作失效；B26 改 100ms。
- **强制切蓝牙**：Default 页同时按 K3+K4 = `&out OUT_BLE`（见下方"蓝牙连不上"排查）。
- **清配对**：Default 页同时按 K1+K4 = `&bt BT_SEL 0` + `&bt BT_CLR`（B27 新增，解决 bond 残留导致的"连上马上断"，须配合主机侧删除设备记录）。
- **回主页面**：任何子页面按 K4（`&to 0`）。
- **Alt+Tab**：ZMK 官方宏，`&macro_press &kp LALT` → `&macro_tap &kp TAB` → `&macro_pause_for_release` → `&macro_release &kp LALT`。短按切换一次；长按 Alt 保持按下，任务切换器持续显示，**松手确认切换**。
- 为什么不用 hold-tap 做功能键：官方 `&lt` 默认 `flavor = "tap-preferred"`（见 `app/dts/behaviors/layer_tap.dtsi`），快速「按住 K1 再点其它键」会被判成 tap；且被按住的 K1 在菜单页上无法再按，四键会少一个。combo 没有这两个问题。
- 6、7 层为 `status = "reserved"` 空层，Studio 里可启用并自由改键。
- **进 UF2 刷机走硬件通道**：双击板边 RST 键（固件按键里不再放 `&bootloader`）。

## 3. 显示与连接

### B28 自定义状态屏（128×64 单色）

```text
┌────────────────────────────────────┐
│        USB ⇄ K1+K2>MENU      +87% │  ← 顶栏 10px：中部每 1.5s 交替，右侧电量常显
├────────────────────────────────────┤
│      C+C      │      C+V          │
│               │                   │  ← 四象限键位，中央十字分隔
│ ──────────────┼──────────────      │     无格子边框：只点亮文字+十字，省电
│      C+D      │     AltTab        │
│               │                   │  ← 按下只在文字前加 ">"，不整块反白
└────────────────────────────────────┘
```

- **顶栏交替显示**（B28）：中部每 1.5s 在「连接状态 `USB/BLE/---`」和「操作提示 `K1+K2>MENU` / `K4>HOME`」之间切换，右侧电量 `+87%` 常显。B27 三者同排时提示显示不全，交替后独占整行。层名仍不显示——看四象限键位即可判断当前页。
- **颜色极性**（B25 修复，保留）：overlay 的 `com-invdir` 让 LVGL `black` = 硬件点亮，代码统一走 `PIXEL_ON()/PIXEL_OFF()` 宏，避免写反导致"字全灭只剩暗纹"。
- **省电**：默认只有文字 + 1px 十字线点亮；按下不改背景，只加 `>` 前缀。

- 实现：`config/boards/shields/fourkey_oled/custom_status_screen.c`；`CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM=y` 覆盖 `app/src/display/main.c` 的 weak `zmk_display_status_screen()`。
- **键位准确性**：卡片文字不是写死的「层号→标签」表，而是每 200 ms 调用官方 `zmk_keymap_get_layer_binding_at_idx(layer_id, position)` 读运行时 `behavior_dev / param1 / param2`，再渲染成短标签（`C+C`、`->Num`、`BT0`、`Alt+Tab` …）。`&trans` 会按 ZMK 派发逻辑向下一层解析；未知 behavior 直接显示设备名或十六进制 usage，宁可朴素也不报错信息。
- **支持在线改键**：ZMK Studio 改键、改层名、重排层后，屏幕 200 ms 内自动同步（层名来自 `zmk_keymap_layer_name()`，标签解析的是同一份运行时 keymap）。刷新跑在 ZMK 显示工作队列里（`k_work` + `zmk_display_work_q()`），LVGL 调用线程安全。
- 顶栏：`zmk_endpoint_get_selected()` → `USB / BLE / ---`；`zmk_battery_state_of_charge()` → 百分比，`+` 前缀表示 USB 供电中。
- 按下标记：订阅 `zmk_position_state_changed`，只写标志，显示队列里给标签加 `>` 前缀。
- 字体：键位 **12px**、顶栏 **10px**（`lv_font_montserrat_10/12`，ASCII 纯文本；B27 为 14px 键位 + 12px 顶栏，B28 按用户反馈都调小）；空闲自动熄屏、按键唤醒沿用 ZMK 默认行为。

### 输出与 Studio

- **输出策略**：ZMK 默认 —— 插 USB 走 USB，拔掉后蓝牙兜底。蓝牙设备名 **"FourKey OLED"**。
- **VIA 连不上是正常的**：VIA 是 QMK 专属协议，ZMK 不响应；对应方案是 [ZMK Studio](https://zmk.studio)。

**⚠️ 拔掉 USB 后蓝牙连不上 / 按键全无效（B26 重点）**

根因不是蓝牙坏了，而是 ZMK 的**首选输出（preferred transport）被存进了 NVS**：

1. `endpoints.c` 里 `preferred_transport` 默认 = USB，且每次切换都会 `settings_save_one()` 持久化；
2. 长时间插着 USB 使用时，首选一直是 USB。拔线后 ZMK 会尝试回退到 BLE，**但前提是 BLE profile 已连接**（`is_ble_ready()`）；
3. 如果此时 BLE 没连上主机，`get_selected_transport()` 返回 `ZMK_TRANSPORT_NONE` → **endpoint 为空 → 所有按键（包括蓝牙键本身）看起来都没效果**。这是 ZMK 的设计，不是 bug。

解决办法（任选其一）：

- **Default 页同时按 K3+K4** → `&out OUT_BLE`：直接把首选输出切到 BLE，ZMK 立即广播并回连已配对主机。顶栏应从 `USB` 变 `BLE` 或 `---`。这是最快的一键恢复。
- 若 BLE 从未配对过：切到 BLE 后，用 Sys 页 `BT_SEL 0/1` 选一个**空的 profile**，电脑/手机搜索 "FourKey OLED" 配对。
- 若之前配对过但连不上：Fn 页 `&bt BT_CLR` 清除当前 profile 的 bond，同时**在主机侧删除该设备的配对记录**，再重新配对（两侧 bond 必须一致，否则会一直 "Security failed"）。
- **Studio 改键残留**：Studio 保存的 keymap/bond 存在 NVS，刷固件不擦除。若层行为错乱，在 Studio 里点 *Restore Stock Settings* 恢复出厂 keymap。

> 说明：`BT_SEL 0` 只是"选择 profile 0"，它不会把输出从 USB 切到 BLE，也不会主动发起配对。所以插着 USB 时按蓝牙键看不到任何效果属于正常现象——必须先用 `&out OUT_BLE` 或拔线让 BLE 成为当前输出。

**⚠️ 蓝牙连上后几秒就断 / 反复配对失败（B27 重点）**

这条与上面的"首选输出残留"是两回事。ZMK 源码里断连主要来自两条路径：

1. **2M PHY 链路余量不足**：B27 前 `CONFIG_BT_CTLR_PHY_2M` 默认开启。SuperMini 这类克隆板天线/射频余量有限，高速 PHY 容易在连接建立后被主机断开。ZMK 官方提供 `CONFIG_ZMK_BLE_EXPERIMENTAL_CONN=y`，其帮助文字明确写着 "improve connection stability"，实际效果是把 `BT_CTLR_PHY_2M` 改为 `n`，回到更稳的 1M PHY（四键键盘的数据量完全用不满 2M）。
2. **NVS 里的旧 bond/profile 导致配对被拒**：`ble.c` 的 `pairing_allowed_for_current_profile()` 对"已被占用的 profile"默认返回 `Rejecting pairing request to taken profile` 并断开。这一路刷了十几个测试固件，NVS 不擦除，很容易留下旧地址。B27 开 `CONFIG_BT_SMP_ALLOW_UNAUTH_OVERWRITE=y`，允许**同一 MAC 的主机**重新配对时覆盖旧密钥（ZMK 文档给出的 bond mismatch 修复手段）。

若仍出现连上就断：

- **Default 页同时按 K1+K4** → 清掉 profile 0 的 bond；
- **同时在电脑/手机侧删除 "FourKey OLED" 的蓝牙配对记录**（ZMK 官方文档明确要求两侧一致，只清一侧会一直 Security failed）；
- 再重新搜索配对。
- 若层行为/改键也错乱（不只是蓝牙），在 Studio 点 *Restore Stock Settings* 恢复出厂 keymap。

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
| `conf` | `CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM=y` + `LV_FONT_MONTSERRAT_10/14=y` + `LV_Z_MEM_POOL_SIZE=8192` | 自定义屏开关、顶栏/键位字体、nice_view 官方内存池取值 |
| `conf` | `ZMK_BLE_EXPERIMENTAL_CONN=y` + `BT_SMP_ALLOW_UNAUTH_OVERWRITE=y` | 1M PHY 提升克隆板链路稳定性；允许同主机覆盖 NVS 旧 bond |
| `CMakeLists.txt` | `zephyr_library()` + `zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)` + `zephyr_library_sources(custom_status_screen.c)` | Zephyr 自动 include shield 目录 CMakeLists；写法照抄官方 nice_view / englmaxi dongle_display |
| `overlay &i2c0` | `nordic,nrf-twim` + `concat-buf-size=<1024>` + `flash-buf-max-size=<1024>` | TWIM 有传输超时不会挂死；1024 拼接缓冲是 SSD1306 整帧写入的硬性要求 |
| `overlay oled` | `solomon,ssd1306fb`、`0x3C`、128×64 | 屏型判定见第 6 节 |
| `conf` | `HEAP_MEM_POOL_SIZE=32768`、独立显示线程 3072、主栈 2048 | LVGL 走系统堆，堆为 0 会硬错误 |

## 5. 编译与刷写

**编译**：push 到 GitHub 后自动触发 Actions → `Build ZMK firmware` → 成功后从 Artifacts 下载 zip，解出 `fourkey_oled-nice_nano@2.0.0__zmk-zmk.uf2`。构建日志里会打印最终 `.config`，B24 建议核对：

```text
CONFIG_ZMK_STUDIO=y
CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM=y
CONFIG_LV_FONT_MONTSERRAT_10=y
CONFIG_LV_FONT_MONTSERRAT_14=y
CONFIG_ZMK_BLE_EXPERIMENTAL_CONN=y
CONFIG_BT_SMP_ALLOW_UNAUTH_OVERWRITE=y
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

**清空 NVS（蓝牙彻底重置，B28 新增）**

普通固件刷写**不会**清 settings；这一路刷了十几个测试固件，NVS 里会堆积旧 bond、首选输出、Studio 改键和层顺序，表现为蓝牙连不上或连上就断。ZMK 官方给的标准做法是一次性 `settings_reset` 固件：

1. `build.yaml` 已加第二个构建项 `shield: settings_reset`，Actions 会同时产出两个 UF2：`fourkey_oled-nice_nano@2.0.0__zmk-zmk.uf2` 和 `settings_reset-nice_nano@2.0.0__zmk-zmk.uf2`。
2. **先刷 `settings_reset-...uf2`**：它启动时清空整个 settings 分区，并刻意关掉 BLE 和显示，防止清完立刻写回。⚠️ 这个固件**不能长期使用**——每次启动都会清空。
3. 等几秒，**再双击 RST 进 UF2，刷回 `fourkey_oled-...uf2` 正常固件**。
4. 在电脑/手机侧删除 "FourKey OLED" 的蓝牙记录，重新搜索配对（两侧必须一致）。

> 如果只是 Studio 改键残留（蓝牙正常），直接在 Studio 点 *Restore Stock Settings* 即可，不必刷重置固件。

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
| 蓝牙 | 连上几秒就断、反复配对失败 | 2M PHY 在克隆板链路余量不足 + NVS 旧 bond/profile 让 ZMK 拒绝新配对。B27 开 `ZMK_BLE_EXPERIMENTAL_CONN`（禁 2M PHY）+ `BT_SMP_ALLOW_UNAUTH_OVERWRITE`，并加 K1+K4 清配对 combo |
| 键位 | 阵列不对 | 固件配 D8/D5/D7/D6，实物是 D9/D6/D8/D7，K1 的 P1.06 根本没被扫描 |

**B24 新增排查点**：屏全黑先看 `CMakeLists.txt`（自定义屏没编进去会报 `No status screen provided`）；卡片标签显示成原始行为名，说明 ZMK 该 behavior 的设备名不在 `render_binding()` 的映射表里 —— 补一条即可，不影响功能。

**SSD1315 说明**：Zephyr 4.5 才新增 `solomon,ssd1315` 绑定（官方参考 M5Stack Unit MiniOLED），但仍由同一个 SSD1306 驱动处理；ZMK main 锁 Zephyr 4.1，用 `solomon,ssd1306fb` 是正确写法。

## 7. 后续待办

- [x] **更好看的界面**（B24 已实现）：混合布局（顶栏状态 + 2×2 键位卡片 + 底部提示），自定义状态屏走官方 nice_view 路径。
- [x] **电量百分比**（B22 内置 widget → B24 自定义屏直接取 `zmk_battery_state_of_charge()`）。
- [ ] **B27 上板实测**：蓝牙是否不再秒断、10px 顶栏清晰度、K1+K4 清配对后能否重新配对。
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
