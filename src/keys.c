#define _GNU_SOURCE

#include "oshi.h"

static int editorReadEscape(void) {
  char seq[32];
  int len = 1;

  if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
  if (seq[0] == '\x1b') return ESC_AGAIN;
  if (seq[0] != '[' && seq[0] != 'O')
    return ALT_KEY((unsigned char)seq[0]);
  while (len < (int)sizeof(seq) - 1) {
    if (read(STDIN_FILENO, &seq[len], 1) != 1) break;
    len++;
    if (seq[len - 1] >= 0x40 && seq[len - 1] <= 0x7e) break;
  }
  if (len == 1) return ALT_KEY((unsigned char)seq[0]);
  seq[len] = '\0';

  char final = seq[len - 1];
  if (seq[0] == '[' && final == 'M' && len == 2) {
    unsigned char m[3];
    int got = 0, idle = 0;
    while (got < 3) {
      ssize_t r = read(STDIN_FILENO, m + got, sizeof(m) - (size_t)got);
      if (r > 0) {
        got += (int)r;
        idle = 0;
        continue;
      }
      if (r == 0 && ++idle <= 10) continue;
      if (r == -1 && errno == EINTR && !got_term) continue;
      break;
    }
    return NOKEY;
  }
  if (len > 2 && seq[0] == '[' && seq[1] == '<' &&
      sscanf(&seq[2], "%d;%d;%d", &E.mouse_button, &E.mouse_x, &E.mouse_y) == 3) {
    E.mouse_release = final == 'm';
    return MOUSE_KEY;
  }
  switch (final) {
  case 'A': case 'B': case 'C': case 'D': {
    int m = (len > 4 && seq[1] == '1' && seq[2] == ';') ? atoi(&seq[3]) - 1 : 0;
    int shift = m & 1, ctrl = m & 4;
    if (final == 'A') return shift ? SHIFT_UP : ARROW_UP;
    if (final == 'B') return shift ? SHIFT_DOWN : ARROW_DOWN;
    if (ctrl) return final == 'C'
                    ? (shift ? CSHIFT_RIGHT : CTRL_RIGHT)
                    : (shift ? CSHIFT_LEFT : CTRL_LEFT);
    if (shift) return final == 'C' ? SHIFT_RIGHT : SHIFT_LEFT;
    return final == 'C' ? ARROW_RIGHT : ARROW_LEFT;
  }
  case 'H': return HOME_KEY;
  case 'F': return END_KEY;
  case '~':
    switch (atoi(&seq[1])) {
    case 1: case 7: return HOME_KEY;
    case 3: return DEL_KEY;
    case 4: case 8: return END_KEY;
    case 5: return PAGE_UP;
    case 6: return PAGE_DOWN;
    case 200: return PASTE_KEY;
    }
    break;
  }
  return NOKEY;
}

int editorReadKey(void) {
  int nread;
  char c;
  static int instant_reads;
  for (;;) {
    if (got_term) {
      editorTermReset();
      exit(128 + (int)got_term);
    }
    if (got_winch) {
      got_winch = 0;
      if (editorResize()) return RESIZE_KEY;
    }
    long t0 = editorNowMs();
    nread = read(STDIN_FILENO, &c, 1);
    if (nread == 1) {
      instant_reads = 0;
      break;
    }
    if (nread == 0 && editorNowMs() - t0 < 20) {
      if (++instant_reads > 200) {
        editorTermReset();
        exit(1);
      }
    } else {
      instant_reads = 0;
    }
    if (nread == -1 && errno != EAGAIN && errno != EINTR) die("read");
    if (E.notice_pending && editorNowMs() >= E.notice_deadline) {
      E.notice_pending = 0;
      return NOKEY;
    }
  }
  if (c == '\x1b') {
    int key = editorReadEscape();
    while (key == ESC_AGAIN) key = editorReadEscape();
    return key;
  }
  unsigned char u = (unsigned char)c;
  if (u == 0x08) return CTRL_BACKSPACE;
  if (u < 0x80) return u;
  if (u >= 0xC2 && u <= 0xF4) {
    int need = u >= 0xF0 ? 3 : u >= 0xE0 ? 2 : 1;
    int total = need + 1;
    E.utf8[0] = c;
    E.utf8len = 1;
    while (need-- > 0) {
      char cc;
      if (read(STDIN_FILENO, &cc, 1) != 1) break;
      if (((unsigned char)cc & 0xC0) != 0x80) return NOKEY;
      E.utf8[E.utf8len++] = cc;
    }
    return E.utf8len == total ? UTF8_KEY : NOKEY;
  }
  return NOKEY;
}

static int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

  return 0;
}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}
