/*
 * fourkey_oled - B31 状态屏（128x64 单色 SSD1306）
 *
 * B31 蓝牙显示简化（用户反馈）：
 *   1) 蓝牙只保留 2 档（Sys K1=BT_SEL0、K2=BT_SEL1），删除 Fn 的 BT_SEL2。
 *   2) 键位标签极简：B0/B1=选档、CLR=清配对、NXT/PRV/DISC…；并修正
 *      "bluetooth" 无 "bt" 子串导致匹配失败、把整个设备名打出来的 bug。
 *   3) 顶栏 BLE 连接时显示当前档位 B0/B1（zmk_ble_active_profile_index）。
 *   4) 选档后触发 ble_active_profile_changed -> 顶栏短暂显示 "B<n> OK" 反馈。
 *
 * 布局（B26 按用户反馈重构）：
 *   y=0..12   顶栏 10px：左=连接状态/操作提示交替闪烁（1.5s），右=电量百分比
 *             （B29：banner 改左对齐——居中时 "K1+K2>MENU" 尾部与右侧
 *               "+100%" 重叠被遮，才出现"K1+K4 显示不全/电量缺字"）
 *   y=13      分隔线
 *   y=15..63  四象限键位区：中间一个十字分隔（不再给每个键画边框，省电）
 *             K1 左上 / K2 右上 / K3 左下 / K4 右下，12px 字体（B28 缩小）
 *             按下只在文字前加 ">" 标记，不整块反白
 *
 * ★ 颜色极性（B25 修复，保留）--------------------------------------------
 * overlay 里 SSD1306 配了 com-invdir，硬件像素极性与 LVGL 相反：
 *     LVGL white(1) -> 像素熄灭(暗)     LVGL black(0) -> 像素点亮(亮)
 * 统一用 PIXEL_ON()/PIXEL_OFF() 宏，杜绝写反。
 * 若将来去掉 overlay 的 com-invdir，把两个宏对调即可。
 *
 * 键位标签来自运行时 keymap（zmk_keymap_get_layer_binding_at_idx），
 * Studio 在线改键/改名/重排层后 200ms 内自动同步，不说谎。
 *
 * ★ B30 顶栏状态修复 ----------------------------------------------------
 * 旧逻辑：selected==NONE 时拿 preferred_transport 兜底，而 ZMK 默认
 *   preferred=USB（endpoints.c: DEFAULT_TRANSPORT=USB）。结果拔掉 USB 且
 *   BLE 未连时屏幕仍显示 "USB"，让人误以为在 USB 模式、蓝牙没坏——其实
 *   那是"首选 USB"，不是"当前连接"。
 * 新逻辑：只显示 current_instance（真正连接的通道），没连就显示 "----"。
 *   这样"拔线后显示 ----"就是 BLE 真没连上，判断有依据。
 * 注意：这只是显示修正，不改 ZMK 端点选择行为（USB 优先 / 拔线回退 BLE
 *   由 ZMK endpoints.c 自己按 transport ready 状态决定）。
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <lvgl.h>

#include <zmk/behavior.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/battery.h>
#include <zmk/endpoints.h>
#include <zmk/endpoints_types.h>
#include <zmk/usb.h>
#include <zmk/ble.h>
#include <zmk/events/ble_active_profile_changed.h>

#define KEY_COUNT 4
#define LABEL_CAP 16
#define REFRESH_MS 200

/* com-invdir：LVGL black -> 硬件点亮，LVGL white -> 硬件熄灭 */
#define PIXEL_ON()  lv_color_black()
#define PIXEL_OFF() lv_color_white()

#define FONT_KEY  &lv_font_montserrat_12
#define FONT_TOP  &lv_font_montserrat_10

/* ---------- LVGL 对象 ---------- */
static lv_obj_t *banner_label;  /* 顶栏左侧：连接状态/提示交替闪烁（左对齐） */
static lv_obj_t *bat_label;
static lv_obj_t *key_labels[KEY_COUNT];
static lv_style_t screen_style;

/* ---------- 缓存：只有文本真的变了才动 LVGL ---------- */
static char cache_banner[16];
static char cache_bat[8];
static char cache_keys[KEY_COUNT][LABEL_CAP];
static volatile bool key_down[KEY_COUNT];

/* B31：选档反馈——收到 ble_active_profile_changed 后，顶栏临时显示 "B<n> OK"
   约 2s 再回到常态；用 uptime 计时，跑在显示工作队列里，无锁。 */
#define BT_HINT_MS 2000U
static volatile uint32_t bt_hint_until;
static volatile uint8_t bt_hint_index;

/* ---------- 小工具 ---------- */

static const char *dev_leaf(const char *dev) {
    if (dev == NULL) {
        return "";
    }
    const char *slash = strrchr(dev, '/');
    return slash ? slash + 1 : dev;
}

static bool ieq(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == *b;
}

static bool is_transparent(const struct zmk_behavior_binding *b) {
    if (b == NULL || b->behavior_dev == NULL) {
        return false;
    }
    const char *leaf = dev_leaf(b->behavior_dev);
    return ieq(leaf, "transparent") || ieq(leaf, "trans");
}

static bool is_layer_tap(const struct zmk_behavior_binding *b) {
    if (b == NULL || b->behavior_dev == NULL) {
        return false;
    }
    const char *leaf = dev_leaf(b->behavior_dev);
    return ieq(leaf, "layer_tap") || ieq(leaf, "lt") || ieq(leaf, "hold_tap");
}

/* HID usage -> 短名称。ZMK_HID_USAGE(page,id) = (page << 16) | id */
static void usage_name(uint16_t page, uint16_t id, char *out, size_t cap) {
    if (page == 0x07) { /* Keyboard/Keypad */
        if (id >= 0x04 && id <= 0x1D) {
            snprintf(out, cap, "%c", (char)('A' + id - 0x04));
            return;
        }
        if (id >= 0x1E && id <= 0x26) {
            snprintf(out, cap, "%c", (char)('1' + id - 0x1E));
            return;
        }
        if (id == 0x27) {
            snprintf(out, cap, "0");
            return;
        }
        switch (id) {
        case 0x28: snprintf(out, cap, "RET"); return;
        case 0x29: snprintf(out, cap, "ESC"); return;
        case 0x2A: snprintf(out, cap, "BSPC"); return;
        case 0x2B: snprintf(out, cap, "TAB"); return;
        case 0x2C: snprintf(out, cap, "SPC"); return;
        case 0x4F: snprintf(out, cap, "RGHT"); return;
        case 0x50: snprintf(out, cap, "LEFT"); return;
        case 0x51: snprintf(out, cap, "DOWN"); return;
        case 0x52: snprintf(out, cap, "UP"); return;
        }
        if (id >= 0x3A && id <= 0x45) { /* F1..F12 */
            snprintf(out, cap, "F%u", (unsigned)(id - 0x39));
            return;
        }
    } else if (page == 0x0C) { /* Consumer */
        switch (id) {
        case 0xE9: snprintf(out, cap, "VOL+"); return;
        case 0xEA: snprintf(out, cap, "VOL-"); return;
        case 0xE2: snprintf(out, cap, "MUTE"); return;
        case 0xCD: snprintf(out, cap, "PLAY"); return;
        case 0xB5: snprintf(out, cap, "NEXT"); return;
        case 0xB6: snprintf(out, cap, "PREV"); return;
        }
    }
    snprintf(out, cap, "%04X", (unsigned)id);
}

static void render_kp(uint32_t param, char *out, size_t cap) {
    uint8_t mods = (param >> 24) & 0xFF;
    uint16_t page = (param >> 16) & 0xFF;
    uint16_t id = param & 0xFFFF;

    char key[8];
    usage_name(page, id, key, sizeof(key));

    char prefix[12] = "";
    if (mods & 0x01) { strcat(prefix, "C+"); }
    if (mods & 0x02) { strcat(prefix, "S+"); }
    if (mods & 0x04) { strcat(prefix, "A+"); }
    if (mods & 0x08) { strcat(prefix, "G+"); }
    if (mods & 0xF0) { strcat(prefix, "R+"); }
    if (mods && prefix[0] == '\0') { strcat(prefix, "M+"); }

    snprintf(out, cap, "%s%s", prefix, key);
}

/* behavior binding 的层参数是 layer ID（behavior_to_layer.c），不是 index */
static const char *layer_name_for(uint8_t layer_id) {
    if (layer_id == ZMK_KEYMAP_LAYER_ID_INVAL) {
        return "?";
    }
    const char *name = zmk_keymap_layer_name(layer_id);
    return (name && name[0]) ? name : "?";
}

static void render_binding(const struct zmk_behavior_binding *b, char *out, size_t cap) {
    if (b == NULL || b->behavior_dev == NULL || b->behavior_dev[0] == '\0') {
        snprintf(out, cap, "---");
        return;
    }
    const char *leaf = dev_leaf(b->behavior_dev);

    if (ieq(leaf, "key_press") || ieq(leaf, "kp")) {
        render_kp(b->param1, out, cap);
        return;
    }
    if (is_layer_tap(b) || ieq(leaf, "mod_tap") || ieq(leaf, "mt")) {
        char tap[8];
        render_kp(b->param2, tap, sizeof(tap));
        snprintf(out, cap, "%.5s>%.6s", tap, layer_name_for((uint8_t)b->param1));
        return;
    }
    if (ieq(leaf, "to_layer") || ieq(leaf, "to")) {
        snprintf(out, cap, "->%.9s", layer_name_for((uint8_t)b->param1));
        return;
    }
    if (ieq(leaf, "momentary_layer") || ieq(leaf, "mo")) {
        snprintf(out, cap, "MO %.7s", layer_name_for((uint8_t)b->param1));
        return;
    }
    if (ieq(leaf, "toggle_layer") || ieq(leaf, "tog")) {
        snprintf(out, cap, "TG %.7s", layer_name_for((uint8_t)b->param1));
        return;
    }
    /* B31：bluetooth 一词里没有连续的 "bt" 子串，旧的 strstr(leaf,"bt")
       匹配不到它，会掉到末尾兜底把整个设备名打出来（"bluetooth..." 太长、
       看不出功能）。这里把 bluetooth / blue / behavior_bt 都显式纳入，
       并统一渲染成极短标签：B0/B1=选档，CLR=清配对，其余同理。 */
    if (ieq(leaf, "bt") || ieq(leaf, "behavior_bt") ||
        strstr(leaf, "bt") != NULL || strstr(leaf, "blue") != NULL) {
        switch (b->param1) {
        case 3:  snprintf(out, cap, "B%u", (unsigned)b->param2); break; /* BT_SEL */
        case 0:  snprintf(out, cap, "CLR"); break;                      /* BT_CLR */
        case 1:  snprintf(out, cap, "NXT"); break;                      /* BT_NXT */
        case 2:  snprintf(out, cap, "PRV"); break;                      /* BT_PRV */
        case 4:  snprintf(out, cap, "CLRA"); break;                     /* CLR_ALL */
        case 5:  snprintf(out, cap, "DISC"); break;                     /* DISCONNECT */
        default: snprintf(out, cap, "B?%u", (unsigned)b->param1); break;
        }
        return;
    }
    if (strstr(leaf, "studio_unlock")) { snprintf(out, cap, "Studio"); return; }
    if (strstr(leaf, "alt_tab"))      { snprintf(out, cap, "AltTab"); return; }
    if (ieq(leaf, "none"))           { snprintf(out, cap, "---"); return; }
    if (strstr(leaf, "bootloader"))   { snprintf(out, cap, "Boot"); return; }
    if (strstr(leaf, "reset"))        { snprintf(out, cap, "Reset"); return; }
    if (ieq(leaf, "outputs") || ieq(leaf, "out")) {
        switch (b->param1) {
        case 1:  snprintf(out, cap, "USB OUT"); break;
        case 2:  snprintf(out, cap, "BLE OUT"); break;
        case 0:  snprintf(out, cap, "TOG OUT"); break;
        case 3:  snprintf(out, cap, "NO OUT"); break;
        default: snprintf(out, cap, "OUT%u", (unsigned)b->param1); break;
        }
        return;
    }
    snprintf(out, cap, "%.13s", leaf);
}

static const struct zmk_behavior_binding *effective_binding(uint16_t position) {
    zmk_keymap_layer_index_t highest = zmk_keymap_highest_layer_active();
    for (int index = (int)highest; index >= 0; index--) {
        zmk_keymap_layer_id_t id = zmk_keymap_layer_index_to_id((uint8_t)index);
        if (id == ZMK_KEYMAP_LAYER_ID_INVAL || !zmk_keymap_layer_active(id)) {
            continue;
        }
        const struct zmk_behavior_binding *b =
            zmk_keymap_get_layer_binding_at_idx(id, position);
        if (b == NULL || is_transparent(b)) {
            continue;
        }
        return b;
    }
    return NULL;
}

static void set_text(lv_obj_t *label, char *cache, size_t cap, const char *text) {
    if (strcmp(cache, text) == 0) {
        return;
    }
    snprintf(cache, cap, "%s", text);
    lv_label_set_text(label, cache);
}

static bool is_to_layer(const struct zmk_behavior_binding *b, uint8_t layer_id) {
    if (b == NULL || b->behavior_dev == NULL) {
        return false;
    }
    const char *leaf = dev_leaf(b->behavior_dev);
    return (ieq(leaf, "to_layer") || ieq(leaf, "to")) &&
           (b->param1 == (uint32_t)layer_id);
}

/* ---------- 刷新：跑在 ZMK 显示工作队列里 ---------- */
static void refresh_ui(void) {
    zmk_keymap_layer_id_t layer_id =
        zmk_keymap_layer_index_to_id(zmk_keymap_highest_layer_active());

    /* 电量百分比：'+' = USB 供电中 */
    char bat[8];
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    snprintf(bat, sizeof(bat), "%s%u%%", zmk_usb_is_powered() ? "+" : "",
             (unsigned)zmk_battery_state_of_charge());
#else
    snprintf(bat, sizeof(bat), "%u%%", (unsigned)zmk_battery_state_of_charge());
#endif
    set_text(bat_label, cache_bat, sizeof(cache_bat), bat);

    /* B28：顶栏中部在「连接状态」和「操作提示」之间每 1.5s 交替。
       原来三者同排，10px 下 "K1+K2>MENU" 显示不全；交替后独占整行。 */
    /* B30：只显示真正连接的通道，不再用 preferred 兜底。
       B31：BLE 时显示当前档位 B0/B1（zmk_ble_active_profile_index），
       一眼看出连的是哪个蓝牙配置；USB 显示 USB，未连显示 ----。 */
    enum zmk_transport transport = zmk_endpoint_get_selected().transport;
    char out[8];
    switch (transport) {
#if IS_ENABLED(CONFIG_ZMK_BLE)
    case ZMK_TRANSPORT_BLE:
        snprintf(out, sizeof(out), "B%d", zmk_ble_active_profile_index());
        break;
#endif
    case ZMK_TRANSPORT_USB: snprintf(out, sizeof(out), "USB"); break;
    default:                snprintf(out, sizeof(out), "----"); break;
    }

    uint32_t now = k_uptime_get_32();

    char banner[16];
    /* 选档后 2s 内优先显示反馈（如 "B0 OK"），再回到常态交替 */
    if ((int32_t)(bt_hint_until - now) > 0) {
        snprintf(banner, sizeof(banner), "B%u OK", (unsigned)bt_hint_index);
    } else {
        static uint32_t banner_last_toggle;
        static bool banner_show_hint;
        if (now - banner_last_toggle >= 1500U) {
            banner_show_hint = !banner_show_hint;
            banner_last_toggle = now;
        }
        if (banner_show_hint) {
            if (layer_id == zmk_keymap_layer_default()) {
                snprintf(banner, sizeof(banner), "K1+K2>MENU");
            } else if (is_to_layer(effective_binding(3), zmk_keymap_layer_default())) {
                snprintf(banner, sizeof(banner), "K4>HOME");
            } else {
                snprintf(banner, sizeof(banner), "K1+K2>MENU");
            }
        } else {
            snprintf(banner, sizeof(banner), "%s", out);
        }
    }
    set_text(banner_label, cache_banner, sizeof(cache_banner), banner);

    /* 四个键位：按下只在文字前加 '>'，不整块反白 */
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        char text[LABEL_CAP];
        render_binding(effective_binding(i), text, sizeof(text));
        char shown[LABEL_CAP + 2];
        if (key_down[i]) {
            snprintf(shown, sizeof(shown), ">%s", text);
        } else {
            snprintf(shown, sizeof(shown), "%s", text);
        }
        set_text(key_labels[i], cache_keys[i], sizeof(cache_keys[i]), shown);
    }
}

static void refresh_work_cb(struct k_work *work) {
    ARG_UNUSED(work);
    refresh_ui();
}
K_WORK_DEFINE(refresh_work, refresh_work_cb);

static void refresh_timer_cb(struct k_timer *timer) {
    ARG_UNUSED(timer);
    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &refresh_work);
    }
}
K_TIMER_DEFINE(refresh_timer, refresh_timer_cb, NULL);

static int position_listener_cb(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev != NULL && ev->position < KEY_COUNT) {
        key_down[ev->position] = ev->state;
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(fourkey_screen, position_listener_cb);
ZMK_SUBSCRIPTION(fourkey_screen, zmk_position_state_changed);

/* B31：蓝牙选档事件 -> 顶栏短暂显示 "B<n> OK" 反馈 */
#if IS_ENABLED(CONFIG_ZMK_BLE)
static int ble_profile_listener_cb(const zmk_event_t *eh) {
    const struct zmk_ble_active_profile_changed *ev =
        as_zmk_ble_active_profile_changed(eh);
    if (ev != NULL) {
        bt_hint_index = ev->index;
        bt_hint_until = k_uptime_get_32() + BT_HINT_MS;
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(fourkey_ble_hint, ble_profile_listener_cb);
ZMK_SUBSCRIPTION(fourkey_ble_hint, zmk_ble_active_profile_changed);
#endif

/* ---------- 建屏 ---------- */
static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                            const lv_font_t *font, lv_color_t color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_SCROLLABLE);
    lv_label_set_text(label, text);
    return label;
}

/* 1px 亮线：画十字分隔用 */
static lv_obj_t *make_line(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                           lv_coord_t w, lv_coord_t h) {
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_set_size(line, w, h);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_style_bg_color(line, PIXEL_ON(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(line, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(line, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(line, 0, LV_PART_MAIN);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    return line;
}

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    lv_style_init(&screen_style);
    lv_style_set_bg_color(&screen_style, PIXEL_OFF()); /* 熄灭 = 黑底 */
    lv_style_set_bg_opa(&screen_style, LV_OPA_COVER);
    lv_style_set_text_color(&screen_style, PIXEL_ON()); /* 点亮 = 白字 */
    lv_style_set_text_font(&screen_style, FONT_KEY);
    lv_obj_add_style(screen, &screen_style, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /* 顶栏 10px：左侧交替显示连接状态/提示（左对齐，避免与电量重叠），
       右侧电量常显。10px 下 "+100%" 约 6 字符 ≈ 36px，从右边缘 -1 起，
       左侧 banner 最长 "K1+K2>MENU" 约 10 字符 ≈ 60px，二者不重叠。 */
    banner_label = make_label(screen, "----", FONT_TOP, PIXEL_ON());
    lv_obj_align(banner_label, LV_ALIGN_TOP_LEFT, 2, 0);

    bat_label = make_label(screen, "--%", FONT_TOP, PIXEL_ON());
    lv_obj_align(bat_label, LV_ALIGN_TOP_RIGHT, -1, 0);

    /* 顶栏分隔线 */
    make_line(screen, 2, 13, 124, 1);

    /* 中央十字：一竖一横，把四象限分开（取代四个格子边框，省电）*/
    make_line(screen, 63, 15, 1, 48); /* 竖线 */
    make_line(screen, 2, 39, 124, 1); /* 横线 */

    /* 四象限键位标签，12px */
    static const int16_t xs[2] = {3, 66};
    static const int16_t ys[2] = {17, 42};
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        lv_obj_t *label = make_label(screen, "---", FONT_KEY, PIXEL_ON());
        key_labels[i] = label;
        lv_obj_set_size(label, 58, 20);
        lv_obj_set_pos(label, xs[i % 2], ys[i / 2]);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }

    refresh_ui();
    k_timer_start(&refresh_timer, K_MSEC(REFRESH_MS), K_MSEC(REFRESH_MS));
    return screen;
}
