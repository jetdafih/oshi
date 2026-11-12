#define _GNU_SOURCE

#include "oshi.h"

void *xmalloc(size_t n) {
  void *p = malloc(n ? n : 1);
  if (p == NULL) die("malloc");
  return p;
}

void *xrealloc(void *p, size_t n) {
  void *np = realloc(p, n ? n : 1);
  if (np == NULL) die("realloc");
  return np;
}

void editorSkipPaste(void) {
  int n;
  free(editorReadPasteBuf(&n));
}

static size_t editorFindPasteEnd(const char *buf, size_t n, size_t from) {
  static const char endseq[] = "\x1b[201~";
  const size_t elen = sizeof(endseq) - 1;
  size_t i;
  if (n < elen) return n;
  for (i = from; i + elen <= n; i++)
    if (memcmp(buf + i, endseq, elen) == 0) return i;
  return n;
}

static void editorDrainPaste(const char *tail, size_t n) {
  static const char endseq[] = "\x1b[201~";
  const size_t elen = sizeof(endseq) - 1;
  char win[8192 + 8];
  size_t have = n < elen - 1 ? n : elen - 1;
  int idle = 0;
  memcpy(win, tail + n - have, have);
  for (;;) {
    ssize_t r = read(STDIN_FILENO, win + have, 8192);
    if (r == 0) { if (++idle > 100) return; continue; }
    if (r == -1) {
      if (errno == EINTR && !got_term) continue;
      return;
    }
    idle = 0;
    size_t total = have + (size_t)r;
    if (editorFindPasteEnd(win, total, 0) < total) return;
    have = total < elen - 1 ? total : elen - 1;
    memmove(win, win + total - have, have);
  }
}

char *editorReadPasteBuf(int *len) {
  static const char endseq[] = "\x1b[201~";
  const size_t elen = sizeof(endseq) - 1;
  const size_t step = 8192;
  size_t cap = step, n = 0;
  char *buf = xmalloc(cap);
  int idle = 0;

  for (;;) {
    if (n + step + 1 > cap) {
      if (cap >= OSHI_PASTE_MAX) { editorDrainPaste(buf, n); break; }
      size_t newcap = cap * 2;
      if (newcap > OSHI_PASTE_MAX) newcap = OSHI_PASTE_MAX;
      if (newcap <= cap) break;
      buf = xrealloc(buf, newcap);
      cap = newcap;
    }
    size_t want = cap - n;
    if (want > step) want = step;
    if (want == 0) break;
    ssize_t r = read(STDIN_FILENO, buf + n, want);
    if (r == 0) {
      if (++idle > 100) break;
      continue;
    }
    if (r == -1) {
      if (errno == EINTR && !got_term) continue;
      break;
    }
    idle = 0;
    size_t scan_from = n >= elen - 1 ? n - (elen - 1) : 0;
    n += (size_t)r;
    size_t hit = editorFindPasteEnd(buf, n, scan_from);
    if (hit < n) {
      n = hit;
      break;
    }
  }
  *len = (int)n;
  return buf;
}
