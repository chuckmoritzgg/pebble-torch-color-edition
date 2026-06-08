#include <pebble.h>

// ─── Persist keys ────────────────────────────────────────────────────────────
#define KEY_IS_ON       0
#define KEY_COLOR_INDEX 1

// ─── Backlight retrigger interval (ms) ───────────────────────────────────────
// Must be shorter than the system backlight timeout (~2-3 s)
#define BACKLIGHT_RETRIGGER_MS 1000

// ─── Color palette — only compiled on Emery (RGB backlight) ──────────────────
#if defined(PBL_RGB_BACKLIGHT)
static const uint32_t COLOR_PALETTE[] = {
  0xFFFFFF,  // 0 – Full White  (default)
  0xFFD580,  // 1 – Warm White  (~3000 K)
  0xFF6A00,  // 2 – Orange
  0xFF1800,  // 3 – Red
  0x0060FF,  // 4 – Blue
};
#define COLOR_COUNT 5

static const char *COLOR_NAMES[] = {
  "White", "Warm White", "Orange", "Red", "Blue"
};
#endif  // PBL_RGB_BACKLIGHT

// ─── State ───────────────────────────────────────────────────────────────────
static Window    *s_window;
static TextLayer *s_state_label;    // ON / OFF
static TextLayer *s_color_label;    // color name (Emery only)
static TextLayer *s_hint_label;     // button hints
static AppTimer  *s_backlight_timer = NULL;

static bool s_is_on;        // backlight on or off
static int  s_color_index;  // 0–4 (Emery only)

// ─── Backlight retrigger timer ────────────────────────────────────────────────

static void backlight_timer_callback(void *ctx);

static void schedule_backlight_timer(void) {
  if (s_backlight_timer) {
    app_timer_cancel(s_backlight_timer);
    s_backlight_timer = NULL;
  }
  if (s_is_on) {
    s_backlight_timer = app_timer_register(
        BACKLIGHT_RETRIGGER_MS, backlight_timer_callback, NULL);
  }
}

static void backlight_timer_callback(void *ctx) {
  s_backlight_timer = NULL;
  if (s_is_on) {
    light_enable_interaction();
#if defined(PBL_RGB_BACKLIGHT)
    light_set_color_rgb888(COLOR_PALETTE[s_color_index]);
#endif
    schedule_backlight_timer();
  }
}

// ─── Core: apply state → hardware + UI + persist ─────────────────────────────
static void apply_light(void) {
  if (s_is_on) {
    light_enable_interaction();
#if defined(PBL_RGB_BACKLIGHT)
    light_set_color_rgb888(COLOR_PALETTE[s_color_index]);
#endif
    schedule_backlight_timer();
    // Bright white screen so no dark pixels block the LED
    window_set_background_color(s_window, GColorWhite);
    text_layer_set_text_color(s_state_label, GColorBlack);
    text_layer_set_text(s_state_label, "ON");
  } else {
    if (s_backlight_timer) {
      app_timer_cancel(s_backlight_timer);
      s_backlight_timer = NULL;
    }
    // Return to automatic backlight control — it will fade on its own
    light_enable(false);
    // Dark screen to signal the light is off
    window_set_background_color(s_window, GColorBlack);
    text_layer_set_text_color(s_state_label, GColorWhite);
    text_layer_set_text(s_state_label, "OFF");
  }

#if defined(PBL_RGB_BACKLIGHT)
  text_layer_set_text(s_color_label, s_is_on ? COLOR_NAMES[s_color_index] : "");
#endif

  persist_write_int(KEY_IS_ON,       s_is_on ? 1 : 0);
  persist_write_int(KEY_COLOR_INDEX, s_color_index);
}

// ─── Button handlers ─────────────────────────────────────────────────────────

// UP → turn ON
static void up_click_handler(ClickRecognizerRef recognizer, void *ctx) {
  if (!s_is_on) {
    s_is_on = true;
    apply_light();
  }
}

// DOWN → turn OFF
static void down_click_handler(ClickRecognizerRef recognizer, void *ctx) {
  if (s_is_on) {
    s_is_on = false;
    apply_light();
  }
}

// SELECT → cycle color (Emery only)
static void select_click_handler(ClickRecognizerRef recognizer, void *ctx) {
#if defined(PBL_RGB_BACKLIGHT)
  s_color_index = (s_color_index + 1) % COLOR_COUNT;
  apply_light();
#endif
}

static void click_config_provider(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP,     up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN,   down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
}

// ─── Window lifecycle ─────────────────────────────────────────────────────────
static void window_load(Window *window) {
  Layer *root   = window_get_root_layer(window);
  GRect  bounds = layer_get_bounds(root);
  int w = bounds.size.w;
  int h = bounds.size.h;

  // ON/OFF label (big, centred)
  s_state_label = text_layer_create(GRect(0, h / 2 - 36, w, 56));
  text_layer_set_text_alignment(s_state_label, GTextAlignmentCenter);
  text_layer_set_background_color(s_state_label, GColorClear);
  text_layer_set_font(s_state_label,
                      fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
  layer_add_child(root, text_layer_get_layer(s_state_label));

  // Color name row (Emery only)
  s_color_label = text_layer_create(GRect(0, h / 2 + 24, w, 28));
  text_layer_set_text_alignment(s_color_label, GTextAlignmentCenter);
  text_layer_set_background_color(s_color_label, GColorClear);
  text_layer_set_font(s_color_label,
                      fonts_get_system_font(FONT_KEY_GOTHIC_18));
  layer_add_child(root, text_layer_get_layer(s_color_label));

  // Hint row at bottom
#if defined(PBL_RGB_BACKLIGHT)
  const char *hint = "\xe2\x96\xb2 on   \xe2\x96\xbc off   SEL: color";
#else
  const char *hint = "\xe2\x96\xb2 on         \xe2\x96\xbc off";
#endif
  s_hint_label = text_layer_create(GRect(4, h - 36, w - 8, 32));
  text_layer_set_text_alignment(s_hint_label, GTextAlignmentCenter);
  text_layer_set_background_color(s_hint_label, GColorClear);
  text_layer_set_font(s_hint_label, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text(s_hint_label, hint);
  layer_add_child(root, text_layer_get_layer(s_hint_label));

  // ── Restore persisted values ──────────────────────────────────────────
  s_is_on = persist_exists(KEY_IS_ON)
              ? (persist_read_int(KEY_IS_ON) != 0)
              : true;  // First launch → ON

  if (persist_exists(KEY_COLOR_INDEX)) {
    s_color_index = persist_read_int(KEY_COLOR_INDEX);
#if defined(PBL_RGB_BACKLIGHT)
    if (s_color_index < 0 || s_color_index >= COLOR_COUNT)
      s_color_index = 0;
#else
    s_color_index = 0;
#endif
  } else {
    s_color_index = 0;  // Default → Full White
  }

  apply_light();
}

static void window_unload(Window *window) {
  if (s_backlight_timer) {
    app_timer_cancel(s_backlight_timer);
    s_backlight_timer = NULL;
  }
  light_enable(false);

  text_layer_destroy(s_state_label);
  text_layer_destroy(s_color_label);
  text_layer_destroy(s_hint_label);
}

// ─── App entry point ─────────────────────────────────────────────────────────
static void init(void) {
  s_window = window_create();
  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}

static void deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}