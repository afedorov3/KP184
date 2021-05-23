#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <termios.h>

#include "KP184.h"
#include "util.h" // str2*, matches

#include "device.h"

using namespace std;

KP184 kp184;

static const char *prompt = "> ";
// settings
static const char *defconf_serial = "19200,8,N,1";

// public

// settings
int set_address(int argc, char *argv[])
{
  int rc;

  argc--; argv++;

  if (argc < 1) // get addr
    printf("OK %hhu\n", kp184.getAddress());
  else {
    unsigned long addr;

    if ((rc = Util::str2ul(argv[0], addr)))
      return rc;
    rc = kp184.setAddress((devaddr_t) addr);
    if (rc < 0) {
      if (rc == -EINVAL)
        printf("ERR Device address range is %hhu .. %hhu\n",
          kp184.minAddress(), kp184.maxAddress());
      return rc;
    }
  }

  return 0;
}

#ifdef MBDEBUG
int set_debug(int argc, char *argv[])
{
  bool debug;

  argc--; argv++;

  if (argc) {
    Util::str2b(argv[0], debug);
    kp184.setDebug(debug);
  } else
    printf("%s\n", kp184.getDebug() ? "on" : "off");

  return 0;
}
#endif

cmd_t settings[] = {
  { "address", set_address, "Get or set target device address" },
#ifdef MBDEBUG
  { "debug", set_debug, "Enable or disable debug mode" },
#endif
  CMD_END
};

int cmd_setting(int argc, char *argv[])
{
  int rc = 0, amb = 0;
  cmd_t *setptr = NULL, *setit;

  argc--; argv++;

  if (argc > 0) {
    char *setting = argv[0];

    for(setit = settings; CMD_ISVALID(setit); setit++) {
      if(Util::matches(setting, setit->cmd) == 0) {
        if (setptr == NULL)
          setptr = setit;
        else {
          if (!amb++) printf("ERR Setting %s is ambiguous, candidates are:\n%s\n",
                              setting, setptr->cmd);
          printf("%s\n", setit->cmd);
        }
      }
    }

    if (setptr == NULL) {
      printf("ERR Setting %s is not supported\n", setting);
      rc = -ENOSYS;
    } else if (amb)
      rc = -EINVAL;
    else
      rc = setptr->proc(argc, argv);
  } else {
    for(setit = settings; CMD_ISVALID(setit); setit++) {
      printf(" %s: ", setit->cmd);
      setit->proc(1, argv);
    }
  }

  return rc;
}

//commands
int cmd_switch(int argc, char *argv[])
{
  int rc;
  bool sw = false;

  Util::str2b(argv[0], sw);

  rc = kp184.setOutput(sw);
  if (rc == 0)
    printf("OK Load switched %s\n", sw ? "ON" : "OFF");
  else
    printf("ERR Setting mode: %s\n", strerror(-rc));

  return rc;
}

int cmd_load(int argc, char *argv[])
{
  int rc;

  argc--; argv++;

  if (argc> 0)
    rc = cmd_switch(argc, argv);
  else {
    bool sw;

    rc = kp184.getOutput(sw);
    if (rc == 0)
      printf("OK Load is %s\n", sw ? "ON" : "OFF");
    else
      printf("ERR Getting mode: %s\n", strerror(-rc));
  }

  return rc;
}

int cmd_mode(int argc, char *argv[])
{
  int rc;
  KP184::mode_t mode = KP184::MODE_CV;

  argc--; argv++;

  if (argc > 0) {
    const char *ptr = argv[0];

    do {
      switch(*ptr) {
      case 'V':
      case 'v': mode = KP184::MODE_CV; break;
      case 'C':
      case 'c': if (*(++ptr)) continue;
                mode = KP184::MODE_CC; break;
      case 'R':
      case 'r': mode = KP184::MODE_CR; break;
      case 'P':
      case 'p': mode = KP184::MODE_CP; break;
      default:
        printf("ERR Invalid mode setting %s\n", argv[0]);
        return -EINVAL;
      }

      break;
    } while(*ptr);

    rc = kp184.setMode(mode);
    if (rc == 0)
      printf("OK Mode set to %s\n", KP184::modeStr(mode));
    else
      printf("ERR Setting mode: %s\n", strerror(-rc));
  } else {
    rc = kp184.getMode(mode);
    if (rc == 0)
      printf("OK %s\n", KP184::modeStr(mode));
    else
      printf("ERR Getting mode: %s\n", strerror(-rc));
  }

  return rc;
}

int cmd_voltage(int argc, char *argv[])
{
  int rc;
  double val;

  argc--; argv++;

  if (argc > 0) {
    rc = Util::str2d(argv[0], val);
    if (rc)
      return rc;

    rc = kp184.setVoltage(val);
    if (rc == 0)
      printf("OK Constant voltage set to %g V\n", val);
    else {
      if (rc == -EINVAL)
        printf("ERR Constant voltage range is %g .. %g V\n",
          KP184::modeValMin(KP184::MODE_CV), KP184::modeValMax(KP184::MODE_CV));
      else
        printf("ERR Setting constant voltage: %s\n", strerror(-rc));
    }
  } else {
    rc = kp184.getVoltage(val);
    if (rc == 0)
      printf("OK %g V\n", val);
    else
      printf("ERR Getting active voltage: %s\n", strerror(-rc));
  }

  return rc;
}

int cmd_current(int argc, char *argv[])
{
  int rc;
  double val;

  argc--; argv++;

  if (argc > 0) {
    rc = Util::str2d(argv[0], val);
    if (rc)
      return rc;

    rc = kp184.setCurrent(val);
    if (rc == 0)
      printf("OK Constant current set to %g A\n", val);
    else {
      if (rc == -EINVAL)
        printf("ERR Constant current range is %g .. %g A\n",
          KP184::modeValMin(KP184::MODE_CC), KP184::modeValMax(KP184::MODE_CC));
      else
        printf("ERR Setting constant current: %s\n", strerror(-rc));
    }
  } else {
    rc = kp184.getCurrent(val);
    if (rc == 0)
      printf("OK %g A\n", val);
    else
      printf("ERR Getting active current: %s\n", strerror(-rc));
  }

  return rc;
}

int cmd_resistance(int argc, char *argv[])
{
  int rc;
  double val;

  argc--; argv++;

  if (argc <= 0) {
    puts("ERR Argument required");
    return -EINVAL;
  }

  rc = Util::str2d(argv[0], val);
  if (rc)
    return rc;

  rc = kp184.setResistance(val);
  if (rc == 0)
    printf("OK Constant resistance set to %g Ohm\n", val);
  else {
    if (rc == -EINVAL)
      printf("ERR Constant resistance range is %g .. %g Ohm\n",
        KP184::modeValMin(KP184::MODE_CR), KP184::modeValMax(KP184::MODE_CR));
    else
      printf("ERR Setting constant resistance: %s\n", strerror(-rc));
  }

  return rc;
}

int cmd_power(int argc, char *argv[])
{
  int rc;
  double val;

  argc--; argv++;

  if (argc > 0) {
    rc = Util::str2d(argv[0], val);
    if (rc)
      return rc;

    rc = kp184.setPower(val);
    if (rc == 0)
      printf("OK Constant power set to %g W\n", val);
    else {
      if (rc == -EINVAL)
        printf("ERR Constant power range is %g .. %g W\n",
          KP184::modeValMin(KP184::MODE_CP), KP184::modeValMax(KP184::MODE_CP));
      else
        printf("ERR Setting constant power: %s\n", strerror(-rc));
    }
  } else {
    rc = kp184.getPower(val);
    if (rc == 0)
      printf("OK %g W\n", val);
    else
      printf("ERR Getting active power: %s\n", strerror(-rc));
  }

  return rc;
}

int cmd_status(int argc, char *argv[])
{
  int rc;
  bool out;
  KP184::mode_t mode;
  double v, c;

  rc = kp184.getStatus(out, mode, v, c);
  if (rc == 0) {
    printf("Load %s\n", out ? "ON" : "OFF");
    printf("Mode %s\n", KP184::modeStr(mode));
    printf("Voltage %g V\n", v);
    printf("Current %g A\n", c);
    printf("Power %.2f A\n", c * v);
  } else
    printf("ERR Getting status: %s\n", strerror(-rc));

  return rc;
}

cmd_t devcmds[] = {
  { "off", cmd_switch, "Switch the load OFF" },
  { "on", cmd_switch, "Switch the load ON" },
  { "load", cmd_load, "Get load status or switch the load ON or OFF" },
  { "mode", cmd_mode, "Set load mode: V / C / R / P" },
  { "voltage", cmd_voltage, "Get active voltage or set constant voltage, V" },
  { "current", cmd_current, "Get active current or set constant current, A" },
  { "resistance", cmd_resistance, "Set constant resistance, Ohm" },
  { "power", cmd_power, "Set constant power, W" },
  { "status", cmd_status, "Get active status" },
  { "setting", cmd_setting, "Manage internal program settings" },
  CMD_END
};

int openDevice(Link::linktype_t type, const char *link, const char *config)
{
  return kp184.open(type, link, config);
}

int reOpenDevice()
{
  return kp184.reOpen();
}

const char *getDefaultConfig(Link::linktype_t type)
{
  switch(type) {
  case Link::SERIAL: return defconf_serial; break;
  default: break;
  }

  return "";
}

const char *getPrompt()
{
  return prompt;
}

void helpCommand(int argc, char *argv[])
{
  cmd_t *cmdit;
  for(cmdit = devcmds; CMD_ISVALID(cmdit); cmdit++) {
    printf("%12s  %s\n", cmdit->cmd, cmdit->help);
    if (strcmp(cmdit->cmd, "setting") == 0) {
      cmd_t *setit;
      for(setit = settings; CMD_ISVALID(setit); setit++)
        printf("%21s  %s\n", setit->cmd,  setit->help);
    }
  }
}
