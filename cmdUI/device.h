#ifndef _DEVICE_H
#define _DEVICE_H

// commands
typedef struct _cmd_t {
  const char *cmd;
  int (*proc)(int argc, char *argv[]);
  const char *help;
} const cmd_t;
#define CMD_END { NULL, NULL, NULL }
#define CMD_ISVALID(ptr) (((ptr)->cmd != NULL) && ((ptr)->proc != NULL))
extern cmd_t devcmds[];

// misc
int openDevice(Link::linktype_t type, const char link[], const char config[]);
int reOpenDevice();
const char *getDefaultConfig(Link::linktype_t type);
const char *getPrompt();
void helpCommand(int argc, char *argv[]);

#endif /* _DEVICE_H */
