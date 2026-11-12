#define _GNU_SOURCE

#include "oshi.h"

int prompt_empty_ok;

char *editorPrompt(char *prompt, void (*callback)(char *, int),
                   void (*decorate)(char *)) {
  size_t bufsize = OSHI_PROMPT_MAX + 8;
  char *buf = xmalloc(bufsize);
  size_t buflen = 0;
  buf[0] = '\0';
  E.in_prompt = 1;
  while (1) {
    editorSetStatusMessage(prompt, buf);
    if (decorate) decorate(buf);
    editorRefreshScreen();
    int c = editorReadKey();
    if (c == RESIZE_KEY || c == MOUSE_KEY || c == NOKEY) continue;
    if (c == CTRL_BACKSPACE) {
      size_t p = buflen;
      while (p > 0 && isspace((unsigned char)buf[p - 1])) p--;
      if (p < buflen) {
        while (p > 0 && !isspace((unsigned char)buf[p - 1])) p--;
      } else {
        do { p--; } while (p > 0 && ((unsigned char)buf[p] & 0xC0) == 0x80);
      }
      buflen = p;
      buf[buflen] = '\0';
    } else if (c == DEL_KEY || c == BACKSPACE) {
      if (buflen != 0) {
        do {
          buflen--;
        } while (buflen > 0 && ((unsigned char)buf[buflen] & 0xC0) == 0x80);
        buf[buflen] = '\0';
      }
    } else if (c == '\x1b') {
      editorSetStatusMessage("");
      if (callback) callback(buf, c);
      free(buf);
      E.in_prompt = 0;
      return NULL;
    } else if (c == '\r') {
      if (buflen != 0 || prompt_empty_ok) {
        editorSetStatusMessage("");
        if (callback) callback(buf, c);
        E.in_prompt = 0;
        return buf;
      }
    } else if (c == UTF8_KEY) {
      if (buflen + E.utf8len <= OSHI_PROMPT_MAX) {
        memcpy(buf + buflen, E.utf8, E.utf8len);
        buflen += E.utf8len;
        buf[buflen] = '\0';
      }
    } else if (c == PASTE_KEY) {
      int n, i;
      char *t = editorReadPasteBuf(&n);
      for (i = 0; i < n && t[i] != '\n' && t[i] != '\r'; i++) {
        unsigned char u = (unsigned char)t[i];
        if (u < 32 || u == 127) continue;
        if (buflen + 1 > OSHI_PROMPT_MAX) break;
        buf[buflen++] = t[i];
      }
      buf[buflen] = '\0';
      free(t);
    } else if (c >= 32 && c < 127) {
      if (buflen + 1 <= OSHI_PROMPT_MAX) {
        buf[buflen++] = c;
        buf[buflen] = '\0';
      }
    }

    if (callback) callback(buf, c);
  }
}

static void editorMoveCursor(int key) {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

  switch (key) {
    case ARROW_LEFT:
      if (row && E.cx > 0) {
        E.cx = editorPrevChar(row, E.cx);
      } else if (E.cy > 0) {
        E.cy--;
        E.cx = E.row[E.cy].size;
      }
      break;
    case ARROW_RIGHT:
      if (row && E.cx < row->size) {
        E.cx = editorCharEnd(row, E.cx);
      } else if (E.cy + 1 < E.numrows) {
        E.cy++;
        E.cx = 0;
      }
      break;
    case ARROW_UP:
      if (E.cy > 0) {
        E.cy--;
        E.cx = editorRowRxToCx(&E.row[E.cy], E.goal_rx);
      }
      break;
    case ARROW_DOWN:
      if (E.cy + 1 < E.numrows) {
        E.cy++;
        E.cx = editorRowRxToCx(&E.row[E.cy], E.goal_rx);
      } else if (row) {
        E.cx = row->size;
        E.goal_rx = E.rx;
      }
      break;
  }
}

static void editorMoveWord(int dir) {
  erow *row = E.cy < E.numrows ? &E.row[E.cy] : NULL;
  int size = row ? row->size : 0;
  int i = E.cx;

  if (dir > 0) {
    if (i >= size) {
      if (E.cy + 1 < E.numrows) {
        E.cy++;
        E.cx = 0;
      }
      return;
    }
    while (i < size && !isspace((unsigned char)row->chars[i])) i++;
    while (i < size && isspace((unsigned char)row->chars[i])) i++;
  } else {
    if (i == 0) {
      if (E.cy > 0) {
        E.cy--;
        E.cx = E.row[E.cy].size;
      }
      return;
    }
    while (i > 0 && isspace((unsigned char)row->chars[i - 1])) i--;
    while (i > 0 && !isspace((unsigned char)row->chars[i - 1])) i--;
  }
  E.cx = i;
}

long editorNowMs(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void editorWordAt(int *from, int *to) {
  erow *row = E.cy < E.numrows ? &E.row[E.cy] : NULL;
  int i, sp, a, b;
  if (!row || row->size == 0) {
    *from = *to = 0;
    return;
  }
  i = E.cx;
  if (i >= row->size) i = row->size - 1;
  sp = isspace((unsigned char)row->chars[i]) ? 1 : 0;
  a = i;
  b = i + 1;
  while (a > 0 && (isspace((unsigned char)row->chars[a - 1]) ? 1 : 0) == sp) a--;
  while (b < row->size && (isspace((unsigned char)row->chars[b]) ? 1 : 0) == sp) b++;
  *from = a;
  *to = b;
}

static void editorMouse(int button, int release, int x, int y) {
  if (y > E.screenrows) y = E.screenrows;
  if (y < 1) y = 1;
  int filerow = y - 1 + E.rowoff;

  if (button & 64) {
    int dir = button & 3;
    int i;
    if (dir >= 2) {
      for (i = 0; i < 3; i++)
        editorMoveCursor(dir == 3 ? ARROW_RIGHT : ARROW_LEFT);
      editorSyncGoal();
    } else {
      for (i = 0; i < 3; i++)
        editorMoveCursor(dir == 1 ? ARROW_DOWN : ARROW_UP);
    }
    return;
  }
  if ((button & 3) != 0) return;

  int last = E.numrows > 0 ? E.numrows - 1 : 0;
  if (filerow < 0) filerow = 0;
  if (filerow > last) filerow = last;

  int rx = x - 1 - E.gutter + E.coloff;
  if (rx < 0) rx = 0;

  if (!(release && E.sel && x == E.mouse_click_x && y == E.mouse_click_y)) {
    E.cy = filerow;
    E.cx = filerow < E.numrows ? editorRowRxToCx(&E.row[filerow], rx) : 0;
    editorSyncGoal();
  }

  if (release) return;
  if (button & 32) {
    E.sel = 1;
    return;
  }

  long now = editorNowMs();
  int near = abs(x - E.mouse_click_x) <= OSHI_CLICK_SLOP &&
             abs(y - E.mouse_click_y) <= OSHI_CLICK_SLOP;
  E.mouse_click_n = (now - E.mouse_click_ms <= OSHI_CLICK_MS && near)
                      ? E.mouse_click_n + 1 : 1;
  E.mouse_click_ms = now;
  E.mouse_click_x = x;
  E.mouse_click_y = y;

  E.sel_cy = E.cy;
  E.sel_cx = E.cx;
  E.sel = 0;
  if (E.numrows == 0) return;

  if (E.mouse_click_n >= 3) {
    E.sel_cx = 0;
    E.cx = E.row[E.cy].size;
    E.sel = 1;
    E.mouse_click_n = 0;
  } else if (E.mouse_click_n == 2) {
    int from, to;
    editorWordAt(&from, &to);
    E.sel_cx = from;
    E.cx = to;
    E.sel = 1;
  }
}

void editorDrawBox(struct abuf *ab, const char *title,
                   const char **body, int nbody, int width) {
  char tmp[256];
  int height = nbody + 2;
  int top = (E.screenrows - height) / 2;
  if (top < 0) top = 0;
  int left = (E.screencols - (width + 2)) / 2;
  if (left < 0) left = 0;

  int tlen = (int)strlen(title);
  int pre = (width - tlen) > 0 ? (width - tlen) / 2 : 0;
  int post = width - tlen - pre;
  if (post < 0) post = 0;
  int i;

  abAppend(ab, "\x1b[?25l", 6);
  abAppend(ab, "\x1b[2J\x1b[H", 7);

  int n = snprintf(tmp, sizeof(tmp), "\x1b[%d;%dH+", top + 1, left + 1);
  abAppend(ab, tmp, n);
  for (i = 0; i < pre; i++) abAppend(ab, "-", 1);
  abAppend(ab, title, tlen);
  for (i = 0; i < post; i++) abAppend(ab, "-", 1);
  abAppend(ab, "+", 1);

  for (i = 0; i < nbody; i++) {
    int blen = (int)strlen(body[i]);
    int n2 = snprintf(tmp, sizeof(tmp), "\x1b[%d;%dH", top + 2 + i, left + 1);
    abAppend(ab, tmp, n2);
    abAppend(ab, "| ", 2);
    abAppend(ab, body[i], blen);
    int pad = width - 1 - blen;
    for (; pad > 0; pad--) abAppend(ab, " ", 1);
    abAppend(ab, "|", 1);
  }

  int n3 = snprintf(tmp, sizeof(tmp), "\x1b[%d;%dH+", top + 2 + nbody, left + 1);
  abAppend(ab, tmp, n3);
  for (i = 0; i < width; i++) abAppend(ab, "-", 1);
  abAppend(ab, "+", 1);
  abAppend(ab, "\x1b[0m", 4);
}

static void editorHelp(void) {
  char lines[CMD_COUNT][128];
  int n = CMD_COUNT, width = 0, i;
  for (i = 0; i < n; i++) {
    char key[16];
    editorKeyName(E.binds[i], key, sizeof(key));
    int len = snprintf(lines[i], sizeof(lines[i]), "%-10s %-14s %s",
                       key, editorCmds[i].name, editorCmds[i].desc);
    if (len > width) width = len;
  }
  width += 1;
  const char *body[CMD_COUNT];
  for (i = 0; i < n; i++) body[i] = lines[i];
  for (;;) {
    struct abuf ab = ABUF_INIT;
    editorDrawBox(&ab, "oshi key bindings", body, n, width);
    ssize_t r = write(STDOUT_FILENO, ab.b, ab.len);
    (void)r;
    abFree(&ab);
    int k = editorReadKey();
    if (k == PASTE_KEY) editorSkipPaste();
    if (k == RESIZE_KEY || k == MOUSE_KEY || k == NOKEY || k == PASTE_KEY)
      continue;
    break;
  }
  E.redraw = 1;
}

static const char *editorElevTool(void) {
  static const char *names[] = { "doas", "sudo" };
  size_t i;
  for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    if (editorInPath(names[i])) return names[i];
  return NULL;
}

static void editorElevReason(char *out, size_t outsz, const char *raw, int rawlen) {
  size_t o = 0;
  int i;
  for (i = 0; i < rawlen && o + 1 < outsz; i++) {
    unsigned char c = (unsigned char)raw[i];
    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    else if (c < 32 || c == 127) continue;
    out[o++] = (char)c;
  }
  while (o > 0 && out[o - 1] == ' ') o--;
  out[o] = '\0';
}

static int editorVerifySaved(const char *buf, int len, long *got) {
  struct stat st;
  char chunk[8192];
  int fd, off = 0;

  *got = -1;
  if (stat(E.filename, &st) == -1 || !S_ISREG(st.st_mode)) return 1;
  *got = (long)st.st_size;
  if (st.st_size != (off_t)len) return 0;
  fd = open(E.filename, O_RDONLY);
  if (fd == -1) return 1;
  for (;;) {
    ssize_t n = read(fd, chunk, sizeof(chunk));
    if (n == 0) break;
    if (n < 0) {
      if (errno == EINTR) continue;
      close(fd);
      return 1;
    }
    if (off + (int)n > len || memcmp(chunk, buf + off, (size_t)n) != 0) {
      close(fd);
      return 0;
    }
    off += (int)n;
  }
  close(fd);
  return off == len;
}

static int editorRunElevated(const char *tool, const char *buf, int len) {
  char etmpl[] = "/tmp/oshi-err-XXXXXX";
  char arg_of[4096];
  int pfd[2];
  int efd, status = 0, ok = 0;
  pid_t pid;

  if (pipe(pfd) == -1) {
    editorSetStatusMessage("Can't save! pipe: %s", strerror(errno));
    return -1;
  }
  efd = mkstemp(etmpl);
  if (efd == -1) {
    editorSetStatusMessage("Can't save! Temp file: %s", strerror(errno));
    close(pfd[0]);
    close(pfd[1]);
    return -1;
  }
  if (strlen(E.filename) + 3 >= sizeof(arg_of)) {
    editorSetStatusMessage("Can't save! Path too long");
    close(pfd[0]);
    close(pfd[1]);
    close(efd);
    unlink(etmpl);
    return -1;
  }
  snprintf(arg_of, sizeof(arg_of), "of=%s", E.filename);
  char *toolpath = editorWhich(tool);
  if (toolpath == NULL) {
    editorSetStatusMessage("Can't save! %s not found in PATH", tool);
    close(pfd[0]);
    close(pfd[1]);
    close(efd);
    unlink(etmpl);
    return -1;
  }
  char *pdir = editorParentDir(E.filename);
  if (pdir && editorIsDir(pdir)) { free(pdir); pdir = NULL; }

  {
    char at[32];
    int n = snprintf(at, sizeof(at), "\x1b[%d;1H", E.screenrows);
    editorWriteAll(STDOUT_FILENO, at, n);
  }

  disableRawMode();
  editorWrite("\x1b[?1002l\x1b[?1006l\x1b[?2004l");
  struct sigaction ign, old_int, old_quit;
  memset(&ign, 0, sizeof(ign));
  ign.sa_handler = SIG_IGN;
  sigemptyset(&ign.sa_mask);
  sigaction(SIGINT, &ign, &old_int);
  sigaction(SIGQUIT, &ign, &old_quit);
  pid = fork();
  if (pid == 0) {
    int devnull = open("/dev/null", O_WRONLY);
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    dup2(pfd[0], STDIN_FILENO);
    if (devnull != -1) dup2(devnull, STDOUT_FILENO);
    dup2(efd, STDERR_FILENO);
    close(pfd[0]);
    close(pfd[1]);
    close(efd);
    if (devnull > 2) close(devnull);
    if (pdir) {
      char *const av[] = { (char *)tool, "sh", "-c",
        "mkdir -p -- \"$2\" && exec dd bs=4k of=\"$1\" conv=fsync",
        "sh", E.filename, pdir, NULL };
      execv(toolpath, av);
    } else {
      char *const av[] = { (char *)tool, "dd", "bs=4k", arg_of,
                           "conv=fsync", NULL };
      execv(toolpath, av);
    }
    _exit(127);
  }
  close(pfd[0]);
  if (pid > 0) {
    int werr = editorWriteAll(pfd[1], buf, len) == -1 ? errno : 0;
    close(pfd[1]);
    while (waitpid(pid, &status, 0) == -1)
      if (errno != EINTR) break;
    ok = WIFEXITED(status) && WEXITSTATUS(status) == 0 && werr == 0;
  } else {
    close(pfd[1]);
  }
  sigaction(SIGINT, &old_int, NULL);
  sigaction(SIGQUIT, &old_quit, NULL);
  enableRawMode();
  editorTermSetup();
  E.redraw = 1;

  if (pid == -1) {
    editorSetStatusMessage("Can't save! fork: %s", strerror(errno));
  } else if (ok) {
    long got = -1;
    if (!editorVerifySaved(buf, len, &got)) {
      editorSetStatusMessage("Save wrong: %ld of %d bytes - check the file", got, len);
      ok = 0;
    } else {
      editorSetStatusMessage("%d bytes written to disk (via %s)", len, tool);
    }
  } else {
    char raw[160] = "", why[160];
    ssize_t got;
    lseek(efd, 0, SEEK_SET);
    got = read(efd, raw, sizeof(raw) - 1);
    if (got < 0) got = 0;
    editorElevReason(why, sizeof(why), raw, (int)got);
    const char *reason = why;
    if (strncmp(reason, tool, strlen(tool)) == 0 && reason[strlen(tool)] == ':' &&
        reason[strlen(tool) + 1] == ' ')
      reason += strlen(tool) + 2;
    if (reason[0])
      editorSetStatusMessage("%s: %s", tool, reason);
    else if (WIFSIGNALED(status))
      editorSetStatusMessage("%s was interrupted - nothing written", tool);
    else
      editorSetStatusMessage("%s failed (exit %d) - nothing written", tool,
                             WEXITSTATUS(status));
  }
  close(efd);
  unlink(etmpl);
  free(pdir);
  free(toolpath);
  return ok ? 0 : -1;
}

int editorSaveElevated(const char *buf, int len) {
  const char *tool = editorElevTool();
  char lines[5][96];
  const char *body[5];
  int n = 0, width = 0, i;
  char *pd = editorParentDir(E.filename);
  int mk = pd && !editorIsDir(pd);
  free(pd);

  snprintf(lines[n++], sizeof(lines[0]), "%s", "Save refused: permission denied.");
  if (mk && tool)
    snprintf(lines[n++], sizeof(lines[0]), "%s", "The directory is missing: it will be created.");
  if (tool) {
    snprintf(lines[n++], sizeof(lines[0]), "Redo the save as root with %s?", tool);
    snprintf(lines[n++], sizeof(lines[0]), "%-14s%s", "enter", "save as root");
    snprintf(lines[n++], sizeof(lines[0]), "%-14s%s", "esc/q/ctrl-q", "keep editing");
  } else {
    snprintf(lines[n++], sizeof(lines[0]), "%s", "No doas or sudo found in PATH.");
    snprintf(lines[n++], sizeof(lines[0]), "%-14s%s", "esc/q/ctrl-q", "keep editing");
  }
  for (i = 0; i < n; i++) {
    int l = (int)strlen(lines[i]);
    if (l > width) width = l;
    body[i] = lines[i];
  }
  width += 1;

  for (;;) {
    struct abuf ab = ABUF_INIT;
    editorDrawBox(&ab, "privileges required", body, n, width);
    ssize_t r = write(STDOUT_FILENO, ab.b, ab.len);
    (void)r;
    abFree(&ab);
    int k = editorReadKey();
    if (k == PASTE_KEY) editorSkipPaste();
    if (k == RESIZE_KEY || k == MOUSE_KEY || k == NOKEY || k == PASTE_KEY)
      continue;
    if (tool && (k == '\r' || k == '\n')) {
      E.redraw = 1;
      return editorRunElevated(tool, buf, len);
    }
    break;
  }
  E.redraw = 1;
  editorSetStatusMessage(tool ? "Save cancelled" : "No sudo or doas in PATH");
  return -1;
}

void editorProcessKeypress(void) {
  int c = editorReadKey();
  int cmd = editorCommandForKey(c);
  int vertical = (c == ARROW_UP || c == ARROW_DOWN || c == SHIFT_UP ||
                  c == SHIFT_DOWN || c == PAGE_UP || c == PAGE_DOWN ||
                  c == MOUSE_KEY);

  if (c == RESIZE_KEY || c == NOKEY) return;
  E.config_err[0] = '\0';

  if (cmd != CMD_COPY && cmd != CMD_CUT && cmd != CMD_PASTE &&
      c != MOUSE_KEY && c != NOKEY && c != CTRL_LEFT && c != CTRL_RIGHT &&
      (c < SHIFT_UP || c > CSHIFT_RIGHT) &&
      c != '\r' && c != '\t' && c != DEL_KEY && c != CTRL_BACKSPACE &&
      c != UTF8_KEY &&
      c != PASTE_KEY && !(c >= 32 && c < ARROW_LEFT)) E.sel = 0;

  if (cmd != CMD_NONE) {
    switch (cmd) {
    case CMD_QUIT: {
      long now = editorNowMs();
      if (E.dirty && !(E.quit_confirm && now - E.quit_time < OSHI_STATUS_MS)) {
        E.quit_confirm = 1;
        E.quit_time = now;
        return;
      }
      editorLeave();
      break;
    }
    case CMD_SAVE: editorSave(); break;
    case CMD_FIND: editorFind(); break;
    case CMD_SELECT: editorSelectAll(); break;
    case CMD_COPY: editorCopySelection(); break;
    case CMD_CUT: editorCut(); break;
    case CMD_PASTE: editorPasteInternal(); break;
    case CMD_KILL: editorDeleteLine(); break;
    case CMD_UNDO: editorUndo(); break;
    case CMD_REDO: editorRedo(); break;
    case CMD_REPLACE: editorReplace(); break;
    case CMD_FIND_NEXT:
      if (find_on) editorFindNextMatch(1);
      break;
    case CMD_FIND_PREV:
      if (find_on) editorFindNextMatch(-1);
      break;
    case CMD_GOTO: editorGoto(); break;
    case CMD_HELP: editorHelp(); break;
    case CMD_DELETE_WORD:
      if (E.sel) editorDeleteSelection();
      else editorDeleteWordBackward();
      break;
    }
    E.quit_confirm = 0;
    editorSyncGoal();
    return;
  }

  if (c == '\x1b' && find_on) {
    editorFindClear();
  }

  switch (c) {
  case '\r':
    editorInsertNewline();
    break;

  case HOME_KEY:
    E.cx = 0;
    break;

  case END_KEY:
    if (E.cy < E.numrows) E.cx = E.row[E.cy].size;
    break;

  case CTRL_LEFT:
  case CTRL_RIGHT:
    if (editorAllSelected()) {
      editorMoveToEnd(c == CTRL_RIGHT ? 1 : -1);
      E.sel = 0;
    } else {
      editorMoveWord(c == CTRL_RIGHT ? 1 : -1);
    }
    break;

  case SHIFT_UP:
  case SHIFT_DOWN:
  case SHIFT_LEFT:
  case SHIFT_RIGHT:
  case CSHIFT_LEFT:
  case CSHIFT_RIGHT:
    if (!E.sel) { E.sel_cy = E.cy; E.sel_cx = E.cx; E.sel = 1; }
    if (c == CSHIFT_LEFT || c == CSHIFT_RIGHT)
      editorMoveWord(c == CSHIFT_RIGHT ? 1 : -1);
    else
      editorMoveCursor(c == SHIFT_UP ? ARROW_UP : c == SHIFT_DOWN ? ARROW_DOWN
                     : c == SHIFT_LEFT ? ARROW_LEFT : ARROW_RIGHT);
    if (E.sel_cy == E.cy && E.sel_cx == E.cx) E.sel = 0;
    break;

  case MOUSE_KEY:
    editorMouse(E.mouse_button, E.mouse_release, E.mouse_x, E.mouse_y);
    break;

  case BACKSPACE:
  case DEL_KEY:
    if (E.sel) {
      editorDeleteSelection();
      break;
    }
    editorDeleteChar(c == DEL_KEY);
    break;

  case PAGE_UP:
  case PAGE_DOWN:
    {
      int last = E.numrows > 0 ? E.numrows - 1 : 0;
      if (c == PAGE_UP) {
        E.cy = E.rowoff;
      } else {
        E.cy = E.rowoff + E.screenrows - 1;
        if (E.cy > last) E.cy = last;
      }
      int times = E.screenrows;
      while (times--)
        editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      if (E.cy < E.numrows)
        E.cx = editorRowRxToCx(&E.row[E.cy], E.goal_rx);
    }
    break;

  case ARROW_UP:
  case ARROW_DOWN:
  case ARROW_LEFT:
  case ARROW_RIGHT:
    editorMoveCursor(c);
    break;

  case UTF8_KEY:
    editorTypeBytes(E.utf8, E.utf8len);
    break;

  case PASTE_KEY: {
    int n;
    char *t = editorReadPasteBuf(&n);
    int lines = editorInsertText(t, n);
    free(t);
    if (lines > 0)
      editorSetStatusMessage("Pasted %d line%s", lines, lines == 1 ? "" : "s");
    break;
  }

  case '\x1b':
  case CTRL_KEY('l'):
  case NOKEY:
    break;

  default:
    if ((c >= 32 && c < 127) || c == '\t') {
      char ch = (char)c;
      editorTypeBytes(&ch, 1);
    }
    break;
  }
  E.quit_confirm = 0;
  editorClampCursor();
  if (!vertical) editorSyncGoal();
}
