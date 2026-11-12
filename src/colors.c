#define _GNU_SOURCE

#include "oshi.h"

static int colIsFg[COL_COUNT] = { 0, 1, 1, 1, 0, 0, 1, 1, 0, 1, 0 };

void editorColorSet(int role, int code) {
  snprintf(E.col_str[role], sizeof(E.col_str[role]),
           colIsFg[role] ? "\x1b[38;5;%dm" : "\x1b[48;5;%dm", code);
}

void editorColorSetRGB(int role, int r, int g, int b) {
  snprintf(E.col_str[role], sizeof(E.col_str[role]),
           colIsFg[role] ? "\x1b[38;2;%d;%d;%dm" : "\x1b[48;2;%d;%d;%dm",
           r, g, b);
}

void editorApplyDefaultColors(void) {
  editorColorSetRGB(COL_STATUS_BG, 0x22, 0x27, 0x33);
  editorColorSetRGB(COL_STATUS_FG, 0xEA, 0xEF, 0xF5);
  editorColorSetRGB(COL_GUTTER_FG, 0x98, 0xA0, 0xAE);
  editorColorSetRGB(COL_GUTTER_CURSOR_FG, 0xE3, 0x7B, 0x66);
  editorColorSetRGB(COL_GUTTER_BG, 0x10, 0x13, 0x18);
  editorColorSetRGB(COL_GUTTER_CURSOR_BG, 0x10, 0x13, 0x18);
  editorColorSetRGB(COL_TILDE_FG, 0x98, 0xA0, 0xAE);
  editorColorSetRGB(COL_TEXT_FG, 0xEA, 0xEF, 0xF5);
  editorColorSetRGB(COL_SCROLLMARK_BG, 0x98, 0xA0, 0xAE);
  editorColorSetRGB(COL_SCROLLMARK_FG, 0x10, 0x13, 0x18);
  editorColorSetRGB(COL_FIND, 0xE8, 0x68, 0x0C);
}

int editorParseColorRole(const char *name) {
  if (!strcmp(name, "status_bg")) return COL_STATUS_BG;
  if (!strcmp(name, "status_fg")) return COL_STATUS_FG;
  if (!strcmp(name, "gutter_fg")) return COL_GUTTER_FG;
  if (!strcmp(name, "gutter_cursor_fg")) return COL_GUTTER_CURSOR_FG;
  if (!strcmp(name, "gutter_bg")) return COL_GUTTER_BG;
  if (!strcmp(name, "gutter_cursor_bg")) return COL_GUTTER_CURSOR_BG;
  if (!strcmp(name, "tilde_fg")) return COL_TILDE_FG;
  if (!strcmp(name, "text")) return COL_TEXT_FG;
  if (!strcmp(name, "scrollmark_bg")) return COL_SCROLLMARK_BG;
  if (!strcmp(name, "scrollmark_fg")) return COL_SCROLLMARK_FG;
  if (!strcmp(name, "find")) return COL_FIND;
  return -1;
}
