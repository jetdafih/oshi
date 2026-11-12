#define _GNU_SOURCE

#include "oshi.h"

static void editorClipboardPut(const char *buf, int len) {
  static const char table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  char *b64 = xmalloc(4 * ((len + 2) / 3) + 1);

  int i, out = 0;
  for (i = 0; i + 2 < len; i += 3) {
    unsigned int n = ((unsigned char)buf[i] << 16) |
                     ((unsigned char)buf[i + 1] << 8) |
                      (unsigned char)buf[i + 2];
    b64[out++] = table[(n >> 18) & 63];
    b64[out++] = table[(n >> 12) & 63];
    b64[out++] = table[(n >> 6) & 63];
    b64[out++] = table[n & 63];
  }
  if (i < len) {
    unsigned int n = (unsigned char)buf[i] << 16;
    b64[out++] = table[(n >> 18) & 63];
    if (i + 1 < len) {
      n |= (unsigned char)buf[i + 1] << 8;
      b64[out++] = table[(n >> 12) & 63];
      b64[out++] = table[(n >> 6) & 63];
      b64[out++] = '=';
    } else {
      b64[out++] = table[(n >> 12) & 63];
      b64[out++] = '=';
      b64[out++] = '=';
    }
  }

  editorWriteAll(STDOUT_FILENO, "\x1b]52;c;", 7);
  editorWriteAll(STDOUT_FILENO, b64, out);
  editorWriteAll(STDOUT_FILENO, "\x07", 1);

  free(b64);
}

static int editorClipboardExternal(const char *text, int len) {
  static const char *cmds[] = {
    "wl-copy",
    "xclip -selection clipboard",
    "xsel --clipboard --input",
    "pbcopy",
    "clip.exe",
    NULL
  };
  int i;
  for (i = 0; cmds[i]; i++) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "%s >/dev/null 2>/dev/null", cmds[i]);
    FILE *fp = popen(cmd, "w");
    if (!fp) continue;
    size_t written = fwrite(text, 1, len, fp);
    int rc = pclose(fp);
    if (written == (size_t)len && rc == 0) return 0;
  }
  return -1;
}

static char *editorCopyGrab(int *len, int *isline) {
  if (E.sel) {
    *isline = 0;
    return editorSelectionToString(len);
  }
  if (E.numrows == 0) return NULL;
  erow *row = &E.row[E.cy < E.numrows ? E.cy : E.numrows - 1];
  char *text = xmalloc(row->size + 2);
  memcpy(text, row->chars, row->size);
  text[row->size] = '\n';
  *len = row->size + 1;
  *isline = 1;
  return text;
}

static void editorClipSet(char *text, int len, int isline, const char *verb) {
  int n = editorLineCount(text, len);
  int pushed = 0, shared = 0;
  free(E.clip);
  E.clip = text;
  E.clip_len = len;
  E.clip_line = isline;
  E.clip_shared = 0;
  if (len <= OSHI_OSC52_MAX) {
    editorClipboardPut(text, len);
    pushed = 1;
  } else if (editorClipboardExternal(text, len) == 0) {
    pushed = 1;
  }
  if (pushed) {
    int tlen = len, tnl = (len > 0 && text[len - 1] == '\n');
    char *trim = NULL;
    if (tnl) {
      trim = xmalloc((size_t)len);
      memcpy(trim, text, (size_t)len - 1);
      trim[len - 1] = '\0';
      tlen = len - 1;
    }
    int tries;
    for (tries = 0; tries < 3 && !shared; tries++) {
      int got = 0;
      char *back = editorClipboardRead(&got);
      if (back) {
        if (got == tlen && memcmp(back, trim ? trim : text, (size_t)tlen) == 0)
          shared = 1;
        else if (got == len && memcmp(back, text, (size_t)len) == 0)
          shared = 1;
        else if (got > 0)
          break;
        free(back);
      } else {
        break;
      }
      if (!shared && tries < 2) usleep(OSHI_CLIP_SETTLE_US);
    }
    free(trim);
    E.clip_shared = shared;
  }
  if (len <= OSHI_OSC52_MAX)
    editorSetStatusMessage("%s %d line%s", verb, n, n == 1 ? "" : "s");
  else if (pushed)
    editorSetStatusMessage("%s %d lines", verb, n);
  else {
    editorSetStatusMessage("%s %d lines: clipboard failed, ctrl-v ok", verb, n);
  }
}

void editorCopySelection(void) {
  int len = 0, isline = 0;
  char *text = editorCopyGrab(&len, &isline);
  if (text == NULL || len == 0) {
    free(text);
    editorSetStatusMessage("Nothing to copy");
  } else {
    editorClipSet(text, len, isline, "Copied");
  }
}

void editorCut(void) {
  int len = 0, isline = 0;
  char *text = editorCopyGrab(&len, &isline);
  if (text == NULL || len == 0) {
    free(text);
    editorSetStatusMessage("Nothing to cut");
    E.sel = 0;
    return;
  }
  editorClipSet(text, len, isline, "Cut");
  if (isline) editorDeleteLine();
  else editorDeleteSelection();
}

char *editorWhich(const char *name) {
  const char *p = getenv("PATH");
  if (!p) return NULL;
  for (;;) {
    const char *end = strchr(p, ':');
    size_t dirlen = end ? (size_t)(end - p) : strlen(p);
    if (dirlen > 0 && p[0] == '/') {
      char full[4096];
      struct stat st;
      int n = snprintf(full, sizeof(full), "%.*s/%s", (int)dirlen, p, name);
      if (n > 0 && (size_t)n < sizeof(full) && access(full, X_OK) == 0 &&
          stat(full, &st) == 0 && S_ISREG(st.st_mode)) {
        char *copy = strdup(full);
        if (copy == NULL) die("strdup");
        return copy;
      }
    }
    if (!end) return NULL;
    p = end + 1;
  }
}

int editorInPath(const char *name) {
  char *p = editorWhich(name);
  int found = p != NULL;
  free(p);
  return found;
}

static const char *editorClipTimeout(void) {
  static const char *tmo;
  static int known;
  if (!known) {
    tmo = editorInPath("timeout") ? "timeout"
        : editorInPath("gtimeout") ? "gtimeout" : NULL;
    known = 1;
  }
  return tmo;
}

static int editorCmdAvailable(const char *cmd) {
  char bin[64];
  size_t n = strcspn(cmd, " ");
  if (n == 0 || n >= sizeof(bin)) return 0;
  memcpy(bin, cmd, n);
  bin[n] = '\0';
  return editorInPath(bin);
}

static char *editorClipSlurp(const char *cmd, int secs, int risky, size_t *len) {
  char full[300];
  const char *tmo = editorClipTimeout();
  size_t have = 0, cap = 4096;
  char *out;
  FILE *fp;
  int rc, over = 0;

  if (!editorCmdAvailable(cmd)) return NULL;
  if (risky && !tmo) return NULL;
  if (tmo) snprintf(full, sizeof(full), "%s %d %s 2>/dev/null", tmo, secs, cmd);
  else snprintf(full, sizeof(full), "%s 2>/dev/null", cmd);
  fp = popen(full, "r");
  if (!fp) return NULL;
  out = xmalloc(cap + 1);
  for (;;) {
    size_t n;
    if (have + 512 > cap) {
      if (cap >= OSHI_PASTE_MAX) { over = 1; break; }
      cap *= 2;
      if (cap > OSHI_PASTE_MAX) cap = OSHI_PASTE_MAX;
      out = xrealloc(out, cap + 1);
    }
    n = fread(out + have, 1, cap - have, fp);
    if (n == 0) break;
    have += n;
  }
  out[have] = '\0';
  rc = pclose(fp);
  if (over || rc != 0 || have == 0) {
    free(out);
    return NULL;
  }
  *len = have;
  return out;
}

char *editorClipboardRead(int *len) {
  static const struct { const char *cmd; int risky; } cmds[] = {
    { "wl-paste -n", 1 },
    { "xclip -selection clipboard -o", 1 },
    { "xsel --clipboard --output", 1 },
    { "pbpaste", 0 },
    { "powershell.exe -NoProfile -Command \"Get-Clipboard -Raw\"", 0 },
    { NULL, 0 }
  };
  int i;
  for (i = 0; cmds[i].cmd; i++) {
    size_t n = 0;
    char *out = editorClipSlurp(cmds[i].cmd, OSHI_CLIP_TIMEOUT, cmds[i].risky, &n);
    if (out) {
      *len = (int)n;
      return out;
    }
  }
  *len = 0;
  return NULL;
}

static const struct { const char *mime; const char *ext; } editorImgTypes[] = {
  { "image/png", "png" },
  { "image/jpeg", "jpg" },
  { "image/webp", "webp" },
  { "image/gif", "gif" },
  { "image/bmp", "bmp" },
  { NULL, NULL }
};

static int editorImgPick(const char *types) {
  int i;
  for (i = 0; editorImgTypes[i].mime; i++) {
    size_t ml = strlen(editorImgTypes[i].mime);
    const char *p = types;
    while ((p = strstr(p, editorImgTypes[i].mime)) != NULL) {
      char after = p[ml];
      if ((p == types || p[-1] == '\n') &&
          (after == '\0' || after == '\n' || after == '\r'))
        return i;
      p += ml;
    }
  }
  return -1;
}

char *editorClipboardImage(int *len, const char **ext) {
  static const struct { const char *list; const char *get; } tools[] = {
    { "wl-paste --list-types", "wl-paste --type %s" },
    { "xclip -selection clipboard -t TARGETS -o",
      "xclip -selection clipboard -t %s -o" },
  };
  size_t i, n = 0;
  char *data;

  *len = 0;
  for (i = 0; i < sizeof(tools) / sizeof(tools[0]); i++) {
    size_t tn = 0, dn = 0;
    char get[160];
    char *types = editorClipSlurp(tools[i].list, OSHI_CLIP_TIMEOUT, 1, &tn);
    int k;
    if (types == NULL) continue;
    k = editorImgPick(types);
    free(types);
    if (k < 0) continue;
    snprintf(get, sizeof(get), tools[i].get, editorImgTypes[k].mime);
    data = editorClipSlurp(get, OSHI_IMG_TIMEOUT, 1, &dn);
    if (data) {
      *len = (int)dn;
      *ext = editorImgTypes[k].ext;
      return data;
    }
  }
  data = editorClipSlurp("pngpaste -", OSHI_IMG_TIMEOUT, 0, &n);
  if (data && n > 8 && memcmp(data, "\x89PNG", 4) == 0) {
    *len = (int)n;
    *ext = "png";
    return data;
  }
  free(data);
  return NULL;
}

void editorPasteInternal(void) {
  char *text = E.clip;
  int len = E.clip_len;
  int isline = E.clip_line;
  int free_text = 0;

  if (text != NULL && len > 0 && E.clip_shared) {
    int got = 0;
    char *ext = editorClipboardRead(&got);
    int same = 0;
    if (ext) {
      if (got == len && memcmp(ext, text, (size_t)len) == 0)
        same = 1;
      else if (len > 0 && text[len - 1] == '\n' && got == len - 1 &&
               memcmp(ext, text, (size_t)got) == 0)
        same = 1;
      if (!same && got > 0) {
        text = ext;
        len = got;
        isline = 0;
        free_text = 1;
      } else {
        free(ext);
      }
    }
  }

  if (text == NULL || len == 0) {
    text = editorClipboardRead(&len);
    free_text = 1;
    if (text == NULL || len == 0) {
      editorSetStatusMessage("Nothing to paste");
      free(text);
      return;
    }
    isline = 0;
  }

  int n = editorLineCount(text, len);
  if (isline && !E.sel) {
    int at = E.cy < E.numrows ? E.cy : 0;
    int had_rows = E.numrows > 0;
    editorUndoBegin(at, 0);
    editorInsertRow(at, text, len - 1);
    if (had_rows) E.cy++;
    editorUndoCommit();
  } else {
    n = editorInsertText(text, len);
    if (n == 0) {
      editorSetStatusMessage("Nothing to paste");
      if (free_text) free(text);
      return;
    }
  }
  editorSetStatusMessage("Pasted %d line%s", n, n == 1 ? "" : "s");
  if (free_text) free(text);
}
