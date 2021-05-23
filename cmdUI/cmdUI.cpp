#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <unistd.h>
#include <termios.h>
#include <libgen.h> // basename

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include "link.h" // linktype_t
#include "util.h" // matches, str2dmm

#include "device.h"

using namespace std;

static int quit = 0;

static void sig_term_handler(int signum, siginfo_t *info, void *ptr)
{
  quit = 1;
}

/* parses string and return argv style array or NULL if failed */
//#define TRNEWLINE 1 /* translate newline characters */
#define PRINTABLE 1 /* only allow 7-bit printable characters */
#define NEWLINEQ 1  /* exit quoting on newline */
static char** line2argv(const char* cmdl, int maxlen, int* argc)
{
  char a;
  int i, j, len = 0, _argc, argstart;
  char** argv;
  char* _argv;
  bool in_QM, in_ES;

  if (cmdl == NULL || maxlen == 0)
    return NULL;

  /* cmdl length */
  if (maxlen > 0)
    len = strnlen(cmdl, maxlen);
  else
    len = strlen(cmdl);

  i = ((len + 2)/2) * sizeof(void*) + sizeof(void*);

  /* pointers + storage + 2xNULL */
  argv = (char**)malloc(i + len + 2);
  if (argv == NULL)
    return NULL;

  /* storage pointer = argv + pointers */
  _argv = (char*)(((char*)argv) + i);

  in_QM = in_ES = false;
  i = j = _argc = argstart = 0;

  while( (i < len) && (a = cmdl[i++]) ) {
#ifdef PRINTABLE
    if (!isprint(a))
      continue;
#endif /* PRINTABLE */
    if(in_ES) {        /* in escape */
      switch(a) {
#ifdef TRNEWLINE
      case 'r':
      case 'n':
        _argv[j++] = '\n';
        break;
#endif /* TRNEWLINE */
      case 't':
        _argv[j++] = '\t';
        break;
      default:
        _argv[j++] = '\\';
      case ' ':
      case '\"':
        _argv[j++] = a;
        break;
      }
      in_ES = false;
    } else if(in_QM) { /* in quoting */
      switch(a) {
      case '\\':
        in_ES = true;
        break;
#ifdef NEWLINEQ
      case '\r':
      case '\n':
#endif /* NEWLINEQ */
      case '\"':
        in_QM = false;
        break;
      default:
        _argv[j++] = a;
        break;
      }
    } else {
      switch(a) {
      case '\"': /* quoting */
        in_QM = true;
        break;
      case '\\': /* escape */
        in_ES = true;
        break;
      case ' ':  /* spacers */
      case '\t':
      case '\n':
      case '\r':
        if(argstart == -1) {
          _argv[j++] = '\0';
          /* remember next argument start position */
          argstart = j;
        }
        continue;
      default:  /* text */
        _argv[j++] = a;
        break;
      }
      if(argstart != -1) {
        /* write current argument ptr */
        argv[_argc++] = _argv + argstart;
        argstart = -1;
      }
    }
  }
  _argv[j] = '\0';   /* NULL terminators */
  argv[_argc] = NULL;
  if (argc)
    *argc = (int)_argc;
  return argv;
}

int int_quit(int argc, char *argv[])
{
  quit = 1;

  return 0;
}

int int_help(int argc, char *argv[])
{
  helpCommand(argc, argv);

  return 0;
}

int int_delay(int argc, char *argv[])
{
  double vald;

  argc--; argv++;

  if (argc <= 0) {
    printf("ERR Command 'delay' requires an argument\n");
    return -EINVAL;
  }

  if (Util::str2dmm(argv[0], vald, 0, 5184000.0) == 0) {
    if (vald < 30)
      usleep((useconds_t)(vald * 1000000.0));
    else
      sleep((unsigned int)vald);
   }

  return 0;
}

int int_reopen(int argc, char *argv[])
{
  return reOpenDevice();
}

static cmd_t intcmds[] = {
  { "quit", int_quit, "Terminate current connection and exit the program" },
  { "exit", int_quit, "Terminate current connection and exit the program" },
  { "help", int_help, "Show help" },
  { "delay", int_delay, "Delay in execution, s" },
  { "reopen", int_reopen, "Reopen device connection" },
  CMD_END
};

static int process_command(int fd, char *line, int len)
{
  int argc = 0;
  char **argv = NULL;
  const char *cmd;
  int rc = 0, amb = 0;
  cmd_t *cmdptr = NULL, *cmdit;
  const cmd_t *cmdnss[] = { intcmds, devcmds };

  argv = line2argv(line, len, &argc);
  if (argv == NULL)
    return -1;
  if (argc == 0)
    return 0;

  cmd = argv[0];
  for(int nsi = 0; nsi < (int)(sizeof(cmdnss)/sizeof(*cmdnss)); nsi++) {
    for(cmdit = cmdnss[nsi]; CMD_ISVALID(cmdit); cmdit++) {
      if(Util::matches(cmd, cmdit->cmd) == 0) {
        if (cmdptr == NULL)
          cmdptr = cmdit;
        else {
          if (!amb++) printf("Command %s is ambiguous, candidates are:\n%s\n",
                              cmd, cmdptr->cmd);
          printf("%s\n", cmdit->cmd);
        }
      }
    }
  }

  if (cmdptr == NULL) {
    printf("Command not supported\n");
    rc = -ENOSYS;
  } else if (amb)
    rc = -EINVAL;
  else
    rc = cmdptr->proc(argc, argv);

  free(argv);

  return rc;
}

void usage(const char *prog)
{
  printf("usage: %s <-t tty|-s host[:port]> [-T conf] [\"cmd 1\"] ...\n", prog);
  printf(" -t: communicate via TTY port\n");
  printf(" -s: communicate via socket\n");
  printf(" -T: serial configuration string [%s]\n", getDefaultConfig(Link::SERIAL));
}

int main(int argc, char *argv[])
{
  int devfd = -1;
  struct sigaction _sigact;
  int rc = 0, tty;
  char *cmd = NULL;
  char cmdbuf[128];
  Link::linktype_t ltype = Link::SERIAL;
  const char *link = NULL, *lconf = NULL, *prog = basename(argv[0]);
  char c;

  opterr = 0;
  while ((c = getopt(argc, argv, "t:s:T:")) != -1) {
    switch(c) {
    case 't': ltype = Link::SERIAL; link = optarg; break;
    case 's': ltype = Link::SOCKET; link = optarg; break;
    case 'T': lconf = optarg; break;
    case '?':
    case 'h':
    default: usage(prog); return 1;
    }
  }
  argc -= optind;
  argv += optind;

  if (link == NULL) {
    usage(prog);
    return -EINVAL;
  }

  memset(&_sigact, 0, sizeof(_sigact));
  _sigact.sa_sigaction = sig_term_handler;
  _sigact.sa_flags = SA_SIGINFO;

  sigaction(SIGTERM, &_sigact, NULL);
  sigaction(SIGINT, &_sigact, NULL);
  sigaction(SIGQUIT, &_sigact, NULL);

  if (openDevice(ltype, link, lconf))
    return -ENOTCONN;

#ifdef HAVE_READLINE
  rl_bind_key('\t', rl_insert);
#endif /* HAVE_READLINE */

  tty = isatty(STDIN_FILENO);
  while(!quit) {
    if (argc > 0)
      cmd = argv[0];
    else {
      if (tty) {
#ifdef HAVE_READLINE
        cmd = readline(tty ? getPrompt() : "");
      } else {
#else
        printf("%s", getPrompt());
      }
      {
#endif /* !HAVE_READLINE */
        fflush(stdout);
        cmd = fgets(cmdbuf, sizeof(cmdbuf), stdin);
      }
    }
    if (cmd) {
      cmd[strcspn(cmd, "\r\n")] = '\0'; // trim newline
      if (argc > 0 || !tty) printf("%s\n", cmd);
      int lrc = process_command(devfd, cmd, -1);
      if (lrc != 0)
        rc = lrc; // preserve fault codes on exit
      if (argc > 0) // deal with the passed command
        argc--, argv++;
#ifdef HAVE_READLINE
      else if (tty) {
        if (*cmd) add_history(cmd);
        free(cmd);
      }
#endif /* HAVE_READLINE */
      if (!quit) {
        if (lrc && argc < 1 && tty) printf("%d ", lrc); // only print fail rc with prompt
        usleep(50000); // interframe delay (Gap Time: default 50ms, min. 10ms)
      }
    } else {
      if (!tty && feof(stdin))
        quit = 1;
      else if (errno > 0)
        rc = errno;
    }
  }

  close(devfd), devfd = -1;

  return rc;
}
