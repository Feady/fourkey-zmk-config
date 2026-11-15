# fourkey_oled · 正式 ZMK 固件（Rev E：4 键直连 + SSD1306 OLED）

> 配套硬件：用户嘉立创 EDA 第三轮原理图（4 键直连 → GND、OLED I2C、电池开关 SW2、RST 双击 UF2）。
> 引脚与 `QuickValidation/接线表.md` 一致；验证固件见 `QuickValidation/`（裸机，与本固件互不影响）。

## 引脚

| 键位 | 位置索引 | GPIO | 板丝印 | J1 脚 |
|---|---|---|---|---|
| K1 | 0 | P1.04 | D8 | J1-12 |
| K2 | 1 | P0.24 ★ | D5 | J1-9 |
| K3 | 2 | P0.11 | D7 | J1-11 |
| K4 | 3 | P1.00 | D6 | J1-10 |

- 全部输入内部上拉，按下接 GND（`zmk,kscan-gpio-direct`，`wakeup-source`）。
- ★ K2/P0.24：部分 SuperMini 克隆板占用此脚（分压电路），若 K2 恒「按下」，把 overlay 里 `&pro_micro 5` 换成 `&pro_micro 4`（P0.22 / J1-8）并同步改接线。
- OLED：SSD1306 I2C `0x3C`，SDA=D2/P0.17（J1-6）、SCL=D3/P0.20（J1-7）＝ `&pro_micro_i2c` 默认脚。

## 默认键位（占位，回头改）

| 键 | 默认层 | Fn 层（长按 K4 进入） |
|---|---|---|
| K1 | 播放/暂停 | 蓝牙配对设备 0 |
| K2 | 音量 − | 蓝牙配对设备 1 |
| K3 | 音量 + | 清除蓝牙配对 |
| K4 | 短按静音 / **长按切 Fn 层** | 进 UF2 刷机模式 |

改键：编辑 `config/boards/shields/fourkey_oled/fourkey_oled.keymap`，常用键名速查已写在文件头注释里。

## 如何编译（GitHub Actions，无需本地环境）

1. 在 GitHub 新建仓库（如 `zmk-config`），把本目录内容按以下结构上传：
   ```text
   zmk-config/
   ├── build.yaml            ← 本目录的 build.yaml
   └── config/               ← 本目录的 config/ 整个目录
       ├── west.yml
       ├── fourkey_oled.conf
       └── boards/shields/fourkey_oled/
           ├── fourkey_oled.overlay
           ├── fourkey_oled.keymap
           └── Kconfig.shield
   ```
   注意 shield 必须在 `config/boards/shields/` 下（west.yml `self.path: config`）。
2. push 后自动触发 Build；仓库 → **Actions → Build ZMK firmware → Artifacts**，
   下载 `fourkey_oled-nice_nano_v2-zmk.uf2`（Artifacts 是 zip，解压取 uf2）。
3. 每次改键后 push 即自动出新固件。

## 刷机

1. 电池开关置 ON，USB 连电脑；
2. **双击 RST**（SW1）→ 出现 `NICENANO` 盘（SuperMini 克隆盘名可能略不同）；
3. 把 `fourkey_oled-nice_nano_v2-zmk.uf2` 拖进盘符，板子自动重启；
4. 软件入口：Fn 层按 K4（`&bootloader`），无需拆壳。

## OLED 显示

- 128×64 模块默认即用；若你的模块是 **128×32**：overlay 里 `height = <32>`、`multiplex-ratio = <31>`、`com-invdir` 改 `com-sequential`。
- 显示内容为 ZMK 内置状态屏：电池电量、蓝牙连接/层名（`Media` / `Fn`）。
- 模块板载 4.7~10 kΩ 上拉即可，PCB 上的 R_PU1/R_PU2 留 DNP。

## 已知待核验（沿用 Rev D / V11）

- **电量读数**：ZMK 电量走 nice!nano 板级 vbatt 分压定义；SuperMini 克隆板的分压比/使能脚可能不同。板到手后看屏上电量与万用表实测差多少，误差大再改（需要覆盖板级 vbatt node，届时我来改）。
- **唤醒**：`&soft_off` 后按任意键唤醒需 `wakeup-source`（已开）＋直连 kscan 实测；若唤不醒，检查电池开关是否 ON。

## 与 QuickValidation 的关系

- `QuickValidation/`：裸机验证固件（UF2 已编译好），用于桌面先验证显示/按键/电池，与本固件互不干扰；
- 本目录：正式 ZMK 固件（需 GitHub Actions 编译），板到手、三项验证通过后刷入。

## 本地编译（可选）

```bash
west init -l config
west update
cd zmk/app
west build -b nice_nano_v2 -s zmk/app -d build/fourkey_oled -- -DSHIELD=fourkey_oled
```
