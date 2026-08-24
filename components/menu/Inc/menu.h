#ifndef MENU_H
#define MENU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef MENU_STACK_MAX
#define MENU_STACK_MAX 8
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct menu_engine menu_engine_t;
typedef struct menu_page menu_page_t;
typedef struct menu_item menu_item_t;

typedef enum {
  MENU_EVT_UP = 0,
  MENU_EVT_DOWN,
  MENU_EVT_LEFT,
  MENU_EVT_RIGHT,
  MENU_EVT_ENTER,
  MENU_EVT_BACK,
  MENU_EVT_HOME,
} menu_evt_t;

typedef enum {
  MENU_ITEM_ACTION = 0,
  MENU_ITEM_SUBMENU,
  /** Static text; can receive focus for navigation; MENU_EVT_ENTER is ignored (not actionable). */
  MENU_ITEM_LABEL,
  MENU_ITEM_PARAM,
} menu_item_type_t;

typedef enum {
  MENU_ITEM_F_NONE = 0,
  MENU_ITEM_F_HIDDEN = 1u << 0,
  MENU_ITEM_F_DISABLED = 1u << 1,
  MENU_ITEM_F_SEPARATOR = 1u << 2,
  MENU_ITEM_F_NO_FOCUS = 1u << 3,
  /** View hint: danger styling (e.g. reset confirm). */
  MENU_ITEM_F_DANGER = 1u << 4,
  /**
   * On MENU_ITEM_PARAM: MENU_EVT_ENTER moves focus to the next focusable row
   * (e.g. param row then save button) instead of being ignored.
   */
  MENU_ITEM_F_ENTER_NEXT = 1u << 5,
} menu_item_flags_t;

typedef void (*menu_action_cb)(void *app_ctx, menu_engine_t *eng, const menu_item_t *item);

typedef void (*menu_page_cb)(void *app_ctx, menu_engine_t *eng, const menu_page_t *page);

typedef int32_t (*menu_param_get_cb)(void *app_ctx, const menu_item_t *item);
typedef void (*menu_param_set_cb)(void *app_ctx, const menu_item_t *item, int32_t value);
typedef int32_t (*menu_param_step_cb)(void *app_ctx, const menu_item_t *item, int32_t cur, int dir);
typedef const char *(*menu_param_format_cb)(void *app_ctx,
                                           const menu_item_t *item,
                                           int32_t value,
                                           char *buf,
                                           size_t buflen);

/**
 * Optional secondary label (e.g. right-aligned "08:00-16:00" on a submenu row).
 * Return static or @a buf text; NULL if none.
 */
typedef const char *(*menu_item_aux_cb)(void *app_ctx,
                                        const menu_item_t *item,
                                        char *buf,
                                        size_t buflen);

typedef struct {
  menu_param_get_cb get;
  menu_param_set_cb set;
  menu_param_step_cb step;
  menu_param_format_cb format; /* optional; if NULL, value is printed as integer */
} menu_param_vtbl_t;

struct menu_item {
  const char *label;
  menu_item_type_t type;
  uint16_t flags;
  const menu_page_t *submenu;
  menu_action_cb on_select;
  const menu_param_vtbl_t *param;
  menu_item_aux_cb aux;
  void *user_ctx;
};

struct menu_page {
  const char *title;
  const menu_item_t *items;
  uint16_t count;
  void *user_ctx;
  /** Optional LCD foot hint (e.g. "选择 · 确认 · 返回"). */
  const char *foot_hint;
  menu_page_cb on_enter;
  menu_page_cb on_exit;
  /**
   * MENU_EVT_BACK at stack depth 0: invoked before @ref MENU_RESULT_EXIT.
   * Use to leave menu mode (e.g. return to SCR_HOME). If NULL, BACK is ignored at root.
   */
  menu_page_cb on_root_back;
};

typedef enum {
  MENU_RESULT_NONE = 0,
  MENU_RESULT_SELECTION_CHANGED,
  MENU_RESULT_PAGE_IN,
  MENU_RESULT_PAGE_OUT,
  MENU_RESULT_ACTION,
  MENU_RESULT_VALUE_CHANGED,
  MENU_RESULT_HOME,
  /** Root BACK with @ref menu_page_t.on_root_back set. */
  MENU_RESULT_EXIT,
  MENU_RESULT_IGNORED,
  MENU_RESULT_ERROR,
  MENU_RESULT_AT_BOUND,
} menu_result_t;

typedef struct {
  uint8_t wrap_around;
  /**
   * Non-zero: on a focused MENU_ITEM_PARAM, MENU_EVT_UP/DOWN adjust the value
   * (same as LEFT/RIGHT) instead of moving list focus. Fits NAV_PREV/NEXT adapters.
   */
  uint8_t param_on_vertical;
} menu_engine_opts_t;

struct menu_engine {
  void *app_ctx;
  menu_engine_opts_t opts;
  const menu_page_t *root;
  const menu_page_t *current;
  uint16_t index;
  struct {
    const menu_page_t *page;
    uint16_t index;
  } stack[MENU_STACK_MAX];
  uint8_t depth;
};

void menu_engine_init(menu_engine_t *eng, const menu_page_t *root, void *app_ctx);
void menu_engine_configure(menu_engine_t *eng, const menu_engine_opts_t *opts);

void menu_engine_reset(menu_engine_t *eng);

menu_result_t menu_dispatch(menu_engine_t *eng, menu_evt_t evt);

/** Pop one page; same as @ref MENU_EVT_BACK when depth > 0. */
menu_result_t menu_nav_back(menu_engine_t *eng);

/**
 * Reset to root, then enter @a target if it is a direct SUBMENU of root.
 * @a target may equal root (admin root only).
 */
menu_result_t menu_nav_goto(menu_engine_t *eng, const menu_page_t *target);

const menu_page_t *menu_current_page(const menu_engine_t *eng);
uint16_t menu_current_index(const menu_engine_t *eng);
const menu_item_t *menu_current_item(const menu_engine_t *eng);
uint8_t menu_engine_depth(const menu_engine_t *eng);

bool menu_item_is_visible_focus(const menu_item_t *item);
bool menu_item_is_actionable(const menu_item_t *item);
bool menu_item_is_danger(const menu_item_t *item);

/**
 * Draw-order predicate: item occupies a list row (not MENU_ITEM_F_HIDDEN).
 * Use with @ref menu_page_visible_* when the UI must show separators / no-focus rows.
 */
bool menu_item_is_draw_visible(const menu_item_t *item);

/**
 * Secondary label for list rows: @a item->aux, or formatted PARAM value when focused row is PARAM.
 * Returns empty string when none.
 */
const char *menu_item_aux_text(void *app_ctx,
                               const menu_item_t *item,
                               char *buf,
                               size_t buflen);

/** @ref menu_page_t.foot_hint or empty string. */
const char *menu_page_foot_hint(const menu_page_t *page);

/** Count of draw-visible rows on @a page (same order as @ref menu_page_visible_item_at). */
uint16_t menu_page_visible_item_count(const menu_page_t *page);

/** Map draw-order slot (0 .. count-1) to absolute item index. */
bool menu_page_visible_item_at(const menu_page_t *page, uint16_t vis_pos, uint16_t *out_item_index);

/** Draw-order slot of @a item_index; false if not draw-visible. */
bool menu_page_visible_cursor_pos(const menu_page_t *page, uint16_t item_index, uint16_t *out_vis_pos);

/**
 * Draw-order slot offset (same ordering as @ref menu_page_visible_item_at).
 * @param rel -1 / 0 / +1; @a wrap_ring non-zero enables ring at ends.
 */
bool menu_page_visible_vis_offset(const menu_page_t *page,
                                  uint16_t cur_vis_pos,
                                  int rel,
                                  uint8_t wrap_ring,
                                  uint16_t *out_vis_pos);

/** Count of focusable rows (same rules as @ref menu_dispatch up/down stepping). */
uint16_t menu_page_focus_item_count(const menu_page_t *page);

/** Map focus-order slot to absolute item index. */
bool menu_page_focus_item_at(const menu_page_t *page, uint16_t focus_pos, uint16_t *out_item_index);

/** Focus-order slot of @a item_index; false if that index cannot hold focus. */
bool menu_page_focus_cursor_pos(const menu_page_t *page, uint16_t item_index, uint16_t *out_focus_pos);

/**
 * Focus-order slot offset (aligns with @ref menu_page_focus_item_at). Prefer this for list UIs that
 * should match keyboard focus, including ring previews when @a wrap_ring is set.
 */
bool menu_page_focus_vis_offset(const menu_page_t *page,
                                uint16_t cur_focus_pos,
                                int rel,
                                uint8_t wrap_ring,
                                uint16_t *out_focus_pos);

/**
 * First focus-order slot for a viewport of @a visible_rows rows that keeps @a item_index visible.
 * Use for 240×135 list windows (e.g. 4 visible rows).
 */
bool menu_page_viewport_first_focus(const menu_page_t *page,
                                    uint16_t item_index,
                                    uint16_t visible_rows,
                                    uint16_t *out_first_focus_slot);

#ifdef __cplusplus
}
#endif

#endif
