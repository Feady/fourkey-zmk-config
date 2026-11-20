# fourkey_oled · 四键蓝牙键盘 ZMK 固件

> **当前状态（2026-11-20，B30）**：USB HID/BLE、4 键功能、Alt+Tab 宏、SSD1306 自定义屏、电量百分比、ZMK Studio 在线改键。
> **B29 蓝牙回退到 B24 已知正常基线**：用户确认 B24 蓝牙正常，B26/B27 为"修蓝牙"加的两处 combo 与两个 BLE 实验开关经证实是回归源，已全部移除（详见第 2、3 节）。仅保留 combo 100ms 与界面改进。
> **B30 顶栏状态修正（仅显示，不改蓝牙行为）**：旧逻辑在"当前无连接"时拿 preferred_transport 兜底，而 ZMK 默认 preferred=USB，导致拔掉 USB、BLE 又没连上时屏幕仍显示 `USB`，误导判断。B30 改为只显示**真正连接的通道**：`USB` / `BLE` / `----`（未连）。现状诊断：B29 与 B24 的 BLE 代码完全等价，"连上就断 + 沿用旧配对记录"是**两侧 bond 不一致**（电脑侧旧密钥），须两端清配对重配，非固件问题（详见第 3 节）。
> B29 界面：顶栏 10px，**左侧**在「连接状态 / 操作提示」间每 1.5s 交替（**左对齐**，解决提示被右侧电量遮住导致的 `K1+K4` 残缺、`+100%` 显示不全），右侧电量常显；四象限键位 **12px**，十字分隔省电，标签从**运行时 keymap** 读取。
> B28 保留：官方 `settings_reset` 一次性固件（清空 NVS，需要彻底重置蓝牙时用）；日常不再依赖它。
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

## 2. 键位、层与组合键（B29「层即页面」，蓝牙回退 B24）

只有 4 个物理键，**不加第 5 个键**：用 `zmk,combos` 把 **K1+K2 同时按** 当作逻辑功能键（`&to 1` → Menu 页）。松开后页面保持，所以每个页面 4 个键都可用。**当前只有一个 combo（K1+K2）**——B26/B27 曾加的 K3+K4（切 BLE）和 K1+K4（清配对）已在 B29 移除。

> **为什么移除那两个 combo（B29 关键修复）**：你的日常主层是 K1=Ctrl+C、K4=Alt+Tab。切换窗口时习惯「Ctrl+C 后紧接 Alt+Tab」，两个键在 combo 的 100 ms 窗口内会被判成 **K1+K4 = 清配对**，bond 被反复清掉，表现就是"蓝牙连上就断 / 连不上"。同理 K3+K4（Ctrl+D→Alt+Tab）会误触发 `OUT_BLE` 把正在用的 USB 输出切走 → 键盘无响应。B24 只有 K1+K2 一个 combo，从无此问题，故回退。

| 层 | 页面 | K1 左上 | K2 右上 | K3 左下 | K4 右下 |
|---|---|---|---|---|---|
| 0 | **Default**（主页面） | Ctrl+C | Ctrl+V | Ctrl+D | Alt+Tab |
| 1 | **Menu**（菜单） | → Num | → Media | → Sys | → Home |
| 2 | **Num** | 1 | 2 | 3 | → Home |
| 3 | **Media** | Vol− | Vol+ | Mute | → Home |
| 4 | **Sys** | BLE 配置 0 | BLE 配置 1 | → Fn | → Home |
| 5 | **Fn** | 解锁 Studio | 清除配对 | BLE 配置 2 | → Home |
| 6,7 | reserved | — | — | — | — |

- **进菜单**：在 Default 页同时按 K1+K2（combo `timeout-ms=100`、`require-prior-idle-ms=80`）。B24/B25 曾用 30ms，短于人手同步两键的时间，经常只按出一个键 → 进不了菜单、后续整串操作失效；B26 起改 100ms 并保留。
- **回主页面**：任何子页面按 K4（`&to 0`）。
- **蓝牙操作**：走 Sys 页 `BT_SEL 0/1`（选 profile）、Fn 页 `BT_CLR`（清当前配对）、Fn 页 `BT_SEL 2`（第 3 个 profile）——都是 B24 原样，没有组合键，不会误触发。
- **Alt+Tab**：ZMK 官方宏，`&macro_press &kp LALT` → `&macro_tap &kp TAB` → `&macro_pause_for_release` → `&macro_release &kp LALT`。短按切换一次；长按 Alt 保持按下，任务切换器持续显示，**松手确认切换**。
- 为什么不用 hold-tap 做功能键：官方 `&lt` 默认 `flavor = "tap-preferred"`（见 `app/dts/behaviors/layer_tap.dtsi`），快速「按住 K1 再点其它键」会被判成 tap；且被按住的 K1 在菜单页上无法再按，四键会少一个。combo 没有这两个问题。
- 6、7 层为 `status = "reserved"` 空层，Studio 里可启用并自由改键。
- **进 UF2 刷机走硬件通道**：双击板边 RST 键（固件按键里不再放 `&bootloader`）。

## 3. 显示与连接

### B29 自定义状态屏（128×64 单色）

```text
┌────────────────────────────────────┐
│ K1+K2>MENU ⇄ USB            +100% │  ← 顶栏 10px：左侧每 1.5s 交替（左对齐），右侧电量常显
├────────────────────────────────────┤
│      C+C      │      C+V          │
│               │                   │  ← 四象限键位，中央十字分隔
│ ──────────────┼──────────────      │     无格子边框：只点亮文字+十字，省电
│      C+D      │     AltTab        │
│               │                   │  ← 按下只在文字前加 ">"，不整块反白
└────────────────────────────────────┘
```

- **顶栏交替显示**（B28 引入，**B29 改左对齐**）：每 1.5s 在「连接状态 `USB/BLE/---`」和「操作提示 `K1+K2>MENU` / `K4>HOME`」之间切换，右侧电量 `+100%` 常显。**B29 根因**：B28 时 banner 用 `LV_ALIGN_TOP_MID` 居中，`K1+K2>MENU` 右端延伸到约 x=94，与右上角电量 `+100%` 重叠，后画的电量 label 遮住 banner 尾部 → 看到残缺的 `K1+K4`、电量也显示不全。改为 `LV_ALIGN_TOP_LEFT`（x=2 起）后两者彻底分开。层名仍不显示——看四象限键位即可判断当前页。
- **颜色极性**（B25 修复，保留）：overlay 的 `com-invdir` 让 LVGL `black` = 硬件点亮，代码统一走 `PIXEL_ON()/PIXEL_OFF()` 宏，避免写反导致"字全灭只剩暗纹"。
- **省电**：默认只有文字 + 1px 十字线点亮；按下不改背景，只加 `>` 前缀。

- 实现：`config/boards/shields/fourkey_oled/custom_status_screen.c`；`CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM=y` 覆盖 `app/src/display/main.c` 的 weak `zmk_display_status_screen()`。
- **键位准确性**：卡片文字不是写死的「层号→标签」表，而是每 200 ms 调用官方 `zmk_keymap_get_layer_binding_at_idx(layer_id, position)` 读运行时 `behavior_dev / param1 / param2`，再渲染成短标签（`C+C`、`->Num`、`BT0`、`Alt+Tab` …）。`&trans` 会按 ZMK 派发逻辑向下一层解析；未知 behavior 直接显示设备名或十六进制 usage，宁可朴素也不报错信息。
- **支持在线改键**：ZMK Studio 改键、改层名、重排层后，屏幕 200 ms 内自动同步（层名来自 `zmk_keymap_layer_name()`，标签解析的是同一份运行时 keymap）。刷新跑在 ZMK 显示工作队列里（`k_work` + `zmk_display_work_q()`），LVGL 调用线程安全。
- 顶栏：**B30 起**只读 `zmk_endpoint_get_selected().transport` → `USB / BLE / ----`（真正连接的通道；未连显示 `----`）。B29 及以前在 `NONE` 时用 `zmk_endpoint_get_preferred_transport()` 兜底，而 ZMK 默认 preferred=USB，导致"拔掉 USB、BLE 又没连"时误显示 `USB`——那是首选不是实连，已修正。`zmk_battery_state_of_charge()` → 百分比，`+` 前缀表示 USB 供电中。
- 按下标记：订阅 `zmk_position_state_changed`，只写标志，显示队列里给标签加 `>` 前缀。
- 字体：键位 **12px**、顶栏 **10px**（`lv_font_montserrat_10/12`，ASCII 纯文本；B27 为 14px 键位 + 12px 顶栏，B28 按用户反馈都调小）；空闲自动熄屏、按键唤醒沿用 ZMK 默认行为。

### 输出与 Studio

- **输出策略**：ZMK 默认 —— 插 USB 走 USB，拔掉后蓝牙兜底。蓝牙设备名 **"FourKey OLED"**。
- **VIA 连不上是正常的**：VIA 是 QMK 专属协议，ZMK 不响应；对应方案是 [ZMK Studio](https://zmk.studio)。

**⚠️ 蓝牙连不上 / 连上就断 —— B29 已回退到 B24 基线**

**结论先行**：用户确认 B24 蓝牙正常。B26/B27 为"修蓝牙"做的一系列改动经证实是**回归源**，B29 全部撤销：

| 已移除（B26/B27） | 为什么有害 |
|---|---|
| combo **K3+K4 = `&out OUT_BLE`** | 日常 Ctrl+D→Alt+Tab 会误触发，把正在用的 USB 输出切走 → 键盘无响应 |
| combo **K1+K4 = `BT_SEL 0`+`BT_CLR`** | 日常 Ctrl+C→Alt+Tab 切窗口会误触发清配对，bond 被反复清掉 → 连上就断 |
| `CONFIG_ZMK_BLE_EXPERIMENTAL_CONN=y`（禁 2M PHY） | B24 不开也正常，属无效变量 |
| `CONFIG_BT_SMP_ALLOW_UNAUTH_OVERWRITE=y` | 同上，回退以保持与 B24 完全一致 |

现在只有 **K1+K2** 一个 combo，蓝牙操作全走层里的按键（Sys 页 `BT_SEL 0/1`、Fn 页 `BT_CLR` / `BT_SEL 2`），不会误触发。

**B30 现状诊断（用户反馈：连上就断 + 沿用旧配对记录）**：逐行 diff 确认 B29 与 B24 的 BLE 代码完全等价——**固件不是病因，别再改 BLE 配置/回退版本**。真因是**两侧 bond 不一致**：B26/B27 时期键盘端的 bond 被误清过，但电脑侧仍保留旧的 "FourKey OLED" 配对记录；电脑拿旧密钥连接、键盘已不认，握手后立即断开。解法只有一条：**键盘端和电脑端都清，再重新配对**（见下方步骤 4/5）。B30 同时修正顶栏显示兜底，让"拔线后是否真连上 BLE"看得准，避免再被假的 `USB` 误导。

**ZMK 的输出机制（仍需理解）**：

1. `endpoints.c` 里 `preferred_transport` 默认 = USB，且会 `settings_save_one()` 持久化；长时间插 USB 后首选是 USB。
2. 拔线后 ZMK 回退 BLE 的前提是 **BLE profile 已连接**（`is_ble_ready()`）；若没连上，`get_selected_transport()` 返回 `ZMK_TRANSPORT_NONE` → **endpoint 为空 → 所有按键看起来都没效果**。这是 ZMK 的设计，不是 bug。
3. `BT_SEL 0` 只是"选 profile 0"，**不切输出也不主动配对**；插着 USB 时按蓝牙键看不到效果属正常。

**B29 下的配对步骤**：

1. 拔掉 USB（或让 BLE 成为当前输出）：先断开 USB 数据线，键盘会尝试回连 BLE。
2. 电脑/手机搜索 "FourKey OLED" → 配对连接。
3. 若从未配对过：Sys 页选一个**空 profile**（`BT_SEL 0/1`）再搜。
4. 若之前配对过但连不上：**两侧都要清** —— Fn 页 `&bt BT_CLR` 清键盘 bond，同时**在电脑/手机侧删除 "FourKey OLED" 的配对记录**，再重新配对（两侧 bond 必须一致，否则一直 "Security failed"）。
5. 若怀疑 NVS 深层残留（keymap/层顺序也错乱）：刷一次官方 `settings_reset` 固件清 NVS，再刷回正常固件；或在 Studio 点 *Restore Stock Settings*。

**连接步骤**

1. 确认刷的是 **B23 或之后**的固件（B12~B22 期间 Studio 被临时关闭，网页会报 `Failed to connect to the chosen device`）。
2. USB 数据线接电脑；**关掉串口助手等占用 COM 口的程序**。
3. 打开 https://zmk.studio → Connect → 选 USB 串口设备。B23 起**只有一个 CDC 串口**（Studio RPC），不会选错。
4. 解锁改键权限：Default 页 **K1+K2** → Menu 页按 **K3**（Sys）→ Sys 页按 **K3**（Fn）→ Fn 页按 **K1**（`&studio_unlock`）。改完按 K4 回主页面。
5. Studio RPC 走独立 CDC 串口，与当前输出（USB/BLE）无关；只要 USB 线接着就能连。B29 已移除 `&out OUT_USB` / `&out OUT_BLE` 组合键，不要再用组合键切输出。

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
| 蓝牙 | 连上几秒就断、反复配对失败 | **B29 根因**：B27 的 K1+K4 清配对 combo 被日常 Ctrl+C→Alt+Tab 误触发，bond 反复被清。B29 移除该 combo 及两个 BLE 实验开关，回退到 B24 基线（用户确认正常）。B26/B27 的 2M PHY / bond 覆盖分析属误判 |
| 显示 | 顶栏 `K1+K4` 残缺、`+100%` 显示不全 | B28 banner 用 `TOP_MID` 居中与右侧电量重叠；B29 改 `TOP_LEFT` 左对齐（x=2 起）|
| 键位 | 阵列不对 | 固件配 D8/D5/D7/D6，实物是 D9/D6/D8/D7，K1 的 P1.06 根本没被扫描 |

**B24 新增排查点**：屏全黑先看 `CMakeLists.txt`（自定义屏没编进去会报 `No status screen provided`）；卡片标签显示成原始行为名，说明 ZMK 该 behavior 的设备名不在 `render_binding()` 的映射表里 —— 补一条即可，不影响功能。

**SSD1315 说明**：Zephyr 4.5 才新增 `solomon,ssd1315` 绑定（官方参考 M5Stack Unit MiniOLED），但仍由同一个 SSD1306 驱动处理；ZMK main 锁 Zephyr 4.1，用 `solomon,ssd1306fb` 是正确写法。

## 7. 后续待办

- [x] **更好看的界面**（B24 已实现）：混合布局（顶栏状态 + 2×2 键位卡片 + 底部提示），自定义状态屏走官方 nice_view 路径。
- [x] **电量百分比**（B22 内置 widget → B24 自定义屏直接取 `zmk_battery_state_of_charge()`）。
- [ ] **B29 上板实测（当前）**：蓝牙是否恢复 B24 的正常连接（配对后不再秒断）；顶栏左对齐后 `K1+K2>MENU` 与 `+100%` 是否都显示完整、无残缺。
- [ ] combo 误触发复查：日常 Ctrl+C→Alt+Tab 是否还会误入菜单（K1+K2 只剩这一个 combo，理论上不会清配对了）。
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
