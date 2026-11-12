#define _GNU_SOURCE

#include "oshi.h"

char *editorRowsToString(int *buflen) {
  int totlen = 0;
  int j;
  for (j = 0; j < E.numrows; j++)
    totlen += E.row[j].size + (E.crlf ? 2 : 1);
  *buflen = totlen;
  char *buf = xmalloc(totlen);
  char *p = buf;
  for (j = 0; j < E.numrows; j++) {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    if (E.crlf) *p++ = '\r';
    *p++ = '\n';
  }
  return buf;
}

void editorOpen(char *filename) {
  free(E.filename);
  E.filename = strdup(filename);
  if (E.filename == NULL) die("strdup");

  struct stat sb;
  if (stat(filename, &sb) == 0 && S_ISDIR(sb.st_mode)) {
    errno = EISDIR;
    die(filename);
  }

  if (stat(filename, &sb) == 0) {
    if (!S_ISREG(sb.st_mode)) dieMsg(filename, "not a regular file");
    if (sb.st_size > OSHI_FILE_MAX) dieMsg(filename, "file is too large (over 1 GiB)");
  }

  FILE *fp = fopen(filename, "r");
  if (!fp) {
    if (errno == ENOENT) return;
    die(filename);
  }

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  int crlf_lines = 0, lf_lines = 0;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    if (linelen > 0 && line[linelen - 1] == '\n') {
      if (linelen > 1 && line[linelen - 2] == '\r') crlf_lines++;
      else lf_lines++;
    }
    while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) linelen--;
    editorInsertRow(E.numrows, line, linelen);
  }
  free(line);
  fclose(fp);
  E.crlf = crlf_lines > lf_lines;
  editorSnapshot();
  E.dirty = 0;
}

int editorWriteAll(int fd, const char *buf, int len) {
  int written = 0;
  while (written < len) {
    ssize_t n = write(fd, buf + written, len - written);
    if (n == -1) {
      if (errno == EINTR) continue;
      return -1;
    }
    if (n == 0) return -1;
    written += (int)n;
  }
  return 0;
}

static int editorWriteInPlace(const char *path, const char *buf, int len) {
  int rc = -1, err = 0;
  int fd = open(path, O_WRONLY | O_CREAT, 0644);
  if (fd == -1) return -1;
  if (editorWriteAll(fd, buf, len) == 0 && ftruncate(fd, len) == 0) rc = 0;
  else err = errno;
  if (close(fd) == -1 && rc == 0) { rc = -1; err = errno; }
  errno = err;
  return rc;
}

static int editorWriteFile(const char *name, const char *buf, int len) {
  char *real = realpath(name, NULL);
  const char *path = real ? real : name;
  struct stat st;
  int have = stat(path, &st) == 0;
  int rc = -1, err = 0;

  if (!have || (S_ISREG(st.st_mode) && st.st_nlink == 1)) {
    size_t n = strlen(path) + 32;
    char *tmp = xmalloc(n);
    int fd;
    snprintf(tmp, n, "%s.oshi-tmp-%ld", path, (long)getpid());
    fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd != -1) {
      int keep = 1;
      if (have) {
        if (fchown(fd, st.st_uid, st.st_gid) == -1) keep = 0;
        else if (fchmod(fd, st.st_mode & 07777) == -1) keep = 0;
      }
      if (keep) {
        if (editorWriteAll(fd, buf, len) == 0 && fsync(fd) == 0) rc = 0;
        else err = errno;
        if (close(fd) == -1 && rc == 0) { rc = -1; err = errno; }
        if (rc == 0 && rename(tmp, path) == -1) { rc = -1; err = errno; }
        if (rc == -1) unlink(tmp);
        free(tmp);
        free(real);
        errno = err;
        return rc;
      }
      close(fd);
      unlink(tmp);
    }
    free(tmp);
  }

  rc = editorWriteInPlace(path, buf, len);
  err = errno;
  free(real);
  errno = err;
  return rc;
}

static void editorMarkSaved(void) {
  editorSnapshot();
  editorUndoRunEnd();
  editorUpdateDirty();
}

char *editorParentDir(const char *path) {
  const char *slash = strrchr(path, '/');
  size_t n;
  char *d;
  if (!slash) return NULL;
  n = (size_t)(slash - path);
  if (n == 0) return NULL;
  d = xmalloc(n + 1);
  memcpy(d, path, n);
  d[n] = '\0';
  while (n > 1 && d[n - 1] == '/') d[--n] = '\0';
  return d;
}

int editorIsDir(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int editorMkdirP(const char *dir) {
  char *tmp = strdup(dir);
  size_t n, i;
  if (tmp == NULL) die("strdup");
  n = strlen(tmp);
  for (i = 1; i <= n; i++) {
    if (tmp[i] == '/' || tmp[i] == '\0') {
      char keep = tmp[i];
      tmp[i] = '\0';
      if (mkdir(tmp, 0777) == -1 && errno != EEXIST) {
        int e = errno;
        free(tmp);
        errno = e;
        return -1;
      }
      tmp[i] = keep;
    }
  }
  free(tmp);
  if (!editorIsDir(dir)) { errno = ENOTDIR; return -1; }
  return 0;
}

static int editorConfirm(const char *fmt, const char *arg) {
  for (;;) {
    int k;
    editorSetStatusMessage(fmt, arg);
    editorRefreshScreen();
    k = editorReadKey();
    if (k == 'y' || k == 'Y' || k == '\r') return 1;
    if (k == 'n' || k == 'N' || k == '\x1b' || k == CTRL_KEY('q')) return 0;
    if (k == PASTE_KEY) editorSkipPaste();
  }
}

void editorSave(void) {
  if (E.filename == NULL) {
    E.filename = editorPrompt("Save as: %s ", NULL, NULL);
    if (E.filename == NULL) {
      editorSetStatusMessage("Save aborted");
      return;
    }
  }

  int len;
  char *buf = editorRowsToString(&len);
  int rc = editorWriteFile(E.filename, buf, len);
  int err = errno;
  if (rc != 0 && err == ENOENT) {
    char *dir = editorParentDir(E.filename);
    if (dir && !editorIsDir(dir)) {
      if (!editorConfirm("Directory '%s' doesn't exist. Create it? (y/n)", dir)) {
        free(dir);
        free(buf);
        editorSetStatusMessage("Save cancelled");
        return;
      }
      if (editorMkdirP(dir) == 0) {
        rc = editorWriteFile(E.filename, buf, len);
        err = errno;
      } else {
        err = errno;
      }
    }
    free(dir);
  }
  if (rc == 0) {
    free(buf);
    editorMarkSaved();
    editorSetStatusMessage("%d bytes written to disk", len);
    return;
  }
  if (err == EACCES || err == EPERM) {
    if (editorSaveElevated(buf, len) == 0) editorMarkSaved();
    free(buf);
    return;
  }
  free(buf);
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(err));
}
