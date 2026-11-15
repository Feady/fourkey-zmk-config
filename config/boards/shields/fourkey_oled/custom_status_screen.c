/*
 * fourkey_oled - B24 混合状态屏（128x64 单色 SSD1306）
 *
 * 布局：
 *   y=0..10   顶栏：USB/BLE | 当前层名 | +87%（+ 表示 USB 供电中）
 *   y=11      分隔线
 *   y=14..53  2x2 键位卡片，位置与实物 2x2 一一对应，按下反白
 *   y=56..63  底部提示（K1+K2=MENU / K4=HOME）
 *
 * 关键点（与 ZMK Studio 在线改键共存）：
 *   卡片文字**不是**写死的层号->标签表，而是每次刷新调用官方 API
 *   zmk_keymap_get_layer_binding_at_idx() 读取运行时 keymap 的
 *   behavior_dev / param1 / param2，再渲染成短标签。
 *   Studio 改键、改层名、重排层之后，屏幕 200ms 内自动同步，不会说谎。
 *
 * 参考 ZMK 官方实现：
 *   app/src/display/main.c                    (weak zmk_display_status_screen)
 *   app/src/display/widgets/*                 (ZMK_DISPLAY_WIDGET_LISTENER 线程模型)
 *   app/boards/shields/nice_view/             (自定义屏 CMake / Kconfig 范例)
 *   englmaxi/zmk-dongle-display              (显式 lv_style + 8px 字体范例)
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
#define LABEL_CAP 14
#define REFRESH_MS 200

/* ---------- LVGL 对象 ---------- */
static lv_obj_t *out_label;
static lv_obj_t *layer_label;
static lv_obj_t *bat_label;
static lv_obj_t *hint_label;
static lv_obj_t *cards[KEY_COUNT];
static lv_obj_t *key_labels[KEY_COUNT];
static lv_style_t screen_style;

/* ---------- 缓存：只有文本真的变了才动 LVGL，避免每 200ms 全屏重绘 ---------- */
static char cache_layer[12];
static char cache_out[8];
static char cache_bat[8];
static char cache_hint[24];
static char cache_keys[KEY_COUNT][LABEL_CAP];
static bool card_pressed[KEY_COUNT];
static volatile bool key_down[KEY_COUNT];

/* ---------- 小工具 ---------- */

/* behavior_dev 可能是 "key_press"，也可能是带路径的 "/behaviors/key_press" */
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

/* HID usage -> 短名称。ZMK_HID_USAGE(page,id) = (page << 16) | id，
   修饰位在 param1 的最高字节（LC()/LS()/LA()/LG()，见 modifiers.h）。 */
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
    /* 未收录的 usage 显示十六进制，仍然准确，不会显示成别的键 */
    snprintf(out, cap, "%04X", (unsigned)id);
}

static void render_kp(uint32_t param, char *out, size_t cap) {
    uint8_t mods = (param >> 24) & 0xFF;
    uint16_t page = (param >> 16) & 0xFF;
    uint16_t id = param & 0xFFFF;

    char key[8];
    usage_name(page, id, key, sizeof(key));

    char prefix[12] = "";
    if (mods & 0x01) { strcat(prefix, "C+"); } /* LCTL */
    if (mods & 0x02) { strcat(prefix, "S+"); } /* LSFT */
    if (mods & 0x04) { strcat(prefix, "A+"); } /* LALT */
    if (mods & 0x08) { strcat(prefix, "G+"); } /* LGUI */
    if (mods & 0xF0) { strcat(prefix, "R+"); } /* 右侧修饰 */
    if (mods && prefix[0] == '\0') { strcat(prefix, "M+"); }

    snprintf(out, cap, "%s%s", prefix, key);
}

/* 用官方 API 取层名：Studio 里重命名层后会同步变化。
   注意 behavior binding 的层参数是 layer ID（见 behavior_to_layer.c 的
   zmk_keymap_layer_to(binding->param1, ...) 与 LAYER_ID 元数据），
   不是 layer index，重排层后也不会错位。 */
static const char *layer_name_for(uint8_t layer_id) {
    if (layer_id == ZMK_KEYMAP_LAYER_ID_INVAL) {
        return "?";
    }
    const char *name = zmk_keymap_layer_name(layer_id);
    return (name && name[0]) ? name : "?";
}

/* 把一个运行时 binding 渲染成 <=13 字符的卡片标签 */
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
    /* &lt <layer> <keycode>：param1=层，param2=短按键码 */
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
    /* bt 行为节点名在不同 ZMK 版本里可能是 bt / bluetooth / behavior_bt，
       统一用 "bt" 子串兜底；其余内置行为名都不含 "bt"。 */
    if (ieq(leaf, "bt") || ieq(leaf, "behavior_bt") || strstr(leaf, "bt") != NULL) {
        /* dt-bindings/zmk/bt.h：0=CLR 1=NXT 2=PRV 3=SEL 4=CLR_ALL 5=DISC */
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
    if (strstr(leaf, "alt_tab"))      { snprintf(out, cap, "Alt+Tab"); return; }
    if (ieq(leaf, "none"))           { snprintf(out, cap, "---"); return; }
    if (strstr(leaf, "bootloader"))   { snprintf(out, cap, "Boot"); return; }
    if (strstr(leaf, "reset"))        { snprintf(out, cap, "Reset"); return; }
    if (ieq(leaf, "outputs") || ieq(leaf, "out")) {
        snprintf(out, cap, "OUT%u", (unsigned)b->param1);
        return;
    }
    /* 未知行为：直接显示设备名，宁可朴素也不显示错误信息 */
    snprintf(out, cap, "%.11s", leaf);
}

/* 从最高激活层往下找第一个非 &trans 的 binding，与 ZMK 实际派发逻辑一致 */
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

/* ---------- 刷新：跑在 ZMK 显示工作队列里，LVGL 调用线程安全 ---------- */
static void refresh_ui(void) {
    /* 当前层 */
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    zmk_keymap_layer_id_t layer_id = zmk_keymap_layer_index_to_id(index);
    const char *name = (layer_id == ZMK_KEYMAP_LAYER_ID_INVAL)
                           ? NULL : zmk_keymap_layer_name(layer_id);
    char layer[12];
    snprintf(layer, sizeof(layer), "%.10s", (name && name[0]) ? name : "?");
    set_text(layer_label, cache_layer, sizeof(cache_layer), layer);

    /* 输出状态：未连接时显示"正在尝试"的通道，行为与官方 output widget 一致 */
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

    /* 电量百分比，'+' = USB 供电中 */
    char bat[8];
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    snprintf(bat, sizeof(bat), "%s%u%%", zmk_usb_is_powered() ? "+" : "",
             (unsigned)zmk_battery_state_of_charge());
#else
    snprintf(bat, sizeof(bat), "%u%%", (unsigned)zmk_battery_state_of_charge());
#endif
    set_text(bat_label, cache_bat, sizeof(cache_bat), bat);

    /* 四个键位卡片 */
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        char text[LABEL_CAP];
        render_binding(effective_binding(i), text, sizeof(text));
        set_text(key_labels[i], cache_keys[i], sizeof(cache_keys[i]), text);

        bool down = key_down[i];
        if (down != card_pressed[i]) {
            card_pressed[i] = down;
            lv_color_t bg = down ? lv_color_white() : lv_color_black();
            lv_color_t fg = down ? lv_color_black() : lv_color_white();
            lv_obj_set_style_bg_color(cards[i], bg, LV_PART_MAIN);
            lv_obj_set_style_text_color(cards[i], fg, LV_PART_MAIN);
            lv_obj_set_style_text_color(key_labels[i], fg, LV_PART_MAIN);
        }
    }

    /* 底部提示：主页面提示组合键，子页面提示 K4 回主页面。
       判断依据是运行时层 id 与 K4 的真实 binding，Studio 改键/改名后仍成立。 */
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

/* 按键按下/释放：只写标志，LVGL 反白交给显示队列里的刷新做 */
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
static lv_obj_t *make_label(lv_obj_t *parent, const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_8, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_label_set_text(label, text);
    return label;
}

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    lv_style_init(&screen_style);
    lv_style_set_bg_color(&screen_style, lv_color_black());
    lv_style_set_bg_opa(&screen_style, LV_OPA_COVER);
    lv_style_set_text_color(&screen_style, lv_color_white());
    lv_style_set_text_font(&screen_style, &lv_font_montserrat_8);
    lv_obj_add_style(screen, &screen_style, LV_PART_MAIN);

    /* 顶栏 */
    out_label = make_label(screen, "---");
    lv_obj_align(out_label, LV_ALIGN_TOP_LEFT, 2, 1);

    layer_label = make_label(screen, "?");
    lv_obj_align(layer_label, LV_ALIGN_TOP_MID, 0, 1);

    bat_label = make_label(screen, "--%");
    lv_obj_align(bat_label, LV_ALIGN_TOP_RIGHT, -2, 1);

    /* 分隔线 */
    lv_obj_t *line = lv_obj_create(screen);
    lv_obj_set_size(line, 124, 1);
    lv_obj_set_pos(line, 2, 11);
    lv_obj_set_style_bg_color(line, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(line, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(line, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(line, 0, LV_PART_MAIN);

    /* 2x2 键位卡片：位置 0/1 第一排，2/3 第二排 */
    static const int16_t xs[2] = {2, 66};
    static const int16_t ys[2] = {14, 35};
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        lv_obj_t *card = lv_obj_create(screen);
        cards[i] = card;
        lv_obj_set_size(card, 60, 19);
        lv_obj_set_pos(card, xs[i % 2], ys[i / 2]);
        lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(card, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(card, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_text_color(card, lv_color_white(), LV_PART_MAIN);

        lv_obj_t *label = make_label(card, "---");
        key_labels[i] = label;
        lv_obj_set_size(label, 56, 12);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    }

    /* 底部提示 */
    hint_label = make_label(screen, "K4=HOME");
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_LEFT, 2, 0);

    refresh_ui();
    k_timer_start(&refresh_timer, K_MSEC(REFRESH_MS), K_MSEC(REFRESH_MS));
    return screen;
}
