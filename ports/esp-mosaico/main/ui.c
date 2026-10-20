// Port of magpie-usage-display's LVGL UI (MIT, peterroe) to ESP-IDF.
#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"

LV_IMG_DECLARE(magpie_logo);

#define HEX(v) lv_color_hex(v)

static const uint32_t kGreen = 0xA8FF4F;
static const uint32_t kWhite = 0xF4F7ED;
static const uint32_t kMuted = 0x718079;
static const uint32_t kPanel = 0x0B1210;
static const uint32_t kTrack = 0x1A231E;
static const uint32_t kDivider = 0x1E2A25;
static const uint32_t kAmber = 0xFFB02E;
static const uint32_t kRed = 0xFF5F57;
static const uint32_t kBlue = 0x55E6D2;
static const uint32_t kViolet = 0xA5B4FC;

static ui_action_handler_t s_action_handler;

static lv_obj_t *s_tileview;
static lv_obj_t *s_pages[4];
static lv_obj_t *s_page_dots[4];
static lv_obj_t *s_time_label;
static lv_obj_t *s_connection_label;

static lv_obj_t *s_success_arc;
static lv_obj_t *s_success_value_label;
static lv_obj_t *s_calls_value_label;
static lv_obj_t *s_errors_value_label;
static lv_obj_t *s_quota_label;

static lv_obj_t *s_request_tokens_label;
static lv_obj_t *s_request_ttft_label;
static lv_obj_t *s_request_latest_model_label;
static lv_obj_t *s_request_status_label;
static lv_obj_t *s_request_latency_label;

static lv_obj_t *s_session_count_label;
static lv_obj_t *s_session_tokens_label;
static lv_obj_t *s_session_cost_label;
static lv_obj_t *s_top_model_label;
static lv_obj_t *s_top_model_calls_label;

static lv_obj_t *s_cache_arc;
static lv_obj_t *s_cache_value_label;
static lv_obj_t *s_context_input_label;
static lv_obj_t *s_context_output_label;
static lv_obj_t *s_context_cache_label;
static lv_obj_t *s_context_reasoning_label;
static lv_obj_t *s_mix_segments[4];

static lv_obj_t *s_quick_actions_overlay;
static lv_obj_t *s_pairing_overlay;
static lv_obj_t *s_pairing_code_label;
static lv_obj_t *s_toast;
static lv_timer_t *s_toast_timer;

static void style_label(lv_obj_t *label, const lv_font_t *font,
                        uint32_t text_color) {
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, HEX(text_color), LV_PART_MAIN);
  lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN);
}

static lv_obj_t *create_label(lv_obj_t *parent, const char *text,
                              const lv_font_t *font, uint32_t text_color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  style_label(label, font, text_color);
  return label;
}

static void create_page_title(lv_obj_t *page, const char *title,
                              const char *range) {
  lv_obj_t *label = create_label(page, title, &lv_font_montserrat_18, kGreen);
  lv_obj_set_pos(label, 24, 8);
  lv_obj_t *range_label =
      create_label(page, range, &lv_font_montserrat_14, kMuted);
  lv_obj_align(range_label, LV_ALIGN_TOP_RIGHT, -24, 11);
}

static lv_obj_t *create_card(lv_obj_t *parent, int x, int y, int width,
                             int height, const char *title,
                             lv_obj_t **value_label) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, width, height);
  lv_obj_set_style_radius(card, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(card, HEX(kPanel), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title_label = create_label(card, title, &lv_font_montserrat_14, kMuted);
  lv_obj_set_pos(title_label, 12, 9);
  *value_label = create_label(card, "--", &lv_font_montserrat_32, kWhite);
  lv_obj_set_pos(*value_label, 12, 25);
  return card;
}

static void create_ring(lv_obj_t *parent, lv_obj_t **arc, lv_obj_t **value,
                        const char *caption, int x, int y, int size) {
  *arc = lv_arc_create(parent);
  lv_obj_set_pos(*arc, x, y);
  lv_obj_set_size(*arc, size, size);
  lv_arc_set_rotation(*arc, 270);
  lv_arc_set_bg_angles(*arc, 0, 360);
  lv_arc_set_range(*arc, 0, 1000);
  lv_arc_set_value(*arc, 0);
  lv_obj_remove_style(*arc, NULL, LV_PART_KNOB);
  lv_obj_set_style_arc_width(*arc, 12, LV_PART_MAIN);
  lv_obj_set_style_arc_color(*arc, HEX(kTrack), LV_PART_MAIN);
  lv_obj_set_style_arc_width(*arc, 12, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(*arc, HEX(kGreen), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(*arc, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_clear_flag(*arc, LV_OBJ_FLAG_CLICKABLE);

  *value = create_label(parent, "--.-", &lv_font_montserrat_48, kWhite);
  lv_obj_align_to(*value, *arc, LV_ALIGN_CENTER, 0, -16);
  lv_obj_t *unit_label = create_label(parent, "%", &lv_font_montserrat_18, kGreen);
  lv_obj_align_to(unit_label, *arc, LV_ALIGN_CENTER, 0, 25);
  lv_obj_t *caption_label =
      create_label(parent, caption, &lv_font_montserrat_14, kMuted);
  lv_obj_align_to(caption_label, *arc, LV_ALIGN_CENTER, 0, 49);
}

static void refresh_page_dots(void) {
  lv_obj_t *active = lv_tileview_get_tile_act(s_tileview);
  for (size_t i = 0; i < 4; ++i) {
    const bool selected = s_pages[i] == active;
    lv_obj_set_style_bg_color(s_page_dots[i],
                              HEX(selected ? kGreen : kMuted), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_page_dots[i], selected ? LV_OPA_COVER : LV_OPA_50,
                            LV_PART_MAIN);
  }
}

static void on_page_changed(lv_event_t *event) {
  (void)event;
  refresh_page_dots();
}

static void dispatch_action(lv_event_t *event) {
  if (s_action_handler == NULL) {
    return;
  }
  const uintptr_t raw = (uintptr_t)lv_event_get_user_data(event);
  s_action_handler((ui_action_t)raw);
}

static void close_overlay(lv_event_t *event) {
  (void)event;
  lv_obj_add_flag(s_quick_actions_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void close_pairing_overlay(lv_event_t *event) {
  (void)event;
  ui_hide_pairing_code();
}

static void dispatch_overlay_action(lv_event_t *event) {
  lv_obj_add_flag(s_quick_actions_overlay, LV_OBJ_FLAG_HIDDEN);
  dispatch_action(event);
}

static void create_action_button(lv_obj_t *parent, const char *text, int y,
                                 ui_action_t action) {
  lv_obj_t *button = lv_btn_create(parent);
  lv_obj_set_pos(button, 50, y);
  lv_obj_set_size(button, 350, 58);
  lv_obj_set_style_radius(button, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, HEX(kPanel), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(button, HEX(kGreen), LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(button, dispatch_overlay_action, LV_EVENT_CLICKED,
                      (void *)(uintptr_t)action);

  lv_obj_t *label = create_label(button, text, &lv_font_montserrat_18, kWhite);
  lv_obj_center(label);
}

static void hide_toast(lv_timer_t *timer) {
  (void)timer;
  lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
  s_toast_timer = NULL;
}

static void format_latency(uint32_t ms, char *out, size_t out_size) {
  if (ms == 0) {
    snprintf(out, out_size, "-");
  } else if (ms < 1000) {
    snprintf(out, out_size, "%lums", (unsigned long)ms);
  } else {
    snprintf(out, out_size, "%.1fs", (double)ms / 1000.0);
  }
}

static void create_overview_page(lv_obj_t *page) {
  create_page_title(page, "OVERVIEW", "TODAY");
  create_ring(page, &s_success_arc, &s_success_value_label, "SUCCESS", 24, 52,
              220);

  lv_obj_t *metrics = lv_obj_create(page);
  lv_obj_remove_style_all(metrics);
  lv_obj_set_pos(metrics, 268, 58);
  lv_obj_set_size(metrics, 188, 220);
  lv_obj_clear_flag(metrics, LV_OBJ_FLAG_SCROLLABLE);
  create_card(metrics, 0, 0, 188, 64, "CALLS", &s_calls_value_label);
  create_card(metrics, 0, 76, 188, 64, "ERRORS", &s_errors_value_label);
  create_card(metrics, 0, 152, 188, 64, "LOWEST QUOTA", &s_quota_label);
}

static void create_requests_page(lv_obj_t *page) {
  create_page_title(page, "REQUESTS", "TODAY");

  lv_obj_t *model_title =
      create_label(page, "LATEST MODEL", &lv_font_montserrat_14, kMuted);
  lv_obj_set_pos(model_title, 24, 54);
  s_request_latest_model_label =
      create_label(page, "-", &lv_font_montserrat_32, kWhite);
  lv_obj_set_pos(s_request_latest_model_label, 24, 79);
  lv_obj_set_width(s_request_latest_model_label, 432);
  lv_label_set_long_mode(s_request_latest_model_label, LV_LABEL_LONG_DOT);

  create_card(page, 24, 145, 205, 82, "STATUS", &s_request_status_label);
  create_card(page, 251, 145, 205, 82, "LATENCY", &s_request_latency_label);
  create_card(page, 24, 242, 205, 82, "AVG TTFT", &s_request_ttft_label);
  create_card(page, 251, 242, 205, 82, "TOKENS TODAY",
              &s_request_tokens_label);
}

static void create_sessions_page(lv_obj_t *page) {
  create_page_title(page, "SESSIONS", "30 DAYS");

  lv_obj_t *count_title =
      create_label(page, "SESSIONS", &lv_font_montserrat_14, kMuted);
  lv_obj_set_pos(count_title, 24, 43);
  s_session_count_label =
      create_label(page, "--", &lv_font_montserrat_48, kWhite);
  lv_obj_set_pos(s_session_count_label, 24, 60);

  create_card(page, 24, 135, 205, 82, "TOTAL TOKENS", &s_session_tokens_label);
  create_card(page, 251, 135, 205, 82, "EST. COST", &s_session_cost_label);

  lv_obj_t *model_title =
      create_label(page, "TOP MODEL", &lv_font_montserrat_14, kMuted);
  lv_obj_set_pos(model_title, 24, 258);
  s_top_model_label = create_label(page, "-", &lv_font_montserrat_32, kWhite);
  lv_obj_set_pos(s_top_model_label, 24, 284);
  lv_obj_set_width(s_top_model_label, 300);
  lv_label_set_long_mode(s_top_model_label, LV_LABEL_LONG_DOT);
  s_top_model_calls_label =
      create_label(page, "-", &lv_font_montserrat_18, kGreen);
  lv_obj_align(s_top_model_calls_label, LV_ALIGN_TOP_RIGHT, -24, 290);
}

static void create_context_page(lv_obj_t *page) {
  create_page_title(page, "CONTEXT", "GATEWAY OBSERVED");
  create_ring(page, &s_cache_arc, &s_cache_value_label, "CACHE HIT", 140, 42,
              200);

  lv_obj_t *mix_title =
      create_label(page, "TOKEN MIX", &lv_font_montserrat_14, kMuted);
  lv_obj_set_pos(mix_title, 24, 258);
  const uint32_t colors[] = {kBlue, kGreen, kViolet, kAmber};
  for (size_t i = 0; i < 4; ++i) {
    s_mix_segments[i] = lv_obj_create(page);
    lv_obj_remove_style_all(s_mix_segments[i]);
    lv_obj_set_pos(s_mix_segments[i], 24, 282);
    lv_obj_set_size(s_mix_segments[i], 0, 8);
    lv_obj_set_style_bg_color(s_mix_segments[i], HEX(colors[i]), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_mix_segments[i], LV_OPA_COVER, LV_PART_MAIN);
  }

  s_context_input_label =
      create_label(page, "INPUT --", &lv_font_montserrat_14, kBlue);
  lv_obj_set_pos(s_context_input_label, 24, 306);
  s_context_output_label =
      create_label(page, "OUTPUT --", &lv_font_montserrat_14, kGreen);
  lv_obj_set_pos(s_context_output_label, 251, 306);
  s_context_cache_label =
      create_label(page, "CACHE --", &lv_font_montserrat_14, kViolet);
  lv_obj_set_pos(s_context_cache_label, 24, 338);
  s_context_reasoning_label =
      create_label(page, "REASON --", &lv_font_montserrat_14, kAmber);
  lv_obj_set_pos(s_context_reasoning_label, 251, 338);
}

void ui_create(ui_action_handler_t handler) {
  s_action_handler = handler;

  lv_obj_t *screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, HEX(0x030706), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *logo = lv_img_create(screen);
  lv_img_set_src(logo, &magpie_logo);
  lv_obj_set_pos(logo, 20, 10);
  lv_obj_t *brand_label =
      create_label(screen, "MAGPIE", &lv_font_montserrat_18, kGreen);
  lv_obj_set_pos(brand_label, 68, 11);
  lv_obj_t *gateway_label =
      create_label(screen, "GATEWAY", &lv_font_montserrat_14, kMuted);
  lv_obj_set_pos(gateway_label, 68, 31);

  s_connection_label = create_label(screen, LV_SYMBOL_BLUETOOTH " OFFLINE",
                                    &lv_font_montserrat_14, kRed);
  lv_obj_align(s_connection_label, LV_ALIGN_TOP_RIGHT, -22, 17);
  lv_obj_add_flag(s_connection_label, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(s_connection_label, 12);
  lv_obj_add_event_cb(s_connection_label, dispatch_action, LV_EVENT_CLICKED,
                      (void *)(uintptr_t)UI_ACTION_RECONNECT);

  s_tileview = lv_tileview_create(screen);
  lv_obj_set_pos(s_tileview, 0, 56);
  lv_obj_set_size(s_tileview, 480, 382);
  lv_obj_set_style_bg_opa(s_tileview, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_tileview, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_tileview, 0, LV_PART_MAIN);
  lv_obj_set_scrollbar_mode(s_tileview, LV_SCROLLBAR_MODE_OFF);
  s_pages[0] = lv_tileview_add_tile(s_tileview, 0, 0, LV_DIR_RIGHT);
  s_pages[1] = lv_tileview_add_tile(s_tileview, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
  s_pages[2] = lv_tileview_add_tile(s_tileview, 2, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
  s_pages[3] = lv_tileview_add_tile(s_tileview, 3, 0, LV_DIR_LEFT);
  for (size_t i = 0; i < 4; ++i) {
    lv_obj_set_style_bg_opa(s_pages[i], LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_pages[i], 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_SCROLLABLE);
  }
  create_overview_page(s_pages[0]);
  create_requests_page(s_pages[1]);
  create_sessions_page(s_pages[2]);
  create_context_page(s_pages[3]);
  lv_obj_add_event_cb(s_tileview, on_page_changed, LV_EVENT_VALUE_CHANGED, NULL);

  s_time_label = create_label(screen, "--/-- --:--", &lv_font_montserrat_14, kMuted);
  lv_obj_set_pos(s_time_label, 20, 452);
  for (size_t i = 0; i < 4; ++i) {
    s_page_dots[i] = lv_obj_create(screen);
    lv_obj_remove_style_all(s_page_dots[i]);
    lv_obj_set_pos(s_page_dots[i], 213 + (int)i * 18, 459);
    lv_obj_set_size(s_page_dots[i], 7, 7);
    lv_obj_set_style_radius(s_page_dots[i], LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_page_dots[i], HEX(i == 0 ? kGreen : kMuted),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_page_dots[i], i == 0 ? LV_OPA_COVER : LV_OPA_50,
                            LV_PART_MAIN);
  }

  s_quick_actions_overlay = lv_obj_create(screen);
  lv_obj_set_size(s_quick_actions_overlay, 480, 480);
  lv_obj_set_pos(s_quick_actions_overlay, 0, 0);
  lv_obj_set_style_bg_color(s_quick_actions_overlay, HEX(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_quick_actions_overlay, LV_OPA_90, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_quick_actions_overlay, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_quick_actions_overlay, 0, LV_PART_MAIN);
  lv_obj_clear_flag(s_quick_actions_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *overlay_title = create_label(s_quick_actions_overlay,
                                         "GATEWAY ACTIONS", &lv_font_montserrat_18,
                                         kMuted);
  lv_obj_align(overlay_title, LV_ALIGN_TOP_MID, 0, 72);
  create_action_button(s_quick_actions_overlay, "FOCUS MAGPIE", 130,
                       UI_ACTION_FOCUS_MAGPIE);
  create_action_button(s_quick_actions_overlay, "REFRESH", 203, UI_ACTION_REFRESH);
  lv_obj_t *cancel = lv_btn_create(s_quick_actions_overlay);
  lv_obj_set_pos(cancel, 50, 276);
  lv_obj_set_size(cancel, 350, 58);
  lv_obj_set_style_radius(cancel, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(cancel, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_color(cancel, HEX(kDivider), LV_PART_MAIN);
  lv_obj_set_style_border_width(cancel, 1, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(cancel, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(cancel, close_overlay, LV_EVENT_CLICKED, NULL);
  lv_obj_t *cancel_label = create_label(cancel, "CANCEL", &lv_font_montserrat_18, kMuted);
  lv_obj_center(cancel_label);
  lv_obj_add_flag(s_quick_actions_overlay, LV_OBJ_FLAG_HIDDEN);

  s_pairing_overlay = lv_obj_create(screen);
  lv_obj_set_size(s_pairing_overlay, 420, 250);
  lv_obj_align(s_pairing_overlay, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_radius(s_pairing_overlay, 18, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_pairing_overlay, HEX(0x061014), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_pairing_overlay, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(s_pairing_overlay, HEX(kGreen), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_pairing_overlay, 1, LV_PART_MAIN);
  lv_obj_clear_flag(s_pairing_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_pairing_overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_pairing_overlay, close_pairing_overlay, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_t *pairing_title = create_label(s_pairing_overlay, "PAIR MAGPIE DISPLAY",
                                         &lv_font_montserrat_18, kMuted);
  lv_obj_align(pairing_title, LV_ALIGN_TOP_MID, 0, 34);
  s_pairing_code_label =
      create_label(s_pairing_overlay, "000 000", &lv_font_montserrat_48, kWhite);
  lv_obj_align(s_pairing_code_label, LV_ALIGN_CENTER, 0, 10);
  lv_obj_t *pairing_hint = create_label(s_pairing_overlay, "ENTER ON YOUR MAC",
                                        &lv_font_montserrat_14, kMuted);
  lv_obj_align(pairing_hint, LV_ALIGN_BOTTOM_MID, 0, -30);
  lv_obj_add_flag(s_pairing_overlay, LV_OBJ_FLAG_HIDDEN);

  s_toast = create_label(screen, "", &lv_font_montserrat_14, kWhite);
  lv_obj_set_style_bg_color(s_toast, HEX(0x182024), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(s_toast, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(s_toast, 18, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(s_toast, 12, LV_PART_MAIN);
  lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -42);
  lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
}

void ui_update(const app_state_t *state, uint32_t elapsed_seconds) {
  char buffer[32];

  format_datetime(state, elapsed_seconds, buffer, sizeof(buffer));
  lv_label_set_text(s_time_label, buffer);

  const bool linked = state->connection == CONN_LINKED;
  const bool stale = state->connection == CONN_STALE;
  const char *status =
      linked ? "ONLINE" : (stale ? "STALE" : connection_label(state->connection));
  lv_label_set_text_fmt(s_connection_label, LV_SYMBOL_BLUETOOTH " %s", status);
  lv_obj_set_style_text_color(
      s_connection_label, HEX(linked ? kGreen : (stale ? kAmber : kRed)),
      LV_PART_MAIN);

  if (!state->data_valid) {
    lv_arc_set_value(s_success_arc, 0);
    lv_arc_set_value(s_cache_arc, 0);
    lv_label_set_text(s_success_value_label, "--.-");
    lv_label_set_text(s_cache_value_label, "--.-");
    lv_label_set_text(s_request_latest_model_label, "WAITING FOR DATA");
    return;
  }

  lv_arc_set_value(s_success_arc,
                   state->success_permille > 1000 ? 1000 : state->success_permille);
  lv_label_set_text_fmt(s_success_value_label, "%u.%u",
                        state->success_permille / 10, state->success_permille % 10);
  lv_obj_align_to(s_success_value_label, s_success_arc, LV_ALIGN_CENTER, 0, -16);

  format_compact_tokens(state->calls_today, buffer, sizeof(buffer));
  lv_label_set_text(s_calls_value_label, buffer);
  format_compact_tokens(state->errors_today, buffer, sizeof(buffer));
  lv_label_set_text(s_errors_value_label, buffer);
  lv_obj_set_style_text_color(s_errors_value_label,
                              HEX(state->errors_today > 0 ? kAmber : kWhite),
                              LV_PART_MAIN);
  lv_label_set_text_fmt(s_quota_label, "%u%%", state->remaining_percent);

  const bool latest_ok = !state->latest_failed && state->latest_status >= 200 &&
                         state->latest_status < 400;
  format_compact_tokens(state->tokens_today, buffer, sizeof(buffer));
  lv_label_set_text(s_request_tokens_label, buffer);
  format_latency(state->average_ttft_ms, buffer, sizeof(buffer));
  lv_label_set_text(s_request_ttft_label, buffer);
  lv_label_set_text(s_request_latest_model_label, state->latest_model);
  lv_label_set_text_fmt(s_request_status_label, "%u", state->latest_status);
  format_latency(state->latest_latency_ms, buffer, sizeof(buffer));
  lv_label_set_text(s_request_latency_label, buffer);
  lv_obj_set_style_text_color(s_request_status_label,
                              HEX(latest_ok ? kGreen : kAmber), LV_PART_MAIN);

  const uint64_t total_tokens = state->total_input + state->total_output +
                                state->total_cache_read + state->total_reasoning;
  lv_label_set_text_fmt(s_session_count_label, "%u", state->session_count);
  format_compact_tokens(total_tokens, buffer, sizeof(buffer));
  lv_label_set_text(s_session_tokens_label, buffer);
  snprintf(buffer, sizeof(buffer), "$%.3f",
           (double)state->total_cost_micros / 1000000.0);
  lv_label_set_text(s_session_cost_label, buffer);
  lv_label_set_text(s_top_model_label, state->top_model);
  lv_label_set_text_fmt(s_top_model_calls_label, "%lu CALLS",
                        (unsigned long)state->top_model_calls);

  lv_arc_set_value(s_cache_arc,
                   state->cache_hit_permille > 1000 ? 1000 : state->cache_hit_permille);
  lv_label_set_text_fmt(s_cache_value_label, "%u.%u",
                        state->cache_hit_permille / 10,
                        state->cache_hit_permille % 10);
  lv_obj_align_to(s_cache_value_label, s_cache_arc, LV_ALIGN_CENTER, 0, -16);

  char input[24], output[24], cache[24], reasoning[24];
  format_compact_tokens(state->total_input, input, sizeof(input));
  format_compact_tokens(state->total_output, output, sizeof(output));
  format_compact_tokens(state->total_cache_read, cache, sizeof(cache));
  format_compact_tokens(state->total_reasoning, reasoning, sizeof(reasoning));
  lv_label_set_text_fmt(s_context_input_label, "INPUT %s", input);
  lv_label_set_text_fmt(s_context_output_label, "OUTPUT %s", output);
  lv_label_set_text_fmt(s_context_cache_label, "CACHE %s", cache);
  lv_label_set_text_fmt(s_context_reasoning_label, "REASON %s", reasoning);

  const uint64_t parts[] = {state->total_input, state->total_output,
                            state->total_cache_read, state->total_reasoning};
  int x = 24;
  int remaining_width = 432;
  for (size_t i = 0; i < 4; ++i) {
    const int width =
        i == 3 ? remaining_width
               : (total_tokens > 0
                      ? (int)(432ULL * parts[i] / total_tokens)
                      : 0);
    lv_obj_set_pos(s_mix_segments[i], x, 282);
    lv_obj_set_width(s_mix_segments[i], width);
    x += width;
    remaining_width -= width;
  }
}

void ui_show_quick_actions(void) {
  lv_obj_clear_flag(s_quick_actions_overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_quick_actions_overlay);
}

void ui_cycle_page(void) {
  if (s_tileview == NULL) {
    return;
  }
  lv_obj_t *active = lv_tileview_get_tile_act(s_tileview);
  size_t current = 0;
  for (size_t i = 0; i < 4; ++i) {
    if (s_pages[i] == active) {
      current = i;
      break;
    }
  }
  lv_obj_set_tile(s_tileview, s_pages[(current + 1) % 4], LV_ANIM_ON);
  refresh_page_dots();
}

bool ui_modal_visible(void) {
  return (s_quick_actions_overlay != NULL &&
          !lv_obj_has_flag(s_quick_actions_overlay, LV_OBJ_FLAG_HIDDEN)) ||
         (s_pairing_overlay != NULL &&
          !lv_obj_has_flag(s_pairing_overlay, LV_OBJ_FLAG_HIDDEN));
}

void ui_show_toast(const char *text) {
  lv_label_set_text(s_toast, text);
  lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -42);
  lv_obj_move_foreground(s_toast);
  if (s_toast_timer != NULL) {
    lv_timer_del(s_toast_timer);
  }
  s_toast_timer = lv_timer_create(hide_toast, 2000, NULL);
  lv_timer_set_repeat_count(s_toast_timer, 1);
}

void ui_show_pairing_code(uint32_t passkey) {
  lv_label_set_text_fmt(s_pairing_code_label, "%03lu %03lu",
                        (unsigned long)(passkey / 1000UL),
                        (unsigned long)(passkey % 1000UL));
  lv_obj_clear_flag(s_pairing_overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_pairing_overlay);
}

void ui_hide_pairing_code(void) {
  lv_obj_add_flag(s_pairing_overlay, LV_OBJ_FLAG_HIDDEN);
}

bool ui_pairing_code_visible(void) {
  return !lv_obj_has_flag(s_pairing_overlay, LV_OBJ_FLAG_HIDDEN);
}
