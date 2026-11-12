#define _GNU_SOURCE

#include "oshi.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#ifndef NAME_MAX
#define NAME_MAX 255
#endif

struct brEntry {
  char *name;
  int isdir;
  int islink;
  off_t size;
};

static struct brEntry *br_ent;
static int br_n, br_cap;
static char br_dir[PATH_MAX];
static char br_last[NAME_MAX + 1];
static int br_sel, br_top;
static int br_hidden;
int br_active;

static int brListHeight(void) {
  int h = E.screenrows - 1;
  return h < 1 ? 1 : h;
}

static void brFreeEntries(void) {
  int i;
  for (i = 0; i < br_n; i++) free(br_ent[i].name);
  br_n = 0;
}

static int brCmp(const void *a, const void *b) {
  const struct brEntry *x = a, *y = b;
  int r;
  if (x->isdir != y->isdir) return y->isdir - x->isdir;
  r = strcasecmp(x->name, y->name);
  return r ? r : strcmp(x->name, y->name);
}

static void brAddEntry(const char *name, int isdir, int islink, off_t size) {
  if (br_n == br_cap) {
    br_cap = br_cap ? br_cap * 2 : 64;
    br_ent = xrealloc(br_ent, sizeof(*br_ent) * (size_t)br_cap);
  }
  br_ent[br_n].name = strdup(name);
  if (br_ent[br_n].name == NULL) die("strdup");
  br_ent[br_n].isdir = isdir;
  br_ent[br_n].islink = islink;
  br_ent[br_n].size = size;
  br_n++;
}

static int brPath(char *out, size_t sz, const char *name) {
  int n = snprintf(out, sz, "%s%s%s", br_dir,
                   strcmp(br_dir, "/") == 0 ? "" : "/", name);
  return (n < 0 || (size_t)n >= sz) ? -1 : 0;
}

static int brLoad(void) {
  DIR *d = opendir(br_dir);
  struct dirent *de;
  int first;
  if (d == NULL) return -1;
  brFreeEntries();
  if (strcmp(br_dir, "/") != 0) brAddEntry("..", 1, 0, 0);
  first = br_n;
  while ((de = readdir(d)) != NULL) {
    char full[PATH_MAX + NAME_MAX + 2];
    struct stat lst, st;
    if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
    if (!br_hidden && de->d_name[0] == '.') continue;
    if (brPath(full, sizeof(full), de->d_name) == -1) continue;
    if (lstat(full, &lst) == -1) continue;
    if (S_ISLNK(lst.st_mode) && stat(full, &st) == 0)
      brAddEntry(de->d_name, S_ISDIR(st.st_mode), 1, st.st_size);
    else
      brAddEntry(de->d_name, S_ISDIR(lst.st_mode), S_ISLNK(lst.st_mode),
                 lst.st_size);
  }
  closedir(d);
  if (br_ent != NULL && br_n - first > 1)
    qsort(br_ent + first, (size_t)(br_n - first), sizeof(*br_ent), brCmp);
  return 0;
}

static void brSelectName(const char *name) {
  int i;
  br_sel = (br_ent != NULL && br_n > 1 && strcmp(br_ent[0].name, "..") == 0) ? 1 : 0;
  if (name == NULL) return;
  for (i = 0; i < br_n; i++)
    if (strcmp(br_ent[i].name, name) == 0) {
      br_sel = i;
      return;
    }
}

static int brGo(const char *dir, const char *select, char *msg, size_t msgsz) {
  char old[PATH_MAX], real[PATH_MAX];
  if (realpath(dir, real) == NULL) {
    snprintf(msg, msgsz, "%s: %s", dir, strerror(errno));
    return -1;
  }
  snprintf(old, sizeof(old), "%s", br_dir);
  snprintf(br_dir, sizeof(br_dir), "%s", real);
  if (brLoad() == -1) {
    int e = errno;
    snprintf(br_dir, sizeof(br_dir), "%s", old);
    snprintf(msg, msgsz, "Can't open folder: %s", strerror(e));
    return -1;
  }
  br_top = 0;
  brSelectName(select);
  return 0;
}

static void brUp(char *msg, size_t msgsz) {
  char child[NAME_MAX + 1], parent[PATH_MAX];
  char *slash = strrchr(br_dir, '/');
  if (slash == NULL || strcmp(br_dir, "/") == 0) return;
  snprintf(child, sizeof(child), "%s", slash + 1);
  if (slash == br_dir) {
    snprintf(parent, sizeof(parent), "/");
  } else {
    size_t n = (size_t)(slash - br_dir);
    memcpy(parent, br_dir, n);
    parent[n] = '\0';
  }
  brGo(parent, child, msg, msgsz);
}

static void brReload(const char *select, char *msg, size_t msgsz) {
  int keep = br_sel;
  if (brLoad() == -1) {
    snprintf(msg, msgsz, "Can't read folder: %s", strerror(errno));
    return;
  }
  if (select) brSelectName(select);
  else br_sel = keep;
}

static int brPutClean(struct abuf *ab, const char *s, int maxcols) {
  int used = 0, i = 0, n = (int)strlen(s);
  if (maxcols < 0) maxcols = 0;
  while (i < n) {
    int len, w, cp = utf8Decode((const unsigned char *)s + i, n - i, &len);
    int bad = cp < 32 || cp == 127 || (cp >= 0x80 && cp < 0xA0) ||
              cp == 0x061C || cp == 0x200E || cp == 0x200F ||
              (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2066 && cp <= 0x2069);
    w = bad ? 1 : cpWidth(cp);
    if (used + w > maxcols) break;
    if (bad) abAppend(ab, "?", 1);
    else if (cp == 0xFFFD && len == 1) abAppend(ab, "\xEF\xBF\xBD", 3);
    else abAppend(ab, s + i, len);
    used += w;
    i += len;
  }
  return used;
}

static void brPad(struct abuf *ab, int n) {
  while (n-- > 0) abAppend(ab, " ", 1);
}

static void brColor(struct abuf *ab, int role) {
  abAppend(ab, E.col_str[role], (int)strlen(E.col_str[role]));
}

static void brHuman(off_t n, char *out, size_t sz) {
  static const char unit[] = "KMGTP";
  double v = (double)n;
  int u = -1;
  if (n < 1024) {
    snprintf(out, sz, "%lldB", (long long)n);
    return;
  }
  while (v >= 1024 && u < 4) {
    v /= 1024;
    u++;
  }
  snprintf(out, sz, "%.1f%c", v, unit[u]);
}

static void brDraw(const char *msg, const char *plabel, const char *pbuf,
                   int pcur) {
  static const char hint[] =
    "enter open  n file  N folder  p paste  r rename  d del  . hidden  ? help  q quit";
  struct abuf ab = ABUF_INIT;
  char tmp[48];
  int rows = E.screenrows + 1, cols = E.screencols;
  int lh = brListHeight(), r, n, used;

  if (br_sel >= br_n) br_sel = br_n - 1;
  if (br_sel < 0) br_sel = 0;
  if (br_sel < br_top) br_top = br_sel;
  if (br_sel >= br_top + lh) br_top = br_sel - lh + 1;
  if (br_top < 0) br_top = 0;

  abAppend(&ab, "\x1b[?25l", 6);
  if (E.redraw) {
    abAppend(&ab, "\x1b[2J", 4);
    E.redraw = 0;
  }

  abAppend(&ab, "\x1b[1;1H\x1b[0m", 10);
  brColor(&ab, COL_STATUS_BG);
  brColor(&ab, COL_STATUS_FG);
  abAppend(&ab, " ", 1);
  {
    const char *s = br_dir;
    int dl = (int)strlen(s), avail = cols - 2;
    if (avail < 1) avail = 1;
    used = 1;
    if (editorStrWidth(s, dl) > avail) {
      s += editorFitTail(s, dl, avail - 1);
      abAppend(&ab, "<", 1);
      used++;
    }
    used += brPutClean(&ab, s, cols - used);
    brPad(&ab, cols - used);
  }
  abAppend(&ab, "\x1b[0m", 4);

  for (r = 0; r < lh; r++) {
    int idx = br_top + r;
    n = snprintf(tmp, sizeof(tmp), "\x1b[%d;1H\x1b[0m\x1b[K", 2 + r);
    abAppend(&ab, tmp, n);
    if (idx < br_n) {
      const struct brEntry *e = &br_ent[idx];
      int sizew = cols >= 36 ? 10 : 0;
      int namew = cols - 1 - sizew;
      char label[NAME_MAX + 3];
      const char *suffix = strcmp(e->name, "..") == 0 ? ""
                         : e->isdir ? "/" : e->islink ? "@" : "";
      brColor(&ab, e->isdir || idx == br_sel ? COL_GUTTER_CURSOR_FG : COL_TEXT_FG);
      if (idx == br_sel) abAppend(&ab, "\x1b[7m", 4);
      abAppend(&ab, " ", 1);
      snprintf(label, sizeof(label), "%s%s", e->name, suffix);
      used = brPutClean(&ab, label, namew);
      brPad(&ab, namew - used);
      if (sizew) {
        char hs[32], col[48];
        hs[0] = '\0';
        if (!e->isdir) brHuman(e->size, hs, sizeof(hs));
        n = snprintf(col, sizeof(col), "%9s ", hs);
        abAppend(&ab, col, n);
      }
    } else if (br_n == 0 && r == 0) {
      brColor(&ab, COL_TILDE_FG);
      abAppend(&ab, " (empty)", 8);
    }
    abAppend(&ab, "\x1b[0m", 4);
  }

  n = snprintf(tmp, sizeof(tmp), "\x1b[%d;1H\x1b[0m", rows);
  abAppend(&ab, tmp, n);
  brColor(&ab, COL_STATUS_BG);
  brColor(&ab, COL_STATUS_FG);
  abAppend(&ab, " ", 1);
  used = 1;
  if (plabel) {
    char head[NAME_MAX + 1], cb[8];
    int room, bl = (int)strlen(pbuf), hl = pcur, cl = 0, curw = 1, tw, hroom;
    const char *after;
    if (hl < 0) hl = 0;
    if (hl > bl) hl = bl;
    memcpy(head, pbuf, (size_t)hl);
    head[hl] = '\0';
    if (hl < bl) {
      int cp = utf8Decode((const unsigned char *)pbuf + hl, bl - hl, &cl);
      curw = cpWidth(cp);
      if (curw < 1) curw = 1;
      if (cl > (int)sizeof(cb) - 1) cl = (int)sizeof(cb) - 1;
    }
    after = pbuf + hl + cl;
    used += brPutClean(&ab, plabel, cols - 2);
    room = cols - used - 2;
    if (room < curw + 1) room = curw + 1;
    tw = editorStrWidth(after, (int)strlen(after));
    hroom = room - curw - (tw < (room - curw) / 2 ? tw : (room - curw) / 2);
    if (hroom < 0) hroom = 0;
    used += brPutClean(&ab, head + editorFitTail(head, hl, hroom), hroom);
    abAppend(&ab, "\x1b[7m", 4);
    if (hl < bl) {
      memcpy(cb, pbuf + hl, (size_t)cl);
      cb[cl] = '\0';
      used += brPutClean(&ab, cb, curw);
    } else {
      abAppend(&ab, " ", 1);
      used++;
    }
    abAppend(&ab, "\x1b[27m", 5);
    if (cols - used - 1 > 0)
      used += brPutClean(&ab, after, cols - used - 1);
  } else if (msg[0]) {
    used += brPutClean(&ab, msg, cols - 2);
  } else {
    used += brPutClean(&ab, hint, cols - 2);
  }
  brPad(&ab, cols - used);
  abAppend(&ab, "\x1b[0m", 4);

  editorWriteAll(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

static int brValidName(const char *s) {
  return s[0] != '\0' && strchr(s, '/') == NULL && strcmp(s, ".") != 0 &&
         strcmp(s, "..") != 0;
}

static void brInsert(char *buf, size_t *len, size_t *pos, const char *s,
                     size_t n) {
  if (n == 0 || *len + n > NAME_MAX) return;
  memmove(buf + *pos + n, buf + *pos, *len - *pos + 1);
  memcpy(buf + *pos, s, n);
  *len += n;
  *pos += n;
}

static int brIsCont(const char *buf, size_t i) {
  return ((unsigned char)buf[i] & 0xC0) == 0x80;
}

static char *brPrompt(const char *label, const char *initial) {
  char buf[NAME_MAX + 1];
  size_t len, pos;
  snprintf(buf, sizeof(buf), "%s", initial ? initial : "");
  len = strlen(buf);
  pos = len;
  for (;;) {
    int c;
    brDraw("", label, buf, (int)pos);
    c = editorReadKey();
    if (c == '\x1b') return NULL;
    if (c == '\r') {
      char *out;
      if (len == 0) continue;
      out = strdup(buf);
      if (out == NULL) die("strdup");
      return out;
    }
    if (c == ARROW_LEFT) {
      if (pos > 0) do pos--; while (pos > 0 && brIsCont(buf, pos));
    } else if (c == ARROW_RIGHT) {
      if (pos < len) do pos++; while (pos < len && brIsCont(buf, pos));
    } else if (c == HOME_KEY) {
      pos = 0;
    } else if (c == END_KEY) {
      pos = len;
    } else if (c == CTRL_LEFT) {
      while (pos > 0 && buf[pos - 1] == ' ') pos--;
      while (pos > 0 && buf[pos - 1] != ' ') pos--;
    } else if (c == CTRL_RIGHT) {
      while (pos < len && buf[pos] == ' ') pos++;
      while (pos < len && buf[pos] != ' ') pos++;
    } else if (c == BACKSPACE) {
      if (pos > 0) {
        size_t from = pos;
        do from--; while (from > 0 && brIsCont(buf, from));
        memmove(buf + from, buf + pos, len - pos + 1);
        len -= pos - from;
        pos = from;
      }
    } else if (c == DEL_KEY) {
      if (pos < len) {
        size_t to = pos;
        do to++; while (to < len && brIsCont(buf, to));
        memmove(buf + pos, buf + to, len - to + 1);
        len -= to - pos;
      }
    } else if (c == CTRL_BACKSPACE) {
      size_t from = pos;
      while (from > 0 && buf[from - 1] == ' ') from--;
      while (from > 0 && buf[from - 1] != ' ') from--;
      memmove(buf + from, buf + pos, len - pos + 1);
      len -= pos - from;
      pos = from;
    } else if (c == UTF8_KEY) {
      brInsert(buf, &len, &pos, E.utf8, (size_t)E.utf8len);
    } else if (c == PASTE_KEY) {
      int n, i;
      char *t = editorReadPasteBuf(&n);
      for (i = 0; i < n && t[i] != '\n' && t[i] != '\r'; i++) {
        unsigned char u = (unsigned char)t[i];
        if (u < 32 || u == 127) continue;
        brInsert(buf, &len, &pos, t + i, 1);
      }
      free(t);
    } else if (c >= 32 && c < 127) {
      char ch = (char)c;
      brInsert(buf, &len, &pos, &ch, 1);
    }
  }
}

static int brConfirm(const char *line) {
  for (;;) {
    int k;
    brDraw(line, NULL, NULL, 0);
    k = editorReadKey();
    if (k == 'y' || k == 'Y' || k == '\r') return 1;
    if (k == 'n' || k == 'N' || k == 'q' || k == '\x1b' || k == CTRL_KEY('q'))
      return 0;
    if (k == PASTE_KEY) editorSkipPaste();
  }
}

static int brSaveNew(const char *ext, const char *data, int len, char *name,
                     size_t namesz, char *msg, size_t msgsz) {
  char path[PATH_MAX + NAME_MAX + 2];
  int i;
  for (i = 0; i < 10000; i++) {
    int fd;
    if (i == 0) snprintf(name, namesz, "paste.%s", ext);
    else snprintf(name, namesz, "paste(%d).%s", i, ext);
    if (brPath(path, sizeof(path), name) == -1) {
      snprintf(msg, msgsz, "That path is too long");
      return -1;
    }
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd == -1) {
      if (errno == EEXIST) continue;
      snprintf(msg, msgsz, "Can't make %s: %s", name, strerror(errno));
      return -1;
    }
    if (editorWriteAll(fd, data, len) == -1 || close(fd) == -1) {
      int e = errno;
      unlink(path);
      snprintf(msg, msgsz, "Can't write %s: %s", name, strerror(e));
      return -1;
    }
    return 0;
  }
  snprintf(msg, msgsz, "Too many paste files in this folder");
  return -1;
}

static void brPaste(const char *txt, int txtlen, char *msg, size_t msgsz) {
  char name[NAME_MAX + 1], hs[32];
  const char *data = NULL, *ext = NULL;
  char *owned = NULL;
  int len = 0;

  if (txt != NULL && txtlen > 0) {
    data = txt;
    len = txtlen;
    ext = "txt";
  } else if ((owned = editorClipboardImage(&len, &ext)) != NULL) {
    data = owned;
  } else if ((owned = editorClipboardRead(&len)) != NULL) {
    if (memchr(owned, 0, (size_t)len) != NULL) {
      free(owned);
      snprintf(msg, msgsz, "The clipboard holds neither text nor a png/jpg/webp/gif/bmp image");
      return;
    }
    data = owned;
    ext = "txt";
  } else if (E.clip != NULL && E.clip_len > 0) {
    data = E.clip;
    len = E.clip_len;
    ext = "txt";
  } else {
    snprintf(msg, msgsz, "Nothing to paste: the clipboard has no text or image");
    return;
  }
  if (brSaveNew(ext, data, len, name, sizeof(name), msg, msgsz) == 0) {
    brReload(name, msg, msgsz);
    if (msg[0] == '\0') {
      brHuman((off_t)len, hs, sizeof(hs));
      snprintf(msg, msgsz, "Pasted %s (%s)", name, hs);
    }
  }
  free(owned);
}

static int brDirIsEmpty(const char *path) {
  DIR *d = opendir(path);
  struct dirent *de;
  int empty = 1;
  if (d == NULL) return 1;
  while ((de = readdir(d)) != NULL)
    if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) {
      empty = 0;
      break;
    }
  closedir(d);
  return empty;
}

static int brRemoveTree(const char *path, int depth) {
  struct stat st;
  DIR *d;
  struct dirent *de;
  int rc = 0, e;
  if (lstat(path, &st) == -1) return -1;
  if (!S_ISDIR(st.st_mode)) return unlink(path);
  if (depth > 64) {
    errno = ELOOP;
    return -1;
  }
  d = opendir(path);
  if (d == NULL) return -1;
  while ((de = readdir(d)) != NULL) {
    char child[PATH_MAX];
    int n;
    if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
    n = snprintf(child, sizeof(child), "%s/%s", path, de->d_name);
    if (n < 0 || (size_t)n >= sizeof(child)) {
      errno = ENAMETOOLONG;
      rc = -1;
      break;
    }
    if (brRemoveTree(child, depth + 1) == -1) {
      rc = -1;
      break;
    }
  }
  e = errno;
  closedir(d);
  if (rc == -1) {
    errno = e;
    return -1;
  }
  return rmdir(path);
}

static void brHelp(void) {
  static const char *body[] = {
    "up/down  j/k        move",
    "enter  right  l     open a file or folder",
    "backspace  left  h  up a folder",
    "n                   new file",
    "p  ctrl-v           paste clipboard text/image as a file",
    "N                   new folder",
    "r                   rename",
    "d  del              delete (asks first)",
    ".                   show or hide dotfiles",
    "q  ctrl-q           quit",
  };
  int n = (int)(sizeof(body) / sizeof(body[0])), width = 0, i;
  for (i = 0; i < n; i++) {
    int l = (int)strlen(body[i]);
    if (l > width) width = l;
  }
  width += 1;
  for (;;) {
    struct abuf ab = ABUF_INIT;
    int k;
    editorDrawBox(&ab, "files", body, n, width);
    editorWriteAll(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
    k = editorReadKey();
    if (k == PASTE_KEY) editorSkipPaste();
    if (k == RESIZE_KEY || k == MOUSE_KEY || k == NOKEY || k == PASTE_KEY)
      continue;
    break;
  }
  E.redraw = 1;
}

char *editorBrowse(const char *start, const char *select) {
  char msg[300] = "";
  char real[PATH_MAX];
  static long last_click_ms;
  static int last_click_idx = -1;

  if (realpath(start, real) == NULL) die(start);
  snprintf(br_dir, sizeof(br_dir), "%s", real);
  br_top = 0;
  if (brLoad() == -1) die(br_dir);
  brSelectName(select);
  E.redraw = 1;
  E.notice_pending = 0;
  E.in_prompt = 0;

  for (;;) {
    int c, page = brListHeight() - 1;
    char path[PATH_MAX + NAME_MAX + 2], keep[NAME_MAX + 1];
    const struct brEntry *e;
    int do_open = 0;

    brDraw(msg, NULL, NULL, 0);
    msg[0] = '\0';
    c = editorReadKey();
    e = br_n > 0 ? &br_ent[br_sel] : NULL;
    if (page < 1) page = 1;

    if (c == RESIZE_KEY || c == NOKEY) continue;
    if (c == 'q' || c == E.binds[CMD_QUIT]) {
      E.redraw = 1;
      return NULL;
    }
    if (c == PASTE_KEY) {
      int n;
      char *t = editorReadPasteBuf(&n);
      brPaste(t, n, msg, sizeof(msg));
      free(t);
      continue;
    }
    if (c == 'p' || c == E.binds[CMD_PASTE]) {
      brPaste(NULL, 0, msg, sizeof(msg));
      continue;
    }

    switch (c) {
    case ARROW_UP: case 'k': if (br_sel > 0) br_sel--; break;
    case ARROW_DOWN: case 'j': if (br_sel + 1 < br_n) br_sel++; break;
    case PAGE_UP: br_sel -= page; if (br_sel < 0) br_sel = 0; break;
    case PAGE_DOWN:
      br_sel += page;
      if (br_sel > br_n - 1) br_sel = br_n - 1;
      break;
    case HOME_KEY: case 'g': br_sel = 0; break;
    case END_KEY: case 'G': br_sel = br_n > 0 ? br_n - 1 : 0; break;

    case ARROW_LEFT: case BACKSPACE: case 'h':
      brUp(msg, sizeof(msg));
      break;

    case '\r': case ARROW_RIGHT: case 'l':
      do_open = 1;
      break;

    case '.':
      snprintf(keep, sizeof(keep), "%s", e ? e->name : "");
      br_hidden = !br_hidden;
      brReload(keep, msg, sizeof(msg));
      break;

    case 'n': case 'N': {
      int isdir = c == 'N';
      char *name = brPrompt(isdir ? "New folder: " : "New file: ", NULL);
      if (name == NULL) break;
      if (!brValidName(name)) {
        snprintf(msg, sizeof(msg), "Not a name for a file: it can't hold a '/'");
      } else if (brPath(path, sizeof(path), name) == -1) {
        snprintf(msg, sizeof(msg), "That path is too long");
      } else if (isdir ? mkdir(path, 0777) == -1 : 0) {
        snprintf(msg, sizeof(msg), "Can't make %s: %s", name, strerror(errno));
      } else {
        int ok = 1;
        if (!isdir) {
          int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
          if (fd == -1) {
            snprintf(msg, sizeof(msg), "Can't make %s: %s", name, strerror(errno));
            ok = 0;
          } else {
            close(fd);
          }
        }
        if (ok) {
          brReload(name, msg, sizeof(msg));
          if (msg[0] == '\0')
            snprintf(msg, sizeof(msg), "Made %s%s", name,
                     name[0] == '.' && !br_hidden ? " (hidden: press . to see it)" : "");
        }
      }
      free(name);
      break;
    }

    case 'r': {
      char *name;
      if (e == NULL || strcmp(e->name, "..") == 0) break;
      snprintf(keep, sizeof(keep), "%s", e->name);
      name = brPrompt("Rename to: ", keep);
      if (name == NULL || strcmp(name, keep) == 0) {
        free(name);
        break;
      }
      if (!brValidName(name)) {
        snprintf(msg, sizeof(msg), "Not a name for a file: it can't hold a '/'");
      } else {
        char from[PATH_MAX + NAME_MAX + 2];
        struct stat st;
        if (brPath(from, sizeof(from), keep) == -1 ||
            brPath(path, sizeof(path), name) == -1) {
          snprintf(msg, sizeof(msg), "That path is too long");
        } else if (lstat(path, &st) == 0) {
          snprintf(msg, sizeof(msg), "%s already exists", name);
        } else if (rename(from, path) == -1) {
          snprintf(msg, sizeof(msg), "Can't rename: %s", strerror(errno));
        } else {
          brReload(name, msg, sizeof(msg));
          if (msg[0] == '\0') snprintf(msg, sizeof(msg), "Renamed to %s", name);
        }
      }
      free(name);
      break;
    }

    case 'd': case DEL_KEY: {
      char line[NAME_MAX + 96];
      int full;
      if (e == NULL || strcmp(e->name, "..") == 0) break;
      if (brPath(path, sizeof(path), e->name) == -1) {
        snprintf(msg, sizeof(msg), "That path is too long");
        break;
      }
      full = e->isdir && !e->islink && !brDirIsEmpty(path);
      snprintf(keep, sizeof(keep), "%s", e->name);
      if (full)
        snprintf(line, sizeof(line),
                 "Delete folder '%s' and EVERYTHING in it? (y/n)", keep);
      else
        snprintf(line, sizeof(line), "Delete '%s'? (y/n)", keep);
      if (!brConfirm(line)) {
        snprintf(msg, sizeof(msg), "Kept %s", keep);
        break;
      }
      if (brRemoveTree(path, 0) == -1) {
        snprintf(msg, sizeof(msg), "Can't delete %s: %s", keep, strerror(errno));
        brReload(NULL, line, sizeof(line));
      } else {
        brReload(NULL, msg, sizeof(msg));
        if (msg[0] == '\0') snprintf(msg, sizeof(msg), "Deleted %s", keep);
      }
      break;
    }

    case '?': case OSHI_KEY_HELP:
      brHelp();
      break;

    case MOUSE_KEY: {
      int button = E.mouse_button;
      int idx = br_top + (E.mouse_y - 2);
      if (button & 64) {
        int step = (button & 1) ? 3 : -3;
        br_sel += step;
        if (br_sel < 0) br_sel = 0;
        if (br_sel > br_n - 1) br_sel = br_n > 0 ? br_n - 1 : 0;
      } else if (!E.mouse_release && (button & ~(4 | 8 | 16)) == 0 &&
                 E.mouse_y >= 2 && E.mouse_y < 2 + brListHeight() &&
                 idx >= 0 && idx < br_n) {
        long now = editorNowMs();
        br_sel = idx;
        if (idx == last_click_idx && now - last_click_ms <= OSHI_CLICK_MS)
          do_open = 1;
        last_click_idx = idx;
        last_click_ms = now;
      }
      break;
    }
    }

    if (do_open) {
      e = br_n > 0 ? &br_ent[br_sel] : NULL;
      if (e == NULL) continue;
      if (e->isdir) {
        if (strcmp(e->name, "..") == 0) {
          brUp(msg, sizeof(msg));
        } else if (brPath(path, sizeof(path), e->name) == -1) {
          snprintf(msg, sizeof(msg), "That path is too long");
        } else {
          brGo(path, NULL, msg, sizeof(msg));
        }
      } else if (brPath(path, sizeof(path), e->name) == -1) {
        snprintf(msg, sizeof(msg), "That path is too long");
      } else {
        struct stat st;
        if (stat(path, &st) == -1) {
          snprintf(msg, sizeof(msg), "Can't open %s: %s", e->name, strerror(errno));
        } else if (!S_ISREG(st.st_mode)) {
          snprintf(msg, sizeof(msg), "%s isn't a regular file", e->name);
        } else if (access(path, R_OK) == -1) {
          snprintf(msg, sizeof(msg), "Can't read %s: %s", e->name, strerror(errno));
        } else {
          char *out = strdup(path);
          if (out == NULL) die("strdup");
          snprintf(br_last, sizeof(br_last), "%s", e->name);
          E.redraw = 1;
          return out;
        }
      }
    }
  }
}

static void editorResetBuffer(void) {
  int i;
  for (i = 0; i < E.numrows; i++) editorFreeRow(&E.row[i]);
  free(E.row);
  E.row = NULL;
  E.numrows = 0;
  E.rowcap = 0;
  free(E.filename);
  E.filename = NULL;
  editorUndoClear();
  editorFindClear();
  E.cx = E.cy = E.rx = E.goal_rx = 0;
  E.rowoff = E.coloff = 0;
  E.sel = 0;
  E.crlf = 0;
  E.quit_confirm = 0;
  E.mouse_click_n = 0;
  E.statusmsg[0] = '\0';
  editorSnapshot();
  E.dirty = 0;
}

void editorLeave(void) {
  char *path;
  if (!br_active) editorQuit();
  editorResetBuffer();
  path = editorBrowse(br_dir, br_last);
  if (path == NULL) editorQuit();
  editorOpen(path);
  free(path);
}
