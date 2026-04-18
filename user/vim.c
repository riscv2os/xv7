#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define VIM_VERSION "0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

enum {
  MODE_NORMAL = 0,
  MODE_INSERT = 1,
  MODE_COMMAND = 2,
};

enum {
  KEY_ARROW_LEFT = 1000,
  KEY_ARROW_RIGHT,
  KEY_ARROW_UP,
  KEY_ARROW_DOWN,
};

struct erow {
  int size;
  char *chars;
  int dirty;
};

struct editorConfig {
  int cx, cy;
  int rowoff, coloff;
  int last_rowoff, last_coloff;
  int screenrows, screencols;
  int numrows;
  struct erow *row;
  int dirty;
  int mode;
  char *filename;
  char statusmsg[80];
  char cmdline[80];
  int cmdlen;
  int quit_times;
  int saved_mode;
  int screen_dirty;
};

static struct editorConfig E;

static void editor_quit(void);
static void append_num(char *buf, int *pos, int max, int num);

struct abuf {
  char *b;
  int len;
};

static void
ab_append(struct abuf *ab, const char *s, int len)
{
  if(len <= 0)
    return;
  char *newbuf = malloc(ab->len + len);
  if(newbuf == 0)
    return;
  if(ab->len)
    memmove(newbuf, ab->b, ab->len);
  memmove(newbuf + ab->len, s, len);
  free(ab->b);
  ab->b = newbuf;
  ab->len += len;
}

static void
ab_free(struct abuf *ab)
{
  if(ab->b)
    free(ab->b);
}

static void
str_copy(char *dst, int max, const char *src)
{
  int i = 0;
  if(max <= 0)
    return;
  while(src[i] && i < max - 1){
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

static char*
str_dup(const char *s)
{
  int len = strlen(s);
  char *p = malloc(len + 1);
  if(p == 0)
    return 0;
  memmove(p, s, len);
  p[len] = '\0';
  return p;
}

static void*
mem_realloc(void *p, int oldlen, int newlen)
{
  void *np = malloc(newlen);
  if(np == 0)
    return 0;
  if(p){
    int copylen = oldlen < newlen ? oldlen : newlen;
    if(copylen > 0)
      memmove(np, p, copylen);
    free(p);
  }
  return np;
}

static void
editor_set_status(const char *msg)
{
  str_copy(E.statusmsg, sizeof(E.statusmsg), msg);
}

static void
editor_init(void)
{
  E.cx = 0;
  E.cy = 0;
  E.rowoff = 0;
  E.coloff = 0;
  E.last_rowoff = -1;
  E.last_coloff = -1;
  E.screenrows = 23; // 24 rows - 1 status line
  E.screencols = 80;
  E.numrows = 0;
  E.row = 0;
  E.dirty = 0;
  E.mode = MODE_NORMAL;
  E.filename = 0;
  E.statusmsg[0] = '\0';
  E.cmdline[0] = '\0';
  E.cmdlen = 0;
  E.quit_times = 1;
  E.saved_mode = -1;
  E.screen_dirty = 1;
}

static void
editor_free_row(struct erow *row)
{
  if(row->chars)
    free(row->chars);
}

static void
editor_row_insert_char(struct erow *row, int at, int c)
{
  if(at < 0 || at > row->size)
    at = row->size;
  row->chars = mem_realloc(row->chars, row->size + 1, row->size + 2);
  if(row->chars == 0)
    return;
  if(at < row->size)
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at);
  row->chars[at] = c;
  row->size++;
  row->chars[row->size] = '\0';
  row->dirty = 1;
}

static void
editor_row_del_char(struct erow *row, int at)
{
  if(at < 0 || at >= row->size)
    return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at - 1);
  row->size--;
  row->chars[row->size] = '\0';
  row->dirty = 1;
}

static void
editor_insert_row(int at, const char *s, int len)
{
  int i;
  if(at < 0 || at > E.numrows)
    return;
  E.row = mem_realloc(E.row, sizeof(struct erow) * E.numrows,
                      sizeof(struct erow) * (E.numrows + 1));
  if(E.row == 0)
    return;
  for(i = E.numrows; i > at; i--)
    E.row[i] = E.row[i - 1];
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  if(E.row[at].chars == 0){
    E.row[at].size = 0;
    E.row[at].dirty = 1;
  } else {
    memmove(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.row[at].dirty = 1;
  }
  E.numrows++;
  E.dirty = 1;
  E.screen_dirty = 1;
}

static void
editor_del_row(int at)
{
  int i;
  if(at < 0 || at >= E.numrows)
    return;
  editor_free_row(&E.row[at]);
  for(i = at; i < E.numrows - 1; i++)
    E.row[i] = E.row[i + 1];
  E.numrows--;
  if(E.numrows == 0){
    free(E.row);
    E.row = 0;
  }
  E.dirty = 1;
  E.screen_dirty = 1;
}

static void
editor_insert_char(int c)
{
  if(E.cy == E.numrows)
    editor_insert_row(E.numrows, "", 0);
  editor_row_insert_char(&E.row[E.cy], E.cx, c);
  E.cx++;
  E.dirty = 1;
  E.screen_dirty = 1;
}

static void
editor_insert_newline(void)
{
  if(E.cx == 0){
    editor_insert_row(E.cy, "", 0);
  } else {
    struct erow *row = &E.row[E.cy];
    editor_insert_row(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
    row = &E.row[E.cy];
    row->size = E.cx;
    row->chars[row->size] = '\0';
    row->dirty = 1;
  }
  E.cy++;
  E.cx = 0;
  E.screen_dirty = 1;
}

static void
editor_del_char(void)
{
  if(E.cy >= E.numrows)
    return;
  if(E.cx == 0 && E.cy == 0)
    return;
  struct erow *row = &E.row[E.cy];
  if(E.cx > 0){
    editor_row_del_char(row, E.cx - 1);
    E.cx--;
    E.dirty = 1;
    E.screen_dirty = 1;
  } else {
    int prevlen = E.row[E.cy - 1].size;
    struct erow *prev = &E.row[E.cy - 1];
    prev->chars = mem_realloc(prev->chars, prev->size + 1, prev->size + row->size + 1);
    if(prev->chars == 0)
      return;
    memmove(&prev->chars[prev->size], row->chars, row->size);
    prev->size += row->size;
    prev->chars[prev->size] = '\0';
    prev->dirty = 1;
    editor_del_row(E.cy);
    E.cy--;
    E.cx = prevlen;
    E.dirty = 1;
    E.screen_dirty = 1;
  }
}

static int
editor_read_key(void)
{
  static char buf[64];
  static int buflen = 0;
  static int bufpos = 0;
  static int esc_state = 0; // 0 none, 1 saw ESC, 2 saw ESC [
  static uint esc_tick = 0;
  static int pushback = -1;
  int n;
  char c;

  while(1){
    if(esc_state != 0 && bufpos >= buflen){
      if(uptime() - esc_tick > 5){
        esc_state = 0;
        return '\x1b';
      }
      sleep(1);
      continue;
    }
    if(pushback >= 0){
      c = pushback;
      pushback = -1;
    } else {
      if(bufpos >= buflen){
        n = read(0, buf, sizeof(buf));
        if(n <= 0)
          continue;
        buflen = n;
        bufpos = 0;
      }
      c = buf[bufpos++];
    }

    if(esc_state == 1){
      if(c == '['){
        esc_state = 2;
        continue;
      } else {
        esc_state = 0;
        pushback = c;
        return '\x1b';
      }
    } else if(esc_state == 2){
      esc_state = 0;
      switch(c){
      case 'A': return KEY_ARROW_UP;
      case 'B': return KEY_ARROW_DOWN;
      case 'C': return KEY_ARROW_RIGHT;
      case 'D': return KEY_ARROW_LEFT;
      default:
        return '\x1b';
      }
    }

    if(c == '\x1b'){
      esc_state = 1;
      esc_tick = uptime();
      continue;
    }
    return c;
  }
}

static void
editor_scroll(void)
{
  if(E.cy < E.rowoff)
    E.rowoff = E.cy;
  if(E.cy >= E.rowoff + E.screenrows)
    E.rowoff = E.cy - E.screenrows + 1;
  if(E.cx < E.coloff)
    E.coloff = E.cx;
  if(E.cx >= E.coloff + E.screencols)
    E.coloff = E.cx - E.screencols + 1;
}

static int
editor_row_cx_to_rx(struct erow *row, int cx)
{
  if(cx > row->size)
    cx = row->size;
  return cx;
}

static void
editor_draw_rows(struct abuf *ab)
{
  int y;
  for(y = 0; y < E.screenrows; y++){
    int filerow = y + E.rowoff;
    if(filerow >= E.numrows){
      ab_append(ab, "~", 1);
    } else {
      int len = E.row[filerow].size - E.coloff;
      if(len < 0) len = 0;
      if(len > E.screencols) len = E.screencols;
      if(len > 0)
        ab_append(ab, &E.row[filerow].chars[E.coloff], len);
    }
    ab_append(ab, "\x1b[K", 3);
    ab_append(ab, "\r\n", 2);
  }
}

static void
editor_draw_row_at(int screen_row, int filerow, struct abuf *ab)
{
  char buf[32];
  int len = 0;

  buf[len++] = '\x1b';
  buf[len++] = '[';
  append_num(buf, &len, sizeof(buf), screen_row);
  if(len < (int)sizeof(buf) - 1)
    buf[len++] = ';';
  append_num(buf, &len, sizeof(buf), 1);
  if(len < (int)sizeof(buf) - 1)
    buf[len++] = 'H';
  buf[len] = '\0';
  ab_append(ab, buf, len);

  if(filerow >= E.numrows){
    ab_append(ab, "~", 1);
  } else {
    int clen = E.row[filerow].size - E.coloff;
    if(clen < 0) clen = 0;
    if(clen > E.screencols) clen = E.screencols;
    if(clen > 0)
      ab_append(ab, &E.row[filerow].chars[E.coloff], clen);
  }
  ab_append(ab, "\x1b[K", 3);
}

static void
append_num(char *buf, int *pos, int max, int num)
{
  char tmp[16];
  int len = 0;
  int i;
  if(num == 0){
    tmp[len++] = '0';
  } else {
    while(num > 0 && len < (int)sizeof(tmp)){
      tmp[len++] = '0' + (num % 10);
      num /= 10;
    }
  }
  for(i = len - 1; i >= 0; i--){
    if(*pos >= max - 1)
      break;
    buf[*pos] = tmp[i];
    (*pos)++;
  }
  buf[*pos] = '\0';
}

static void
editor_draw_status(struct abuf *ab)
{
  char line[80];
  int i;
  int limit = E.screencols;
  if(limit > (int)sizeof(line)) limit = sizeof(line);
  for(i = 0; i < limit; i++)
    line[i] = ' ';

  if(E.mode == MODE_COMMAND){
    int pos = 0;
    if(pos < limit) line[pos++] = ':';
    for(i = 0; i < E.cmdlen && pos < limit; i++)
      line[pos++] = E.cmdline[i];
  } else if(E.mode == MODE_INSERT){
    const char *ins = "-- INSERT --";
    for(i = 0; ins[i] && i < limit; i++)
      line[i] = ins[i];
  } else if(E.statusmsg[0]){
    for(i = 0; E.statusmsg[i] && i < limit; i++)
      line[i] = E.statusmsg[i];
  }

  ab_append(ab, line, limit);
  ab_append(ab, "\x1b[K", 3);
}


static void
editor_refresh_screen(void)
{
  struct abuf ab = {0, 0};
  char buf[32];
  int rx;
  int force_full;
  int clear_screen;

  editor_scroll();

  // Force full redraw in insert mode to keep display in sync.
  force_full = (E.mode == MODE_INSERT);
  clear_screen = (!force_full && E.screen_dirty);
  if(force_full || E.screen_dirty || E.rowoff != E.last_rowoff || E.coloff != E.last_coloff){
    ab_append(&ab, "\x1b[?25l", 6); // hide cursor for full redraw
    if(clear_screen)
      ab_append(&ab, "\x1b[2J", 4);   // clear screen
    ab_append(&ab, "\x1b[H", 3);
    editor_draw_rows(&ab);
    {
      int i;
      for(i = 0; i < E.numrows; i++)
        E.row[i].dirty = 0;
    }
    E.screen_dirty = 0;
  } else {
    int y;
    for(y = 0; y < E.screenrows; y++){
      int filerow = y + E.rowoff;
      if(filerow < E.numrows && E.row[filerow].dirty){
        editor_draw_row_at(y + 1, filerow, &ab);
        E.row[filerow].dirty = 0;
      }
    }
  }

  // Move to status line before drawing it to avoid stacking lines.
  {
    int slen = 0;
    int srow = E.screenrows + 1;
    buf[slen++] = '\x1b';
    buf[slen++] = '[';
    append_num(buf, &slen, sizeof(buf), srow);
    if(slen < (int)sizeof(buf) - 1)
      buf[slen++] = ';';
    append_num(buf, &slen, sizeof(buf), 1);
    if(slen < (int)sizeof(buf) - 1)
      buf[slen++] = 'H';
    buf[slen] = '\0';
    ab_append(&ab, buf, slen);
  }
  editor_draw_status(&ab);

  if(E.cy < E.numrows)
    rx = editor_row_cx_to_rx(&E.row[E.cy], E.cx);
  else
    rx = 0;
  int len = 0;
  int row;
  int col;
  if(E.mode == MODE_COMMAND){
    row = E.screenrows + 1;
    col = 2 + E.cmdlen;
  } else {
    row = (E.cy - E.rowoff) + 1;
    col = (rx - E.coloff) + 1;
  }
  if(row < 1) row = 1;
  if(col < 1) col = 1;
  if(col > E.screencols) col = E.screencols;
  buf[len++] = '\x1b';
  buf[len++] = '[';
  append_num(buf, &len, sizeof(buf), row);
  if(len < (int)sizeof(buf) - 1)
    buf[len++] = ';';
  append_num(buf, &len, sizeof(buf), col);
  if(len < (int)sizeof(buf) - 1)
    buf[len++] = 'H';
  buf[len] = '\0';
  ab_append(&ab, buf, len);

  ab_append(&ab, "\x1b[?25h", 6); // show cursor

  write(1, ab.b, ab.len);
  ab_free(&ab);

  E.last_rowoff = E.rowoff;
  E.last_coloff = E.coloff;
}

static void
editor_move_cursor(int key)
{
  struct erow *row = (E.cy >= E.numrows) ? 0 : &E.row[E.cy];
  switch(key){
  case KEY_ARROW_LEFT:
  case 'h':
    if(E.cx != 0){
      E.cx--;
    } else if(E.cy > 0){
      E.cy--;
      E.cx = E.row[E.cy].size;
    }
    break;
  case KEY_ARROW_RIGHT:
  case 'l':
    if(row && E.cx < row->size){
      E.cx++;
    } else if(row && E.cx == row->size){
      if(E.cy + 1 < E.numrows){
        E.cy++;
        E.cx = 0;
      }
    }
    break;
  case KEY_ARROW_UP:
  case 'k':
    if(E.cy != 0)
      E.cy--;
    break;
  case KEY_ARROW_DOWN:
  case 'j':
    if(E.cy + 1 < E.numrows)
      E.cy++;
    break;
  case '0':
    E.cx = 0;
    break;
  case '$':
    if(row)
      E.cx = row->size;
    break;
  }

  row = (E.cy >= E.numrows) ? 0 : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if(E.cx > rowlen)
    E.cx = rowlen;
}

static void
editor_del_char_at_cursor(void)
{
  if(E.cy >= E.numrows)
    return;
  struct erow *row = &E.row[E.cy];
  if(E.cx < row->size){
    editor_row_del_char(row, E.cx);
    E.dirty = 1;
    E.screen_dirty = 1;
    return;
  }
  if(E.cx == row->size && E.cy + 1 < E.numrows){
    struct erow *next = &E.row[E.cy + 1];
    row->chars = mem_realloc(row->chars, row->size + 1, row->size + next->size + 1);
    if(row->chars == 0)
      return;
    memmove(&row->chars[row->size], next->chars, next->size);
    row->size += next->size;
    row->chars[row->size] = '\0';
    row->dirty = 1;
    editor_del_row(E.cy + 1);
    E.dirty = 1;
    E.screen_dirty = 1;
  }
}

static char*
editor_rows_to_string(int *buflen)
{
  int totlen = 0;
  int j;
  char *buf, *p;
  for(j = 0; j < E.numrows; j++)
    totlen += E.row[j].size + 1;
  *buflen = totlen;
  if(totlen == 0){
    buf = malloc(1);
    return buf;
  }
  buf = malloc(totlen);
  if(buf == 0)
    return 0;
  p = buf;
  for(j = 0; j < E.numrows; j++){
    memmove(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }
  return buf;
}

static void
editor_save(void)
{
  int len;
  char *buf;
  int fd;

  if(E.filename == 0){
    editor_set_status("No file name. Use :w <name>");
    return;
  }

  buf = editor_rows_to_string(&len);
  if(buf == 0){
    editor_set_status("Save failed: out of memory");
    return;
  }

  fd = open(E.filename, O_WRONLY | O_CREATE | O_TRUNC);
  if(fd >= 0){
    int wrote = write(fd, buf, len);
    close(fd);
    free(buf);
    if(wrote == len){
      E.dirty = 0;
      editor_set_status("File saved");
      return;
    }
  }
  free(buf);
  editor_set_status("Save failed");
}

static void
editor_open(const char *filename)
{
  int fd;
  char tmp[256];
  int n;
  char *filebuf = 0;
  int filelen = 0;
  int i;

  E.filename = str_dup(filename);
  fd = open(filename, O_RDONLY);
  if(fd < 0)
    return;

  while((n = read(fd, tmp, sizeof(tmp))) > 0){
    filebuf = mem_realloc(filebuf, filelen, filelen + n);
    if(filebuf == 0){
      close(fd);
      return;
    }
    memmove(filebuf + filelen, tmp, n);
    filelen += n;
  }
  if(filelen > 0){
    int start = 0;
    for(i = 0; i < filelen; i++){
      if(filebuf[i] == '\n'){
        editor_insert_row(E.numrows, &filebuf[start], i - start);
        start = i + 1;
      }
    }
    if(start < filelen)
      editor_insert_row(E.numrows, &filebuf[start], filelen - start);
  }
  if(filebuf)
    free(filebuf);
  close(fd);
  E.dirty = 0;
}

static int
editor_try_quit(int force)
{
  if(E.dirty && !force){
    if(E.quit_times > 0){
      editor_set_status("Unsaved changes. Press Ctrl+Q again or use :q!");
      E.quit_times--;
      return 0;
    }
  }
  return 1;
}

static void
editor_enter_command(void)
{
  E.mode = MODE_COMMAND;
  E.cmdlen = 0;
  E.cmdline[0] = '\0';
  E.statusmsg[0] = '\0';
  E.screen_dirty = 1;
}

static void
editor_exec_command(void)
{
  if(E.cmdlen == 0){
    E.mode = MODE_NORMAL;
    return;
  }
  E.cmdline[E.cmdlen] = '\0';
  if(strcmp(E.cmdline, "q") == 0){
    if(editor_try_quit(0))
      editor_quit();
  } else if(strcmp(E.cmdline, "q!") == 0){
    editor_quit();
  } else if(strcmp(E.cmdline, "w") == 0){
    editor_save();
  } else if(strcmp(E.cmdline, "wq") == 0){
    editor_save();
    if(!E.dirty)
      editor_quit();
  } else if(E.cmdline[0] == 'w' && E.cmdline[1] == ' '){
    char *name = &E.cmdline[2];
    if(name[0]){
      if(E.filename)
        free(E.filename);
      E.filename = str_dup(name);
      editor_save();
    }
  } else {
    editor_set_status("Unknown command");
  }
  E.mode = MODE_NORMAL;
  E.cmdlen = 0;
  E.cmdline[0] = '\0';
  E.screen_dirty = 1;
}

static void
editor_process_keypress(void)
{
  int c = editor_read_key();

  if(c != CTRL_KEY('q'))
    E.quit_times = 1;

  if(E.mode == MODE_INSERT){
    switch(c){
    case '\x1b':
      E.mode = MODE_NORMAL;
      E.screen_dirty = 1;
      return;
    case '\r':
    case '\n':
      editor_insert_newline();
      return;
    case CTRL_KEY('s'):
      editor_save();
      return;
    case CTRL_KEY('q'):
      if(editor_try_quit(0))
        editor_quit();
      return;
    case 127:
    case CTRL_KEY('h'):
      editor_del_char();
      return;
    default:
      // Accept printable ASCII and raw bytes (for UTF-8 input).
      {
        unsigned char uc = (unsigned char)c;
        if(uc >= 32 && uc != 127){
          editor_insert_char(uc);
        }
      }
      return;
    }
  }

  if(E.mode == MODE_COMMAND){
    switch(c){
    case '\x1b':
      E.mode = MODE_NORMAL;
      E.cmdlen = 0;
      E.cmdline[0] = '\0';
      E.screen_dirty = 1;
      return;
    case '\r':
    case '\n':
      editor_exec_command();
      return;
    case 127:
    case CTRL_KEY('h'):
      if(E.cmdlen > 0)
        E.cmdline[--E.cmdlen] = '\0';
      return;
    default:
      if(c >= 32 && c <= 126 && E.cmdlen < (int)sizeof(E.cmdline) - 1){
        E.cmdline[E.cmdlen++] = c;
        E.cmdline[E.cmdlen] = '\0';
        E.screen_dirty = 1;
      }
      return;
    }
  }

  switch(c){
  case 'i':
    E.mode = MODE_INSERT;
    E.screen_dirty = 1;
    return;
  case 'x':
    editor_del_char_at_cursor();
    return;
  case '0':
  case '$':
    editor_move_cursor(c);
    return;
  case ':':
    editor_enter_command();
    return;
  case CTRL_KEY('s'):
    editor_save();
    return;
  case CTRL_KEY('q'):
    if(editor_try_quit(0))
      editor_quit();
    return;
  case KEY_ARROW_LEFT:
  case KEY_ARROW_RIGHT:
  case KEY_ARROW_UP:
  case KEY_ARROW_DOWN:
  case 'h':
  case 'j':
  case 'k':
  case 'l':
    editor_move_cursor(c);
    return;
  case 'd': {
    int next = editor_read_key();
    if(next == 'd'){
      if(E.numrows > 0){
        editor_del_row(E.cy);
        if(E.cy >= E.numrows)
          E.cy = E.numrows - 1;
        if(E.cy < 0)
          E.cy = 0;
        E.cx = 0;
        E.dirty = 1;
      }
    }
    return;
  }
  }
}

static void
editor_cleanup(void)
{
  if(E.saved_mode >= 0)
    consolemode((E.saved_mode & 1) ? 1 : 0, (E.saved_mode & 2) ? 1 : 0);
  // Re-enable line wrap on exit.
  write(1, "\x1b[?7h", 5);
  write(1, "\x1b[2J", 4);
  write(1, "\x1b[H", 3);
}

static void
editor_quit(void)
{
  editor_cleanup();
  exit(0);
}

int
main(int argc, char *argv[])
{
  editor_init();

  E.saved_mode = consolemode(1, 0);
  write(1, "\x1b[2J", 4);
  write(1, "\x1b[H", 3);
  write(1, "\x1b[1;1H", 6);
  // Disable line wrap to prevent long lines from corrupting the screen.
  write(1, "\x1b[?7l", 5);

  if(argc >= 2){
    editor_open(argv[1]);
  }

  // leave status line empty on start

  while(1){
    editor_refresh_screen();
    editor_process_keypress();
  }
}
