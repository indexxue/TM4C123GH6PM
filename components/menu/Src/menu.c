#include "menu.h"

#include <stdio.h>

static void leave_page(menu_engine_t *eng, const menu_page_t *page)
{
  if (page != NULL && page->on_exit != NULL)
  {
    page->on_exit(eng->app_ctx, eng, page);
  }
}

static void enter_page(menu_engine_t *eng, const menu_page_t *page)
{
  if (page != NULL && page->on_enter != NULL)
  {
    page->on_enter(eng->app_ctx, eng, page);
  }
}

static uint16_t first_focus_index(const menu_page_t *page)
{
  if (page == NULL || page->items == NULL || page->count == 0U)
  {
    return 0U;
  }
  for (uint16_t i = 0U; i < page->count; i++)
  {
    if (menu_item_is_visible_focus(&page->items[i]))
    {
      return i;
    }
  }
  return 0U;
}

static uint16_t step_index(const menu_engine_t *eng, uint16_t from, int dir)
{
  const menu_page_t *page = eng->current;
  if (page == NULL || page->items == NULL || page->count == 0U)
  {
    return 0U;
  }

  uint16_t n = page->count;
  uint16_t idx = from;
  uint16_t guard = 0U;

  while (guard <= n)
  {
    if (eng->opts.wrap_around)
    {
      int32_t next = (int32_t)idx + dir;
      while (next < 0)
      {
        next += (int32_t)n;
      }
      while (next >= (int32_t)n)
      {
        next -= (int32_t)n;
      }
      idx = (uint16_t)next;
    }
    else
    {
      int32_t next = (int32_t)idx + dir;
      if (next < 0 || next >= (int32_t)n)
      {
        return from;
      }
      idx = (uint16_t)next;
    }

    if (menu_item_is_visible_focus(&page->items[idx]))
    {
      return idx;
    }
    guard++;
  }

  return from;
}

static bool item_can_lr_adjust(const menu_item_t *item)
{
  if (item == NULL)
  {
    return false;
  }
  if ((item->flags & MENU_ITEM_F_DISABLED) != 0U)
  {
    return false;
  }
  if (item->type != MENU_ITEM_PARAM || item->param == NULL)
  {
    return false;
  }
  return (item->param->get != NULL && item->param->set != NULL && item->param->step != NULL);
}

static menu_result_t adjust_param(menu_engine_t *eng, const menu_item_t *item, int dir)
{
  int32_t cur;
  int32_t next;

  if (eng == NULL || item == NULL || !item_can_lr_adjust(item))
  {
    return MENU_RESULT_IGNORED;
  }
  cur = item->param->get(eng->app_ctx, item);
  next = item->param->step(eng->app_ctx, item, cur, dir);
  item->param->set(eng->app_ctx, item, next);
  return MENU_RESULT_VALUE_CHANGED;
}

static menu_result_t pop_page(menu_engine_t *eng)
{
  if (eng == NULL || eng->depth == 0U)
  {
    return MENU_RESULT_ERROR;
  }

  leave_page(eng, eng->current);
  eng->depth--;
  eng->current = eng->stack[eng->depth].page;
  eng->index = eng->stack[eng->depth].index;
  enter_page(eng, eng->current);
  return MENU_RESULT_PAGE_OUT;
}

bool menu_item_is_visible_focus(const menu_item_t *item)
{
  if (item == NULL)
  {
    return false;
  }
  if ((item->flags & MENU_ITEM_F_HIDDEN) != 0U)
  {
    return false;
  }
  if ((item->flags & MENU_ITEM_F_SEPARATOR) != 0U)
  {
    return false;
  }
  if ((item->flags & MENU_ITEM_F_NO_FOCUS) != 0U)
  {
    return false;
  }
  return true;
}

bool menu_item_is_actionable(const menu_item_t *item)
{
  if (!menu_item_is_visible_focus(item))
  {
    return false;
  }
  if ((item->flags & MENU_ITEM_F_DISABLED) != 0U)
  {
    return false;
  }
  return true;
}

bool menu_item_is_danger(const menu_item_t *item)
{
  if (item == NULL)
  {
    return false;
  }
  return ((item->flags & MENU_ITEM_F_DANGER) != 0U);
}

bool menu_item_is_draw_visible(const menu_item_t *item)
{
  if (item == NULL)
  {
    return false;
  }
  return ((item->flags & MENU_ITEM_F_HIDDEN) == 0U);
}

const char *menu_item_aux_text(void *app_ctx,
                               const menu_item_t *item,
                               char *buf,
                               size_t buflen)
{
  if (item == NULL || buf == NULL || buflen == 0U)
  {
    return "";
  }
  buf[0] = '\0';

  if (item->aux != NULL)
  {
    const char *s = item->aux(app_ctx, item, buf, buflen);
    if (s != NULL && s[0] != '\0')
    {
      return s;
    }
  }

  if (item->type == MENU_ITEM_PARAM && item_can_lr_adjust(item))
  {
    int32_t v = item->param->get(app_ctx, item);
    if (item->param->format != NULL)
    {
      const char *s = item->param->format(app_ctx, item, v, buf, buflen);
      if (s != NULL)
      {
        return s;
      }
    }
    (void)snprintf(buf, buflen, "%ld", (long)v);
    return buf;
  }

  return "";
}

const char *menu_page_foot_hint(const menu_page_t *page)
{
  if (page == NULL || page->foot_hint == NULL)
  {
    return "";
  }
  return page->foot_hint;
}

typedef bool (*menu_item_pred_fn)(const menu_item_t *item);

static uint16_t menu_page_count_matching(const menu_page_t *page, menu_item_pred_fn pred)
{
  uint16_t n;

  if (page == NULL || page->items == NULL || pred == NULL)
  {
    return 0U;
  }
  n = 0U;
  for (uint16_t i = 0U; i < page->count; i++)
  {
    if (pred(&page->items[i]))
    {
      n++;
    }
  }
  return n;
}

static bool menu_page_slot_at(const menu_page_t *page,
                              uint16_t slot,
                              menu_item_pred_fn pred,
                              uint16_t *out_item_index)
{
  uint16_t v;

  if (page == NULL || page->items == NULL || pred == NULL || out_item_index == NULL)
  {
    return false;
  }
  v = 0U;
  for (uint16_t i = 0U; i < page->count; i++)
  {
    if (!pred(&page->items[i]))
    {
      continue;
    }
    if (v == slot)
    {
      *out_item_index = i;
      return true;
    }
    v++;
  }
  return false;
}

static bool menu_page_slot_for_index(const menu_page_t *page,
                                    uint16_t item_index,
                                    menu_item_pred_fn pred,
                                    uint16_t *out_slot)
{
  uint16_t v;

  if (page == NULL || page->items == NULL || pred == NULL || out_slot == NULL)
  {
    return false;
  }
  if (item_index >= page->count)
  {
    return false;
  }
  if (!pred(&page->items[item_index]))
  {
    return false;
  }
  v = 0U;
  for (uint16_t i = 0U; i < page->count; i++)
  {
    if (!pred(&page->items[i]))
    {
      continue;
    }
    if (i == item_index)
    {
      *out_slot = v;
      return true;
    }
    v++;
  }
  return false;
}

static bool menu_page_slot_offset(const menu_page_t *page,
                                  uint16_t cur_slot,
                                  int rel,
                                  uint8_t wrap_ring,
                                  menu_item_pred_fn pred,
                                  uint16_t *out_slot)
{
  uint16_t n;
  int32_t base;

  if (page == NULL || pred == NULL || out_slot == NULL)
  {
    return false;
  }
  if (rel < -1 || rel > 1)
  {
    return false;
  }
  n = menu_page_count_matching(page, pred);
  if (n == 0U || cur_slot >= n)
  {
    return false;
  }
  if (rel == 0)
  {
    *out_slot = cur_slot;
    return true;
  }
  if (n == 1U)
  {
    return false;
  }
  if (wrap_ring != 0U)
  {
    base = (int32_t)cur_slot + rel;
    while (base < 0)
    {
      base += (int32_t)n;
    }
    while (base >= (int32_t)n)
    {
      base -= (int32_t)n;
    }
    *out_slot = (uint16_t)base;
    return true;
  }
  if (rel < 0)
  {
    if (cur_slot == 0U)
    {
      return false;
    }
    *out_slot = (uint16_t)(cur_slot - 1U);
    return true;
  }
  if (cur_slot >= n - 1U)
  {
    return false;
  }
  *out_slot = (uint16_t)(cur_slot + 1U);
  return true;
}

uint16_t menu_page_visible_item_count(const menu_page_t *page)
{
  return menu_page_count_matching(page, menu_item_is_draw_visible);
}

bool menu_page_visible_item_at(const menu_page_t *page, uint16_t vis_pos, uint16_t *out_item_index)
{
  return menu_page_slot_at(page, vis_pos, menu_item_is_draw_visible, out_item_index);
}

bool menu_page_visible_cursor_pos(const menu_page_t *page, uint16_t item_index, uint16_t *out_vis_pos)
{
  return menu_page_slot_for_index(page, item_index, menu_item_is_draw_visible, out_vis_pos);
}

bool menu_page_visible_vis_offset(const menu_page_t *page,
                                  uint16_t cur_vis_pos,
                                  int rel,
                                  uint8_t wrap_ring,
                                  uint16_t *out_vis_pos)
{
  return menu_page_slot_offset(page, cur_vis_pos, rel, wrap_ring, menu_item_is_draw_visible, out_vis_pos);
}

uint16_t menu_page_focus_item_count(const menu_page_t *page)
{
  return menu_page_count_matching(page, menu_item_is_visible_focus);
}

bool menu_page_focus_item_at(const menu_page_t *page, uint16_t focus_pos, uint16_t *out_item_index)
{
  return menu_page_slot_at(page, focus_pos, menu_item_is_visible_focus, out_item_index);
}

bool menu_page_focus_cursor_pos(const menu_page_t *page, uint16_t item_index, uint16_t *out_focus_pos)
{
  return menu_page_slot_for_index(page, item_index, menu_item_is_visible_focus, out_focus_pos);
}

bool menu_page_focus_vis_offset(const menu_page_t *page,
                                uint16_t cur_focus_pos,
                                int rel,
                                uint8_t wrap_ring,
                                uint16_t *out_focus_pos)
{
  return menu_page_slot_offset(page, cur_focus_pos, rel, wrap_ring, menu_item_is_visible_focus, out_focus_pos);
}

bool menu_page_viewport_first_focus(const menu_page_t *page,
                                    uint16_t item_index,
                                    uint16_t visible_rows,
                                    uint16_t *out_first_focus_slot)
{
  uint16_t focus_pos;
  uint16_t total;
  uint16_t first;

  if (page == NULL || out_first_focus_slot == NULL || visible_rows == 0U)
  {
    return false;
  }
  if (!menu_page_focus_cursor_pos(page, item_index, &focus_pos))
  {
    return false;
  }

  total = menu_page_focus_item_count(page);
  if (total == 0U)
  {
    return false;
  }
  if (visible_rows >= total)
  {
    *out_first_focus_slot = 0U;
    return true;
  }

  first = 0U;
  if (focus_pos >= visible_rows)
  {
    first = (uint16_t)(focus_pos - visible_rows + 1U);
  }
  if (first + visible_rows > total)
  {
    first = (uint16_t)(total - visible_rows);
  }

  *out_first_focus_slot = first;
  return true;
}

static bool item_can_enter(const menu_item_t *item)
{
  if (item == NULL)
  {
    return false;
  }
  if ((item->flags & MENU_ITEM_F_DISABLED) != 0U)
  {
    return false;
  }
  if (item->type == MENU_ITEM_SUBMENU)
  {
    return item->submenu != NULL;
  }
  if (item->type == MENU_ITEM_ACTION)
  {
    return item->on_select != NULL;
  }
  return false;
}

void menu_engine_init(menu_engine_t *eng, const menu_page_t *root, void *app_ctx)
{
  if (eng == NULL)
  {
    return;
  }
  eng->app_ctx = app_ctx;
  eng->opts.wrap_around = 1U;
  eng->opts.param_on_vertical = 0U;
  eng->root = root;
  eng->current = root;
  eng->index = first_focus_index(root);
  eng->depth = 0U;
  enter_page(eng, root);
}

void menu_engine_configure(menu_engine_t *eng, const menu_engine_opts_t *opts)
{
  if (eng == NULL || opts == NULL)
  {
    return;
  }
  eng->opts = *opts;
}

void menu_engine_reset(menu_engine_t *eng)
{
  if (eng == NULL || eng->root == NULL)
  {
    return;
  }

  while (eng->depth > 0U)
  {
    leave_page(eng, eng->current);
    eng->depth--;
    eng->current = eng->stack[eng->depth].page;
    eng->index = eng->stack[eng->depth].index;
  }

  leave_page(eng, eng->current);
  eng->current = eng->root;
  eng->index = first_focus_index(eng->root);
  enter_page(eng, eng->current);
}

menu_result_t menu_nav_back(menu_engine_t *eng)
{
  const menu_page_t *page;

  if (eng == NULL || eng->root == NULL || eng->current == NULL)
  {
    return MENU_RESULT_ERROR;
  }

  if (eng->depth == 0U)
  {
    page = eng->current;
    if (page->on_root_back != NULL)
    {
      page->on_root_back(eng->app_ctx, eng, page);
      return MENU_RESULT_EXIT;
    }
    return MENU_RESULT_IGNORED;
  }

  return pop_page(eng);
}

menu_result_t menu_nav_goto(menu_engine_t *eng, const menu_page_t *target)
{
  const menu_page_t *root;
  uint16_t i;

  if (eng == NULL || target == NULL || eng->root == NULL)
  {
    return MENU_RESULT_ERROR;
  }

  menu_engine_reset(eng);

  if (target == eng->current)
  {
    return MENU_RESULT_NONE;
  }

  root = eng->root;
  if (root->items == NULL)
  {
    return MENU_RESULT_ERROR;
  }

  for (i = 0U; i < root->count; i++)
  {
    const menu_item_t *item = &root->items[i];
    if (item->type == MENU_ITEM_SUBMENU && item->submenu == target)
    {
      eng->index = i;
      return menu_dispatch(eng, MENU_EVT_ENTER);
    }
  }

  return MENU_RESULT_ERROR;
}

const menu_page_t *menu_current_page(const menu_engine_t *eng)
{
  if (eng == NULL)
  {
    return NULL;
  }
  return eng->current;
}

uint16_t menu_current_index(const menu_engine_t *eng)
{
  if (eng == NULL)
  {
    return 0U;
  }
  return eng->index;
}

const menu_item_t *menu_current_item(const menu_engine_t *eng)
{
  const menu_page_t *p;
  if (eng == NULL)
  {
    return NULL;
  }
  p = eng->current;
  if (p == NULL || p->items == NULL || eng->index >= p->count)
  {
    return NULL;
  }
  return &p->items[eng->index];
}

uint8_t menu_engine_depth(const menu_engine_t *eng)
{
  if (eng == NULL)
  {
    return 0U;
  }
  return eng->depth;
}

menu_result_t menu_dispatch(menu_engine_t *eng, menu_evt_t evt)
{
  const menu_item_t *item;
  uint16_t next;

  if (eng == NULL || eng->root == NULL || eng->current == NULL)
  {
    return MENU_RESULT_ERROR;
  }

  if (eng->current->items == NULL || eng->current->count == 0U)
  {
    return MENU_RESULT_ERROR;
  }

  switch (evt)
  {
  case MENU_EVT_UP:
  case MENU_EVT_DOWN:
    item = menu_current_item(eng);
    if (eng->opts.param_on_vertical != 0U && item != NULL && item_can_lr_adjust(item))
    {
      int dir = (evt == MENU_EVT_UP) ? -1 : 1;
      return adjust_param(eng, item, dir);
    }
    next = step_index(eng, eng->index, (evt == MENU_EVT_UP) ? -1 : 1);
    if (next == eng->index)
    {
      return eng->opts.wrap_around ? MENU_RESULT_IGNORED : MENU_RESULT_AT_BOUND;
    }
    eng->index = next;
    return MENU_RESULT_SELECTION_CHANGED;

  case MENU_EVT_LEFT:
  case MENU_EVT_RIGHT:
    item = menu_current_item(eng);
    if (item == NULL)
    {
      return MENU_RESULT_ERROR;
    }
    if (!item_can_lr_adjust(item))
    {
      return MENU_RESULT_IGNORED;
    }
    return adjust_param(eng, item, (evt == MENU_EVT_LEFT) ? -1 : 1);

  case MENU_EVT_ENTER:
    item = menu_current_item(eng);
    if (item == NULL)
    {
      return MENU_RESULT_ERROR;
    }
    if (item->type == MENU_ITEM_PARAM && (item->flags & MENU_ITEM_F_ENTER_NEXT) != 0U &&
        menu_item_is_visible_focus(item))
    {
      next = step_index(eng, eng->index, 1);
      if (next == eng->index)
      {
        return MENU_RESULT_AT_BOUND;
      }
      eng->index = next;
      return MENU_RESULT_SELECTION_CHANGED;
    }
    if (!item_can_enter(item))
    {
      return MENU_RESULT_IGNORED;
    }
    if (item->type == MENU_ITEM_SUBMENU && item->submenu != NULL)
    {
      if (eng->depth >= MENU_STACK_MAX)
      {
        return MENU_RESULT_ERROR;
      }
      eng->stack[eng->depth].page = eng->current;
      eng->stack[eng->depth].index = eng->index;
      eng->depth++;

      leave_page(eng, eng->current);
      eng->current = item->submenu;
      eng->index = first_focus_index(item->submenu);
      enter_page(eng, eng->current);
      return MENU_RESULT_PAGE_IN;
    }
    if (item->type == MENU_ITEM_ACTION && item->on_select != NULL)
    {
      item->on_select(eng->app_ctx, eng, item);
      return MENU_RESULT_ACTION;
    }
    return MENU_RESULT_IGNORED;

  case MENU_EVT_BACK:
    return menu_nav_back(eng);

  case MENU_EVT_HOME:
    menu_engine_reset(eng);
    return MENU_RESULT_HOME;

  default:
    return MENU_RESULT_IGNORED;
  }
}
