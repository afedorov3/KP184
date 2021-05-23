#ifndef _UTIL_H
#define _UTIL_H

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <ctime>

class Util {
public:
  static ssize_t hex2bin(uint8_t obuf[], const char ibuf[], size_t len) {
    uint8_t c, c2;
    ssize_t ret;

    ret = len = len / 2;
    while (len > 0 && *ibuf != 0) {
      c = *ibuf++;
      if( c >= '0' && c <= '9' )
        c -= '0';
      else if( c >= 'a' && c <= 'f' )
        c -= 'a' - 10;
      else if( c >= 'A' && c <= 'F' )
        c -= 'A' - 10;
      else
        return -1;

      c2 = *ibuf++;
      if( c2 >= '0' && c2 <= '9' )
        c2 -= '0';
      else if( c2 >= 'a' && c2 <= 'f' )
        c2 -= 'a' - 10;
      else if( c2 >= 'A' && c2 <= 'F' )
        c2 -= 'A' - 10;
      else
        return -1;

      *obuf++ = ( c << 4 ) | c2;
      len--;
    }

    return ret;
  }

  static ssize_t bin2hex(char *obuf[], const uint8_t ibuf[], size_t len) {
    ssize_t ret;
    char *buf;
    const char map[] = { '0', '1', '2', '3', '4', '5', '6', '7',
                         '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };

    ret = len * 3 + 1;
    buf = (char *)malloc(ret);
    if (!buf)
      return -1;
    *obuf = buf;

    while(len--) {
      *buf++ = map[(*ibuf >> 4) & 0x0F];
      *buf++ = map[*ibuf & 0x0F];
      *buf++ = ' ';
      ibuf++;
    }

    return ret;
  }

  static void printbuf(const uint8_t buf[], size_t len, const char* tag) {
    size_t i;

    if ((stderr == NULL) || (len == 0))
      return;

    if (tag)
      fprintf(stderr, "%s:\n", tag);

    for (i = 0; i < len; i++) {
      fprintf(stderr, "%02X%c", buf[i],
             (i % 16 == 15) ? '\n' : ' ');
    }
    if (i && (i % 16 != 15))
      fputc('\n', stderr);
  }

  /* from iproute2 */
  static int matches(const char cmd[], const char pattern[]) {
    size_t len = strlen(cmd);
    if (len > strlen(pattern))
      return -1;
    return strncasecmp(pattern, cmd, len);
  }

  static int str2b(const char str[], bool &val) {
    if ((matches(str, "true") == 0) ||
        (matches(str, "1") == 0)    ||
        (matches(str, "on") == 0))
      val = true;
    else
      val = false;

    return 0;
  }

  static int str2i(const char str[], int &val) {
    int v;
    char *eptr;

    v = strtol(str, &eptr, 0);
    if (*eptr != '\0') {
      printf("ERR Malformed value %s\n", str);
      return -EINVAL;
    }

    val = v;
    return 0;
  }

  static int str2ul(const char str[], unsigned long &val) {
    unsigned long v;
    char *eptr;

    v = strtoul(str, &eptr, 0);
    if (*eptr != '\0') {
      printf("ERR Malformed value %s\n", str);
      return -EINVAL;
    }

    val = v;
    return 0;
  }

  static int str2d(const char str[], double &val) {
    double v;
    char *eptr;

    v = strtod(str, &eptr);
    if (*eptr == 'm') {
      v /= 1000.0;
      ++eptr;
    }
    if (*eptr != '\0') {
      printf("Malformed value %s\n", str);
      return -EINVAL;
    }

    val = v;
    return 0;
  }

  static int str2du(const char str[], double &val, const char *&unit) {
    double v;
    char *eptr;

    v = strtod(str, &eptr);
    if (*eptr == 'm') {
      v /= 1000.0;
      ++eptr;
    }

    val = v;
    unit = eptr;
    return 0;
  }

  static int str2dmm(const char str[], double &val, double vmin, double vmax) {
    double v;
    int rc;

    rc = str2d(str, v);
    if (rc != 0)
      return rc;
    if (v < vmin || v > vmax) {
      printf("ERR Value %s is out of range %g .. %g\n", str, vmin, vmax);
      return -EINVAL;
    }

    val = v;
    return 0;
  }

  static int str2ts(const char str[], struct timespec &ts) {
    const char *eptr = str;
    int part = 0;
    time_t t;

    ts.tv_sec = ts.tv_nsec = 0;
    do {
      t = strtoul(eptr, const_cast<char**>(&eptr), 10);
      if (((part > 0) && (t >= 60)) ||
         ((*eptr != '\0') && ((*eptr != ':') || (part > 1))))
        return -EINVAL;
      ts.tv_sec = ts.tv_sec * 60 + t;
      ++part;
    } while (*eptr++);

    return 0;
  }
};

#endif /* _UTIL_H */
