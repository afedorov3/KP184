#include <cstdio>
#include <cstring>
#include <cerrno>
#include <cmath>
#include <iostream>
#include <fstream>
#include <csignal>
#include <ctime>
#include <cstdarg>
#include <unistd.h>
#include <sys/stat.h>
#include <libgen.h> // basename

#include "KP184.h"
#include "util.h"

using namespace std;

static const char *defconf_serial = "19200,8,N,1";
static const struct timespec defconf_interval = { 1, 0 };
static const unsigned long defconf_n0samp = 3;
static const unsigned long defconf_ntsamp = 3;
static const useconds_t interframe_delay = 10000;

enum {
  TERM_NONE = 0,
  TERM_TIME = 1,
  TERM_IMMED = 2,
  TERM_USER = TERM_IMMED + 0,
  TERM_LOWVOLT = TERM_IMMED + 1,
  TERM_LOWCUR = TERM_IMMED + 2,
  TERM_HICUR = TERM_IMMED + 3,
  TERM_ERR = TERM_IMMED + 4,
  TERM_MAX = TERM_ERR
};
static int term;
static FILE *outfile = NULL;

void sig_handler(int signum, siginfo_t *info, void *ptr)
{
  term = TERM_USER;
}

#define USEC 1000000L
#define NSEC 1000000000L
void ts_add(struct timespec &ts, const struct timespec &a,
                                 const struct timespec &b)
{
    ts.tv_sec = a.tv_sec + b.tv_sec;
    ts.tv_nsec = a.tv_nsec + b.tv_nsec;
    if (ts.tv_nsec >= NSEC) {
        ts.tv_sec++;
        ts.tv_nsec -= NSEC;
    }
}

/* from strace */
void ts_sub(struct timespec &ts, const struct timespec &a,
                                 const struct timespec &b)
{
    ts.tv_sec = a.tv_sec - b.tv_sec;
    ts.tv_nsec = a.tv_nsec - b.tv_nsec;
    if (ts.tv_nsec < 0) {
        ts.tv_sec--;
        ts.tv_nsec += NSEC;
    }
}

void ts_div(struct timespec &ts, const struct timespec &a, unsigned long divider)
{
  uint64_t x = (uint64_t)a.tv_sec * NSEC + a.tv_nsec;
  x /= divider;
  ts.tv_sec = x / NSEC;
  ts.tv_nsec = x % NSEC;
}

int ts_cmp(const struct timespec &a, const struct timespec &b)
{
  if (a.tv_sec > b.tv_sec) return 1;
  if (b.tv_sec > a.tv_sec) return -1;

  if (a.tv_nsec > b.tv_nsec) return 1;
  if (b.tv_nsec > a.tv_nsec) return -1;

  return 0;
}

const char *ts2str(const struct timespec &ts)
{
  static char str[64];
  time_t s = ts.tv_sec;
  if (ts_cmp(ts, { 0, 0 }) < 0)
    return "N/A";
  if (ts.tv_nsec >= NSEC / 5) s++;
  snprintf(str, sizeof(str), "%ld:%02ld:%02ld", s / 3600, s % 3600 / 60, s % 60);
  return str;
}

int setup(KP184 &device, KP184::mode_t mode, double val)
{
  int rc;

  rc = device.setOutput(false);
  if (rc) {
    fprintf(stderr, "ERR Switching load off: %s\n", strerror(-rc));
    return rc;
  }

  usleep(interframe_delay);
  rc = device.setMode(mode);
  if (rc) {
    fprintf(stderr, "ERR Setting mode: %s\n", strerror(-rc));
    return rc;
  }

  usleep(interframe_delay);
  rc = device.setModeValue(mode, val);
  if (rc) {
    fprintf(stderr, "ERR Setting mode value: %s\n", strerror(-rc));
    return rc;
  }

  return 0;
} 

int writefile(const char *filepath, bool header, bool append, bool persist, const char *fmt...)
{
  va_list args;

  if (outfile == NULL) {
    if (filepath) {
      int rc;
      struct stat st = {};

      if ((rc = stat(filepath, &st)) == 0) {
        if (S_ISDIR(st.st_mode) || S_ISBLK(st.st_mode)) {
          fprintf(stderr, "\nERR %s shouldn't be directory or block device\n", filepath);
          return -EINVAL;
        }
      }

      if (header && append && (st.st_size > 0))
        return 0;

      outfile = fopen(filepath, (header && !append) ? "w" : "a");
      if (outfile == NULL) {
        rc = -errno;
        fprintf(stderr, "\nERR Opening %s: %s\n", filepath, strerror(errno));
        return rc;
      }

    } else
      outfile = stdout;
  }

  va_start(args, fmt);
  vfprintf(outfile, fmt, args);
  va_end(args);

  if (filepath && !persist)
    fclose(outfile), outfile = NULL;

  return 0;
}

void usage(const char prog[])
{
  printf("usage: %s <-t tty|-s host[:port]> <-l load> <-v Volt> [-B conf] [-a addr]"
         " [-V Volt] [-c Amp] [-C Amp] [-i interval] [-N samples] [-n samples]"
         " [-f path] [-o] [-q]\n", prog);
  printf(" -t: communicate via TTY port\n");
  printf(" -s: communicate via socket\n");
  printf(" -B: serial configuration string [%s]\n", defconf_serial);
  printf(" -a: device address [%hhu]\n", KP184::defAddress());
  printf(" -l: load mode and value: val[m]<A|R|W>\n");
  printf(" -v: voltage threshold, V\n");
  printf(" -V: voltage threshold to set half load, V\n");
  printf(" -c: cuurent low threshold, A\n");
  printf(" -C: current high threshold, load is immediately off, A\n");
  printf(" -T: maximum load time, h:m:s\n");
  printf(" -i: sample interval, s [%g s]\n",
        (double)defconf_interval.tv_sec + (double)defconf_interval.tv_nsec / NSEC);
  printf(" -N: initial no load samples [%lu]\n", defconf_n0samp);
  printf(" -n: sequential samples exceeding thresholds [%lu]\n", defconf_ntsamp);
  printf(" -f: output CSV file name [stdout]\n");
  printf(" -o: do not append CSV file\n");
  printf(" -q: produce no additional information\n");
}

int main(int argc, char *argv[])
{
  int rc = 0, op;
  KP184 kp184;
  Link::linktype_t ltype = Link::NONE;
  KP184::mode_t mode = KP184::MODE_CV, cmode; // N/A
  const char *prog = basename(argv[0]), *link = NULL, *lconf = defconf_serial, *saddr = NULL;
  const char *sload = NULL, *svlthres = NULL, *svhthres = NULL, *sclthres = NULL, *schthres = NULL;
  const char *sint = NULL, *stend = NULL, *csvfile = NULL;
  const char *sn0samp = NULL, *sntsamp = NULL;
  double vlthres, load, vhthres = -1.0, clthres = -1.0, chthres = -1.0;
  double voltage, current, pv, pc, capacity, energy;
  unsigned long sampleno, n0samp = defconf_n0samp, ntsamp = defconf_ntsamp, vsamp, csamp;
  bool sw, bstat = false, quiet = false, fappend = true, fpersist = false;
  struct timespec tstart, tload, tsamp, thalf;
  struct itimerspec tsint, tsend = {};
  sigset_t timset;
  timer_t tintid = 0, tendid = 0;
  static struct sigaction sigact;
  siginfo_t sinfo;
  struct winsize ws;

  opterr = 0;
  while ((op = getopt(argc, argv, "t:s:B:a:l:v:V:c:C:T:i:N:n:f:oq")) != -1) {
    switch(op) {
    case 't': ltype = Link::SERIAL; link = optarg; break;
    case 's': ltype = Link::SOCKET; link = optarg; break;
    case 'B': lconf = optarg; break;
    case 'a': saddr = optarg; break;
    case 'l': sload = optarg; break;
    case 'v': svlthres = optarg; break;
    case 'V': svhthres = optarg; break;
    case 'c': sclthres = optarg; break;
    case 'C': schthres = optarg; break;
    case 'T': stend = optarg; break;
    case 'i': sint = optarg; break;
    case 'N': sn0samp = optarg; break;
    case 'n': sntsamp = optarg; break;
    case 'f': csvfile = optarg; break;
    case 'o': fappend = false; break;
    case 'q': quiet = true; break;
    case '?':
    case 'h':
    default: usage(prog); return -EINVAL;
    }
  }
  argc -= optind;
  argv += optind;

  if ((link == NULL) || (sload == NULL) || (svlthres == NULL)) {
    usage(prog);
    return -EINVAL;
  }

  Util::str2du(sload, load, sload);
  if (strcasecmp(sload, "A") == 0)
    mode = KP184::MODE_CC;
  else if ((strcasecmp(sload, "R") == 0) ||
           (strcasecmp(sload, "Ohm") == 0))
    mode = KP184::MODE_CR;
  else if (strcasecmp(sload, "W") == 0)
    mode = KP184::MODE_CP;
  else {
    fprintf(stderr, "ERR Malformed load value\n");
    rc = -EINVAL;
  }

  Util::str2du(svlthres, vlthres, svlthres);
  if ((*svlthres == '\0') || (strcasecmp(svlthres, "V") == 0)) {
    if (vlthres < 0.1) {
      fprintf(stderr, "ERR Voltage threshold minimum value is 0.1V\n");
      rc = -EINVAL;
    };
  } else {
    fprintf(stderr, "ERR Malformed voltage threshold value\n");
    rc = -EINVAL;
  }

  if (svhthres) {
    if (*svhthres == '\0')
      vhthres = vlthres;
    else {
      Util::str2du(svhthres, vhthres, svhthres);
      if ((*svhthres == '\0') || (strcasecmp(svhthres, "V") == 0)) {
        if (vhthres < vlthres) {
          fprintf(stderr, "ERR half load voltage threshold can't be lower than voltage threshold\n");
          rc = -EINVAL;
        };
      } else {
        fprintf(stderr, "ERR Malformed half load voltage threshold value\n");
        rc = -EINVAL;
      }
    }
  }

  if (sclthres) {
    Util::str2du(sclthres, clthres, sclthres);
    if ((*sclthres != '\0') && (strcasecmp(sclthres, "A") != 0)) {
      fprintf(stderr, "ERR Malformed low current threshold value\n");
      rc = -EINVAL;
    }
  }

  if (schthres) {
    Util::str2du(schthres, chthres, schthres);
    if ((*schthres != '\0') && (strcasecmp(schthres, "A") != 0)) {
      fprintf(stderr, "ERR Malformed high current threshold value\n");
      rc = -EINVAL;
    }
  }

  if (saddr) {
    unsigned long addr;

    if ((Util::str2ul(argv[0], addr) != 0) ||
       (kp184.setAddress((devaddr_t) addr) != 0)) {
        fprintf(stderr, "ERR Device address range is %hhu .. %hhu\n",
          kp184.minAddress(), kp184.maxAddress());
        rc = -EINVAL;
    }
  }

  if (stend) {
    if (Util::str2ts(stend, tsend.it_value) != 0) {
      fprintf(stderr, "ERR Malformed time value %s\n", stend);
      rc = -EINVAL;
    }
  }

  if (sint) {
    double sec;

    Util::str2du(sint, sec, sint);
    if (*sint) {
      fprintf(stderr, "ERR Malformed interval value\n");
      rc = -EINVAL;
    } else if (sec < 0.2) {
      fprintf(stderr, "ERR Minimum sample interval is 0.2 s\n");
      rc = -EINVAL;
    } else {
      tsint.it_interval.tv_sec = (time_t)sec;
      tsint.it_interval.tv_nsec = (long)(modf(sec, &sec) * NSEC);
    }
  } else {
    tsint.it_interval.tv_sec = defconf_interval.tv_sec;
    tsint.it_interval.tv_nsec = defconf_interval.tv_nsec;
  }
  ts_div(thalf, tsint.it_interval, 2);

  if (sn0samp && Util::str2ul(sn0samp, n0samp)) {
    fprintf(stderr, "ERR Malformed no load samples value\n");
    rc = -EINVAL;
  }

  if (sntsamp && Util::str2ul(sntsamp, ntsamp)) {
    fprintf(stderr, "ERR Malformed threshold samples value\n");
    rc = -EINVAL;
  }
  if (ntsamp == 0) {
    fprintf(stderr, "ERR Threshold sample count should be greater than 0\n");
    rc = -EINVAL;
  }

  if (rc != 0)
   return rc;

  memset(&sigact, 0, sizeof(sigact));
  sigact.sa_sigaction = sig_handler;
  sigact.sa_flags = SA_SIGINFO;

  sigaction(SIGTERM, &sigact, NULL);
  sigaction(SIGINT, &sigact, NULL);
  sigaction(SIGQUIT, &sigact, NULL);

  rc = kp184.open(ltype, link, lconf);
  if (rc)
    return rc;

  rc = setup(kp184, mode, load);
  if (rc)
    goto close;

  sigemptyset(&timset);
  sigaddset(&timset, SIGALRM);
  sigprocmask(SIG_BLOCK, &timset, NULL);
  rc = timer_create(CLOCK_MONOTONIC, NULL, &tintid);
  if (rc == EAGAIN)
    rc = timer_create(CLOCK_MONOTONIC, NULL, &tintid); // one more time
  if (rc) {
    perror("ERR Can't create sample timer");
    goto close;
  }

  if (ts_cmp(tsend.it_value, { 0, 0 }) > 0) {
    rc = timer_create(CLOCK_MONOTONIC, NULL, &tendid);
    if (rc == EAGAIN)
      rc = timer_create(CLOCK_MONOTONIC, NULL, &tendid);
    if (rc) {
      perror("ERR Can't create termination timer");
      goto close;
    }
  }

  if (!quiet) {
    fprintf(stderr, "Connection: %s %s%s%s address %hhu\n", Link::linkTypeStr(ltype), link, lconf ? " " : "",
                    lconf ? lconf : "", kp184.getAddress());
    fprintf(stderr, "Settins:\n Mode: %s\n Load: %g %s\n Low voltage threshold: %g V\n",
                    KP184::modeStr(mode), load, KP184::modeUnit(mode), vlthres);
    if (svhthres)
      fprintf(stderr, " HL threshold: %g V\n", vhthres);
    if (sclthres)
      fprintf(stderr, " Low current threshold: %g A\n", clthres);
    if (schthres)
      fprintf(stderr, " High current threshold: %g A\n", chthres);
    if (stend)
      fprintf(stderr, " Maximum load time: %s\n", ts2str(tsend.it_value));
    fprintf(stderr, " Interval: %g s\n No load samples: %lu\n Threshold samples: %lu\n",
                    (double)tsint.it_interval.tv_sec +
                    (double)tsint.it_interval.tv_nsec / NSEC, n0samp, ntsamp);
    if (csvfile)
      fprintf(stderr, " CSV file: %s\n", csvfile);
  }

  writefile(csvfile, true, fappend, fpersist, "No.;time;voltage;unit;current;unit\n");

  usleep(interframe_delay);

  vsamp = csamp = ntsamp;
  sampleno = 0;
  bstat = !quiet && ((csvfile != NULL) || (isatty(STDOUT_FILENO) == 0));
  if ((tsint.it_interval.tv_sec == 0) && tsint.it_interval.tv_nsec < (NSEC/2)) // < 0.5s
    fpersist = true;
  capacity = energy = 0.0;
  term = TERM_NONE;

  tsint.it_value.tv_sec = tsint.it_interval.tv_sec;
  tsint.it_value.tv_nsec = tsint.it_interval.tv_nsec;
  tload.tv_sec = tload.tv_nsec = 0;
  clock_gettime(CLOCK_MONOTONIC, &tstart);
  if (timer_settime(tintid, 0, &tsint, NULL) == -1) {
    perror("ERR Setting termination timer failure");
    term = TERM_ERR;
  }
  while(term < TERM_IMMED) {
    struct timespec tcur, tprev;

    if (sampleno == n0samp) {
      rc = kp184.setOutput(true);
      if (rc) goto looperr;
      clock_gettime(CLOCK_MONOTONIC, &tsamp);
      tload.tv_sec = tsamp.tv_sec;
      tload.tv_nsec = tsamp.tv_nsec;
      if (ts_cmp(tsend.it_value, { 0, 0 }) > 0) {
        if (timer_settime(tendid, 0, &tsend, NULL) == -1) {
          perror("\nERR Setting termination timer failure");
          term = TERM_ERR;
          break;
        }
      }
      usleep(300000); // allow load to stabilize
    }
    rc = kp184.getStatus(sw, cmode, voltage, current);
    if (rc) goto looperr;
    if (sampleno != n0samp) clock_gettime(CLOCK_MONOTONIC, &tsamp);
    ts_sub(tcur, tsamp, tstart);
    ++sampleno;

    // high current threshold
    if ((chthres >= 0.0) && (current >= chthres)) {
      usleep(interframe_delay);
      kp184.setOutput(false);
      fprintf(stderr, "\n!!! Current %g A reached high threshold, load is turned off !!!\n", current);
      term = TERM_HICUR;
    }

    writefile(csvfile, false, fappend, fpersist, "%lu;%ld.%06ld;%g;V;%g;A\n",
             sampleno, tcur.tv_sec, tcur.tv_nsec / (NSEC/USEC), voltage, current);

    if ((sampleno - 1) > n0samp) {
      ts_sub(tprev, tsamp, tprev);
      double passed = (double)tprev.tv_sec + (double)tprev.tv_nsec / NSEC;
      capacity += (current + pc) / 2.0 * passed / 3600.0;
      energy += (current + pc) * (voltage + pv) / 4.0 * passed / 3600.0;
    }

    if (bstat) {
      op = fprintf(stderr, "\r%lu %ld.%06ld s %g V %g A %.5g W %.5g Ah %.5g Wh",
           sampleno, tcur.tv_sec, tcur.tv_nsec / 1000,
           voltage, current, voltage * current, capacity, energy);
      ioctl(STDERR_FILENO, TIOCGWINSZ, &ws);
      fprintf(stderr, "%*s", ws.ws_col - op, "");
      fflush(stderr);
    }

    pv = voltage; pc = current;
    tprev.tv_sec = tsamp.tv_sec;
    tprev.tv_nsec = tsamp.tv_nsec;

    if (term) break;

    // voltage thresholds
    if ((vhthres > 0.0) && (voltage <= vhthres)) {
      clock_gettime(CLOCK_MONOTONIC, &tcur);
      ts_sub(tcur, tcur, tsamp);
      ts_sub(tcur, thalf, tcur); // remaining time to half interval
      while (nanosleep(&tcur, &tcur) == EINTR);
      rc = kp184.setModeValue(mode, load / 2.0);
      if (rc) goto looperr;
      vhthres = -1.0;
    } else {
      if (voltage <= vlthres) {
        --vsamp;
        if (vsamp == 0) {
          term = TERM_LOWVOLT;
          break;
        }
      } else if (vsamp < ntsamp)
        ++vsamp;
    }

    // low current thresholds
    if ((sampleno > n0samp) && (clthres >= 0.0)) {
      if (current <= clthres) {
        --csamp;
        if (csamp == 0) {
          term = TERM_LOWCUR;
          break;
        }
      } else if (csamp < ntsamp)
        ++csamp;
    }

    // wait for timers
    while (term == TERM_NONE) {
      int sret = sigwaitinfo(&timset, &sinfo);
      if ((sret != SIGALRM) || (sinfo.si_value.sival_ptr == 0))
        continue;
      if ((timer_t)sinfo.si_value.sival_ptr != tintid)
        term = TERM_TIME;
      break;
    }

    continue;

looperr:
    fprintf(stderr, "\nERR Communicating device: %s\n", strerror(-rc));
    fprintf(stderr, "Trying to reconnect");
    do {
      sleep(1);
      fputs(".\a", stderr);
      if ((rc = kp184.reOpen()) == 0)
        rc = setup(kp184, mode, load);
    } while((term == TERM_NONE) && (rc != 0));
    fputs("\n", stderr);
  }

  usleep(interframe_delay);
  if (!quiet) fprintf(stderr, "%sSwitching the load off", bstat ? "\n" : "");
  do {
    rc = kp184.setOutput(false);
    if (rc != 0) {
      fputs(".\a", stderr);
      sleep(1);
      kp184.reOpen();
      continue;
    }
    break;
  } while(true);

  if ((outfile != NULL) && (outfile != stdout))
    fclose(outfile);

  if (tintid) timer_delete(tintid);
  if (tendid) timer_delete(tendid);

  ts_sub(tload, tsamp, tload);

  if (!quiet) {
    static const char *sreason[TERM_MAX] = {
      "maximum load time", "user", "low voltage threshold",
      "low current threshold", "high current threshold", "error" };
    fprintf(stderr, "\nTerminated by %s\n", sreason[term - 1]);

    if (!bstat)
      fprintf(stderr, "Load was on for %lu samples %s %.5g Ah %.5g Wh\n",
             sampleno - n0samp, ts2str(tload), capacity, energy);
  }

close:
  kp184.close();

  return term;
}
