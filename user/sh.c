// Shell with command history support.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4
#define BACK  5

#define MAXARGS 10
#define HIST_SIZE 16
#define HIST_LEN  128

struct cmd {
  int type;
};

struct execcmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};

struct redircmd {
  int type;
  struct cmd *cmd;
  char *file;
  char *efile;
  int mode;
  int fd;
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct listcmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct backcmd {
  int type;
  struct cmd *cmd;
};

static char history[HIST_SIZE][HIST_LEN];
static int hist_count = 0;
static int hist_index = 0;
static int hist_pos = 0;

int fork1(void);
void panic(char*);
struct cmd *parsecmd(char*);
void runcmd(struct cmd*) __attribute__((noreturn));
struct cmd *execcmd(void);
struct cmd *redircmd(struct cmd*, char*, char*, int, int);
struct cmd *pipecmd(struct cmd*, struct cmd*);
struct cmd *listcmd(struct cmd*, struct cmd*);
struct cmd *backcmd(struct cmd*);

void
add_history(char *cmd)
{
  int fd;
  struct stat st;
  if(cmd[0] == 0) return;
  if(hist_count > 0 && strcmp(history[(hist_count-1) % HIST_SIZE], cmd) == 0) return;
  memmove(history[hist_count % HIST_SIZE], cmd, HIST_LEN);
  hist_count++;
  hist_index = hist_count;
  hist_pos = 0;

  fd = open(".sh_history", O_RDWR|O_CREATE);
  if(fd >= 0){
    if(fstat(fd, &st) == 0){
      char *p = malloc(st.size + strlen(cmd) + 1);
      int n = read(fd, p, st.size);
      p[n] = 0;
      close(fd);
      fd = open(".sh_history", O_WRONLY|O_CREATE|O_TRUNC);
      if(fd >= 0){
        write(fd, p, n);
        write(fd, cmd, strlen(cmd));
        close(fd);
      }
      free(p);
    } else {
      write(fd, cmd, strlen(cmd));
      close(fd);
    }
  }
}

int
getcmd_with_history(char *buf, int nbuf)
{
  int i, cc;
  char c;
  int escape_state = 0;
  char escape_buf[8];
  int escape_len = 0;
  int prev_mode;
  int ret;

  prev_mode = consolemode(1, 0);

  write(2, "$ ", 2);
  memset(buf, 0, nbuf);
  i = strlen(buf);

get_next_char:
  cc = read(0, &c, 1);
  if(cc < 1) goto done;

  if(c == '\x1b'){
    escape_state = 1;
    escape_len = 0;
    goto get_next_char;
  }

  if(escape_state == 1){
    escape_buf[escape_len++] = c;
    if(c == '['){
      goto get_next_char;
    }
    if(c == 'A' || c == 'B'){
      if(c == 'A'){
        if(hist_index > 0) hist_index--;
      } else {
        if(hist_index < hist_count) hist_index++;
      }

      while(i > 0){
        write(2, "\b \b", 3);
        i--;
      }
      memset(buf, 0, nbuf);
      if(hist_index < hist_count){
        char *h = history[hist_index % HIST_SIZE];
        int hlen = strlen(h);
        if(hlen > nbuf - 1) hlen = nbuf - 1;
        memmove(buf, h, hlen);
        i = hlen;
        write(2, buf, i);
      }
      escape_state = 0;
      escape_len = 0;
      goto get_next_char;
    } else {
      for(int j = 0; j < escape_len; j++){
        buf[i++] = escape_buf[j];
        write(2, &escape_buf[j], 1);
      }
      escape_state = 0;
      escape_len = 0;
      goto get_next_char;
    }
  }

  if(c == '\n' || c == '\r'){
    buf[i++] = '\n';
    write(2, "\r\n", 2);
    ret = 0;
    goto done;
  }

  if(c == '\b' || c == 0x7f){
    if(i > 0){
      i--;
      write(2, "\b \b", 3);
    }
    goto get_next_char;
  }

  if(c == 0x15){
    while(i > 0){
      i--;
      write(2, "\b \b", 3);
    }
    goto get_next_char;
  }

  if(c < ' ' && c != '\t'){
    goto get_next_char;
  }

  buf[i++] = c;
  write(2, &c, 1);
  goto get_next_char;

done:
  buf[i] = '\0';
  consolemode((prev_mode & 1) ? 1 : 0, (prev_mode & 2) ? 1 : 0);
  return ret;
}

int
getcmd(char *buf, int nbuf)
{
  write(2, "$ ", 2);
  memset(buf, 0, nbuf);
  gets(buf, nbuf);
  if(buf[0] == 0)
    return -1;
  return 0;
}

void
print_history(void)
{
  int start = (hist_count >= HIST_SIZE) ? (hist_count - HIST_SIZE + 1) : 1;
  for(int i = 0; i < HIST_SIZE && (start + i) <= hist_count; i++){
    int idx = (start + i - 1) % HIST_SIZE;
    if(history[idx][0] != 0){
      printf("%4d  %s", start + i, history[idx]);
    }
  }
}

int
cmd_is_history(char *buf)
{
  return (buf[0] == 'h' && buf[1] == 'i' && buf[2] == 's' && 
          buf[3] == 't' && buf[4] == 'o' && buf[5] == 'r' && 
          buf[6] == 'y' && (buf[7] == '\n' || buf[7] == '\0' || buf[7] == ' '));
}

void
runcmd(struct cmd *cmd)
{
  int p[2];
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    exit(1);

  switch(cmd->type){
  default:
    panic("runcmd");

  case EXEC:
    ecmd = (struct execcmd*)cmd;
    if(ecmd->argv[0] == 0)
      exit(1);
    exec(ecmd->argv[0], ecmd->argv);
    fprintf(2, "exec %s failed\n", ecmd->argv[0]);
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    close(rcmd->fd);
    if(open(rcmd->file, rcmd->mode) < 0){
      fprintf(2, "open %s failed\n", rcmd->file);
      exit(1);
    }
    runcmd(rcmd->cmd);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    if(fork1() == 0)
      runcmd(lcmd->left);
    wait(0);
    runcmd(lcmd->right);
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    if(pipe(p) < 0)
      panic("pipe");
    if(fork1() == 0){
      close(1);
      dup(p[1]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->left);
    }
    if(fork1() == 0){
      close(0);
      dup(p[0]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->right);
    }
    close(p[0]);
    close(p[1]);
    wait(0);
    wait(0);
    break;

  case BACK:
    bcmd = (struct backcmd*)cmd;
    if(fork1() == 0)
      runcmd(bcmd->cmd);
    break;
  }
  exit(0);
}

int
main(void)
{
  static char buf[256];
  int fd;

  while((fd = open("console", O_RDWR)) >= 0){
    if(fd >= 3){
      close(fd);
      break;
    }
  }

  while(getcmd_with_history(buf, sizeof(buf)) >= 0){
    if(cmd_is_history(buf)){
      char *histargv[] = { "history", 0 };
      if(fork1() == 0){
        exec("history", histargv);
        exit(1);
      }
      wait(0);
      continue;
    }

    if(buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' '){
      buf[strlen(buf)-1] = 0;
      if(chdir(buf+3) < 0)
        fprintf(2, "cannot cd %s\n", buf+3);
      continue;
    }

    add_history(buf);

    if(fork1() == 0)
      runcmd(parsecmd(buf));
    wait(0);
  }
  exit(0);
}

void
panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(1);
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if(pid == -1)
    panic("fork");
  return pid;
}

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  if(q)
    *q = s;
  ret = *s;
  switch(*s){
  case 0:
    break;
  case '|':
  case '(':
  case ')':
  case ';':
  case '&':
  case '<':
    s++;
    break;
  case '>':
    s++;
    if(*s == '>'){
      ret = '+';
      s++;
    }
    break;
  default:
    ret = 'a';
    while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if(eq)
    *eq = s;

  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int
peek(char **ps, char *es, char *toks)
{
  char *s;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char**, char*);
struct cmd *parsepipe(char**, char*);
struct cmd *parseexec(char**, char*);
struct cmd *nulterminate(struct cmd*);

struct cmd*
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if(s != es){
    fprintf(2, "leftovers: %s\n", s);
    panic("syntax");
  }
  nulterminate(cmd);
  return cmd;
}

struct cmd*
parseline(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parsepipe(ps, es);
  while(peek(ps, es, "&")){
    gettoken(ps, es, 0, 0);
    cmd = backcmd(cmd);
  }
  if(peek(ps, es, ";")){
    gettoken(ps, es, 0, 0);
    cmd = listcmd(cmd, parseline(ps, es));
  }
  return cmd;
}

struct cmd*
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parseexec(ps, es);
  if(peek(ps, es, "|")){
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd*
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while(peek(ps, es, "<>")){
    tok = gettoken(ps, es, 0, 0);
    if(gettoken(ps, es, &q, &eq) != 'a')
      panic("missing file for redirection");
    switch(tok){
    case '<':
      cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
      break;
    case '>':
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE|O_TRUNC, 1);
      break;
    case '+':
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE, 1);
      break;
    }
  }
  return cmd;
}

struct cmd*
parseblock(char **ps, char *es)
{
  struct cmd *cmd;

  if(!peek(ps, es, "("))
    panic("parseblock");
  gettoken(ps, es, 0, 0);
  cmd = parseline(ps, es);
  if(!peek(ps, es, ")"))
    panic("syntax - missing )");
  gettoken(ps, es, 0, 0);
  cmd = parseredirs(cmd, ps, es);
  return cmd;
}

struct cmd*
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  if(peek(ps, es, "("))
    return parseblock(ps, es);

  ret = execcmd();
  cmd = (struct execcmd*)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while(!peek(ps, es, "|)&;")){
    if((tok=gettoken(ps, es, &q, &eq)) == 0)
      break;
    if(tok != 'a')
      panic("syntax");
    cmd->argv[argc] = q;
    cmd->eargv[argc] = eq;
    argc++;
    if(argc >= MAXARGS)
      panic("too many args");
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

struct cmd*
nulterminate(struct cmd *cmd)
{
  int i;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    return 0;

  switch(cmd->type){
  case EXEC:
    ecmd = (struct execcmd*)cmd;
    for(i=0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    nulterminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    nulterminate(pcmd->left);
    nulterminate(pcmd->right);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    nulterminate(lcmd->left);
    nulterminate(lcmd->right);
    break;

  case BACK:
    bcmd = (struct backcmd*)cmd;
    nulterminate(bcmd->cmd);
    break;
  }
  return cmd;
}

struct cmd*
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd*)cmd;
}

struct cmd*
redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;
  return (struct cmd*)cmd;
}

struct cmd*
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
listcmd(struct cmd *left, struct cmd *right)
{
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = LIST;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
backcmd(struct cmd *subcmd)
{
  struct backcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = BACK;
  cmd->cmd = subcmd;
  return (struct cmd*)cmd;
}
