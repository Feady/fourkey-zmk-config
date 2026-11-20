/*
 * fourkey_oled - B25 状态屏（128x64 单色 SSD1306）
 *
 * 布局：
 *   y=1..11   顶栏：USB/BLE | 当前层名 | +87%（+ 表示 USB 供电中）
 *   y=12      分隔线
 *   y=15..55  2x2 键位卡片（60x20），位置与实物一一对应
 *   y=57..64  底部提示（K1+K2=MENU / K4=HOME）
 *
 * ★ 颜色极性（B25 关键修复）--------------------------------------------
 * overlay 里 SSD1306 配了 com-invdir，硬件像素极性与 LVGL 相反：
 *     LVGL white(1) -> 像素熄灭(暗)     LVGL black(0) -> 像素点亮(亮)
 * B24 直接写 white 字 / black 底，结果文字全灭、背景全亮：
 * 顶栏/电量看不到、卡片只剩暗横纹，按下反色后黑字点亮才显出来。
 * 本文件统一用 PIXEL_ON()/PIXEL_OFF() 宏，杜绝写反。
 * 若将来去掉 overlay 的 com-invdir，把两个宏对调即可。
 *
 * 省电：默认只有文字 + 1px 边框点亮；按下只给标签加 ">" 前缀，
 *       不再整块 60x20 反白（OLED 点亮像素才耗电）。
 *
 * 键位标签来自运行时 keymap（zmk_keymap_get_layer_binding_at_idx），
 * Studio 在线改键/改名后 200ms 内自动同步，不说谎。
 *
 * 参考：ZMK 官方 app/boards/shields/nice_view/、
 *       englmaxi/zmk-dongle-display（大字体 + 显式样式范例）
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

#define KEY_COUNT 4
#define LABEL_CAP 16
#define REFRESH_MS 200

/* com-invdir：LVGL black -> 硬件点亮，LVGL white -> 硬件熄灭 */
#define PIXEL_ON()  lv_color_black()
#define PIXEL_OFF() lv_color_white()

#define FONT_KEY  &lv_font_montserrat_14
#define FONT_TOP  &lv_font_montserrat_8

/* ---------- LVGL 对象 ---------- */
static lv_obj_t *out_label;
static lv_obj_t *layer_label;
static lv_obj_t *bat_label;
static lv_obj_t *hint_label;
static lv_obj_t *cards[KEY_COUNT];
static lv_obj_t *key_labels[KEY_COUNT];
static lv_style_t screen_style;

/* ---------- 缓存：只有文本真的变了才动 LVGL ---------- */
static char cache_layer[12];
static char cache_out[8];
static char cache_bat[8];
static char cache_hint[24];
static char cache_keys[KEY_COUNT][LABEL_CAP];
static volatile bool key_down[KEY_COUNT];

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
    if (ieq(leaf, "bt") || ieq(leaf, "behavior_bt") || strstr(leaf, "bt") != NULL) {
        switch (b->param1) {
        case 3:  snprintf(out, cap, "BT%u", (unsigned)b->param2); break;
        case 0:  snprintf(out, cap, "BT CLR"); break;
        case 1:  snprintf(out, cap, "BT NXT"); break;
        case 2:  snprintf(out, cap, "BT PRV"); break;
        case 4:  snprintf(out, cap, "BT CLRA"); break;
        case 5:  snprintf(out, cap, "BT DISC"); break;
        default: snprintf(out, cap, "BT %u", (unsigned)b->param1); break;
        }
        return;
    }
    if (strstr(leaf, "studio_unlock")) { snprintf(out, cap, "Studio"); return; }
    if (strstr(leaf, "alt_tab"))      { snprintf(out, cap, "AltTab"); return; }
    if (ieq(leaf, "none"))           { snprintf(out, cap, "---"); return; }
    if (strstr(leaf, "bootloader"))   { snprintf(out, cap, "Boot"); return; }
    if (strstr(leaf, "reset"))        { snprintf(out, cap, "Reset"); return; }
    if (ieq(leaf, "outputs") || ieq(leaf, "out")) {
        snprintf(out, cap, "OUT%u", (unsigned)b->param1);
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

/* ---------- 刷新：跑在 ZMK 显示工作队列里 ---------- */
static void refresh_ui(void) {
    /* 当前层 */
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    zmk_keymap_layer_id_t layer_id = zmk_keymap_layer_index_to_id(index);
    const char *name = (layer_id == ZMK_KEYMAP_LAYER_ID_INVAL)
                           ? NULL : zmk_keymap_layer_name(layer_id);
    char layer[12];
    snprintf(layer, sizeof(layer), "%.9s", (name && name[0]) ? name : "?");
    set_text(layer_label, cache_layer, sizeof(cache_layer), layer);

    /* 输出状态 */
    enum zmk_transport transport = zmk_endpoint_get_selected().transport;
    if (transport == ZMK_TRANSPORT_NONE) {
        transport = zmk_endpoint_get_preferred_transport();
    }
    char out[8];
    switch (transport) {
    case ZMK_TRANSPORT_USB: snprintf(out, sizeof(out), "USB"); break;
    case ZMK_TRANSPORT_BLE: snprintf(out, sizeof(out), "BLE"); break;
    default:                snprintf(out, sizeof(out), "---"); break;
    }
    set_text(out_label, cache_out, sizeof(cache_out), out);

    /* 电量百分比：'+' = USB 供电中 */
    char bat[8];
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    snprintf(bat, sizeof(bat), "%s%u%%", zmk_usb_is_powered() ? "+" : "",
             (unsigned)zmk_battery_state_of_charge());
#else
    snprintf(bat, sizeof(bat), "%u%%", (unsigned)zmk_battery_state_of_charge());
#endif
    set_text(bat_label, cache_bat, sizeof(cache_bat), bat);

    /* 四个键位卡片：按下只在文字前加 '>'，不整块反白 */
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

    /* 底部提示 */
    char hint[24];
    if (layer_id == zmk_keymap_layer_default()) {
        snprintf(hint, sizeof(hint), "%s", "K1+K2=MENU");
    } else {
        const struct zmk_behavior_binding *k4 = effective_binding(3);
        bool k4_is_home = false;
        if (k4 != NULL && k4->behavior_dev != NULL) {
            const char *leaf = dev_leaf(k4->behavior_dev);
            k4_is_home = (ieq(leaf, "to_layer") || ieq(leaf, "to")) &&
                         (k4->param1 == (uint32_t)zmk_keymap_layer_default());
        }
        snprintf(hint, sizeof(hint), "%s", k4_is_home ? "K4=HOME" : "K1+K2=MENU");
    }
    set_text(hint_label, cache_hint, sizeof(cache_hint), hint);
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

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    lv_style_init(&screen_style);
    lv_style_set_bg_color(&screen_style, PIXEL_OFF()); /* 熄灭 = 黑底 */
    lv_style_set_bg_opa(&screen_style, LV_OPA_COVER);
    lv_style_set_text_color(&screen_style, PIXEL_ON()); /* 点亮 = 白字 */
    lv_style_set_text_font(&screen_style, FONT_KEY);
    lv_obj_add_style(screen, &screen_style, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /* 顶栏：输出 | 层名 | 电量 */
    out_label = make_label(screen, "---", FONT_TOP, PIXEL_ON());
    lv_obj_align(out_label, LV_ALIGN_TOP_LEFT, 2, 1);

    layer_label = make_label(screen, "?", FONT_TOP, PIXEL_ON());
    lv_obj_align(layer_label, LV_ALIGN_TOP_MID, 0, 1);

    bat_label = make_label(screen, "--%", FONT_TOP, PIXEL_ON());
    lv_obj_align(bat_label, LV_ALIGN_TOP_RIGHT, -2, 1);

    /* 分隔线 */
    lv_obj_t *line = lv_obj_create(screen);
    lv_obj_set_size(line, 124, 1);
    lv_obj_set_pos(line, 2, 12);
    lv_obj_set_style_bg_color(line, PIXEL_ON(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(line, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(line, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(line, 0, LV_PART_MAIN);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);

    /* 2x2 键位卡片：60x20，14px 大字体 */
    static const int16_t xs[2] = {2, 66};
    static const int16_t ys[2] = {15, 36};
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        lv_obj_t *card = lv_obj_create(screen);
        cards[i] = card;
        lv_obj_set_size(card, 60, 20);
        lv_obj_set_pos(card, xs[i % 2], ys[i / 2]);
        lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(card, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, PIXEL_ON(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(card, PIXEL_OFF(), LV_PART_MAIN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *label = make_label(card, "---", FONT_KEY, PIXEL_ON());
        key_labels[i] = label;
        lv_obj_set_size(label, 58, 19);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    }

    /* 底部提示 */
    hint_label = make_label(screen, "K1+K2=MENU", FONT_TOP, PIXEL_ON());
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_LEFT, 2, -1);

    refresh_ui();
    k_timer_start(&refresh_timer, K_MSEC(REFRESH_MS), K_MSEC(REFRESH_MS));
    return screen;
}
