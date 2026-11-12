#define _GNU_SOURCE

#include "oshi.h"

int utf8Decode(const unsigned char *s, int n, int *len) {
  unsigned char c = s[0];
  int need, cp, i;
  if (c < 0x80) { *len = 1; return c; }
  if (c >= 0xC2 && c <= 0xDF) { need = 1; cp = c & 0x1F; }
  else if (c >= 0xE0 && c <= 0xEF) { need = 2; cp = c & 0x0F; }
  else if (c >= 0xF0 && c <= 0xF4) { need = 3; cp = c & 0x07; }
  else { *len = 1; return 0xFFFD; }
  if (n < need + 1) { *len = 1; return 0xFFFD; }
  for (i = 1; i <= need; i++) {
    if ((s[i] & 0xC0) != 0x80) { *len = 1; return 0xFFFD; }
    cp = (cp << 6) | (s[i] & 0x3F);
  }
  if ((need == 2 && cp < 0x800) || (need == 3 && cp < 0x10000) ||
      cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
    *len = 1;
    return 0xFFFD;
  }
  *len = need + 1;
  return cp;
}

int cpWidth(int cp) {
  static const struct { int lo, hi; } zero[] = {
    {0x0300,0x036F},{0x0483,0x0489},{0x0591,0x05BD},{0x05BF,0x05BF},
    {0x05C1,0x05C2},{0x05C4,0x05C5},{0x05C7,0x05C7},{0x0610,0x061A},
    {0x064B,0x065F},{0x0670,0x0670},{0x06D6,0x06DC},{0x06DF,0x06E4},
    {0x06E7,0x06E8},{0x06EA,0x06ED},{0x0E31,0x0E31},{0x0E34,0x0E3A},
    {0x0E47,0x0E4E},{0x1AB0,0x1AFF},{0x1DC0,0x1DFF},{0x200B,0x200F},
    {0x202A,0x202E},{0x2060,0x2064},{0x20D0,0x20FF},{0xFE00,0xFE0F},
    {0xFE20,0xFE2F},{0xFEFF,0xFEFF},{0x1F3FB,0x1F3FF},{0xE0100,0xE01EF},
    {0x0900,0x0902},{0x093A,0x093A},{0x093C,0x093C},{0x0941,0x0948},
    {0x094D,0x094D},{0x0951,0x0957},{0x0962,0x0963}
  };
  static const struct { int lo, hi; } wide[] = {
    {0x1100,0x115F},{0x2E80,0x303E},{0x3041,0x33FF},{0x3400,0x4DBF},
    {0x4E00,0x9FFF},{0xA000,0xA4CF},{0xA960,0xA97F},{0xAC00,0xD7A3},
    {0xF900,0xFAFF},{0xFE10,0xFE19},{0xFE30,0xFE6F},{0xFF00,0xFF60},
    {0xFFE0,0xFFE6},{0x1F300,0x1F64F},{0x1F680,0x1F6FF},{0x1F900,0x1F9FF},
    {0x1FA70,0x1FAFF},{0x20000,0x3FFFD},
    {0x231A,0x231B},{0x23E9,0x23EC},{0x23F0,0x23F0},{0x23F3,0x23F3},
    {0x25FD,0x25FE},{0x2614,0x2615},{0x2648,0x2653},{0x267F,0x267F},
    {0x2693,0x2693},{0x26A1,0x26A1},{0x26AA,0x26AB},{0x26BD,0x26BE},
    {0x26C4,0x26C5},{0x26CE,0x26CE},{0x26D4,0x26D4},{0x26EA,0x26EA},
    {0x26F2,0x26F3},{0x26F5,0x26F5},{0x26FA,0x26FA},{0x26FD,0x26FD},
    {0x2705,0x2705},{0x270A,0x270B},{0x2728,0x2728},{0x274C,0x274C},
    {0x274E,0x274E},{0x2753,0x2755},{0x2757,0x2757},{0x2795,0x2797},
    {0x27B0,0x27B0},{0x27BF,0x27BF},{0x2B1B,0x2B1C},{0x2B50,0x2B50},
    {0x2B55,0x2B55},{0x1F004,0x1F004},{0x1F0CF,0x1F0CF},{0x1F18E,0x1F18E},
    {0x1F191,0x1F19A},{0x1F200,0x1F2FF}
  };
  size_t i;
  if (cp < 0x300) return 1;
  for (i = 0; i < sizeof(zero) / sizeof(zero[0]); i++)
    if (cp >= zero[i].lo && cp <= zero[i].hi) return 0;
  for (i = 0; i < sizeof(wide) / sizeof(wide[0]); i++)
    if (cp >= wide[i].lo && cp <= wide[i].hi) return 2;
  return 1;
}

int editorCharEnd(const erow *row, int b) {
  int len, cp, zwj;
  if (b >= row->size) return row->size;
  cp = utf8Decode((const unsigned char *)row->chars + b, row->size - b, &len);
  b += len;
  zwj = cp == 0x200D;
  while (b < row->size) {
    int l2, c2 = utf8Decode((const unsigned char *)row->chars + b,
                            row->size - b, &l2);
    if (!zwj && cpWidth(c2) != 0) break;
    zwj = c2 == 0x200D;
    b += l2;
  }
  return b;
}

int editorPrevChar(const erow *row, int b) {
  int s = 0, e;
  if (b > row->size) b = row->size;
  while (s < b) {
    e = editorCharEnd(row, s);
    if (e >= b) return s;
    s = e;
  }
  return b > 0 ? b - 1 : 0;
}

int editorCellWidth(const erow *row, int b, int col) {
  int len, cp, w;
  if (row->chars[b] == '\t') return OSHI_TAB_STOP - (col % OSHI_TAB_STOP);
  cp = utf8Decode((const unsigned char *)row->chars + b, row->size - b, &len);
  w = cpWidth(cp);
  return w > 0 ? w : 1;
}

int editorStrWidth(const char *s, int n) {
  int w = 0, i = 0;
  while (i < n) {
    int l, cp = utf8Decode((const unsigned char *)s + i, n - i, &l);
    w += cpWidth(cp);
    i += l;
  }
  return w;
}

int editorFitHead(const char *s, int n, int maxcols) {
  int w = 0, i = 0;
  while (i < n) {
    int l, cp = utf8Decode((const unsigned char *)s + i, n - i, &l);
    int cw = cpWidth(cp);
    if (w + cw > maxcols) break;
    w += cw;
    i += l;
  }
  return i;
}

int editorFitTail(const char *s, int n, int maxcols) {
  int total = editorStrWidth(s, n), i = 0;
  while (i < n && total > maxcols) {
    int l, cp = utf8Decode((const unsigned char *)s + i, n - i, &l);
    total -= cpWidth(cp);
    i += l;
  }
  return i;
}

int editorCharCount(const char *s, int len) {
  int n = 0, i;
  for (i = 0; i < len; i++)
    if ((unsigned char)(s[i] & 0xC0) != 0x80) n++;
  return n;
}

int editorLineCount(const char *s, int len) {
  int n = 0, i;
  for (i = 0; i < len; i++)
    if (s[i] == '\n') n++;
  if (len > 0 && s[len - 1] != '\n') n++;
  return n;
}
