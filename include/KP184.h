#ifndef _KP184_H
#define _KP184_H

#include <cstring>  // memcmp
#include <cerrno>   // E*
#include <cmath> // NaN
#include <sys/types.h> // ssize_t
#include <unistd.h>  // usleep

#include "mbrtu.h"

class KP184: public mbRTU<24, 1, 1, 250> {
public:
  KP184() {
    setAddress(def_devaddr);
  }

  typedef enum {
    MODE_CV = 0,
    MODE_CC = 1,
    MODE_CR = 2,
    MODE_CP = 3
  } mode_t;

  static double modeValMin(mode_t mode) {
    static const double minvalues[MODE_CP + 1] = { 0.0, 0.0, 0.0, 0.0 };
    if (mode > MODE_CP) return NAN;
    return minvalues[mode];
  }

  static double modeValMax(mode_t mode) {
    static const double maxvalues[MODE_CP + 1] = { 150.0, 40.0, 9999.9, 400.0 };
    if (mode > MODE_CP) return NAN;
    return maxvalues[mode];
  }

  static const char* modeStr(mode_t mode) {
    static const char *modestr[MODE_CP + 1] = { "CV", "CC", "CR", "CP" };
    if (mode > MODE_CP) return "N/A";
    return modestr[mode];
  }

  static const char* modeUnit(mode_t mode) {
    static const char *modeunit[MODE_CP + 1] = { "V", "A", "Ohm", "W" };
    if (mode > MODE_CP) return "N/A";
    return modeunit[mode];
  }

  int getStatus(bool &out, mode_t &mode, double &voltage, double &current) {
    int rc;

    if ((rc = getOutput(out, false)))
      return rc;
    getMode(mode, true);
    getVoltage(voltage, true);
    getCurrent(current, true);
    return 0;
  }

  int getOutput(bool &out, bool fromcache = false) {
    if (!fromcache) {
      int rc;

      if ((rc = readStatus()) < 0)
        return rc;
    }

    out = statOutput(statcache[0]);
    return 0;
  }

  int getMode(mode_t &mode, bool fromcache = false) {
    if (!fromcache) {
      int rc;

      if ((rc = readStatus()) < 0)
        return rc;
    }

    mode = statMode(statcache[0]);
    return 0;
  }

  int getVoltage(double &voltage, bool fromcache = false) {
    if (!fromcache) {
      int rc;

      if ((rc = readStatus()) < 0)
        return rc;
    }

    voltage = (double)((int)statcache[2] << 16 | (int)statcache[3] << 8 | statcache[4]) / 1000.0;

    return 0;
  }

  int getCurrent(double &current, bool fromcache = false) {
    if (!fromcache) {
      int rc;

      if ((rc = readStatus()) < 0)
        return rc;
    }

    current = (double)((int)statcache[5] << 16 | (int)statcache[6] << 8 | statcache[7]) / 1000.0;

    return 0;
  }

  int getPower(double &power, bool fromcache = false) {
    int rc;
    double voltage;
    double current;

    if ((rc = getVoltage(voltage, fromcache)))
      return rc;
    getCurrent(current, true);

    power = voltage * current;

    return 0;
  }

  int setOutput(bool on) {
    int32_t val = 0;

    if (on)
      val = 1;

    return presetSingleRegister(REG_ONOFF, val);
  }

  int setMode(mode_t mode) {
    int32_t val = mode;

    if (val > MODE_CP)
      return -EINVAL;

    return presetSingleRegister(REG_MODE, val);
  }

  int setVoltage(double voltage) {
    int32_t val;

    if ((voltage < modeValMin(MODE_CV)) ||
        (voltage > modeValMax(MODE_CV)))
      return -EINVAL;

    val = (int32_t)(voltage * 1000.0);

    return presetSingleRegister(REG_SETCV, val);
  }

  int setCurrent(double current) {
    int32_t val;

    if ((current < modeValMin(MODE_CC)) ||
        (current > modeValMax(MODE_CC)))
      return -EINVAL;

    val = (int32_t)(current * 1000.0);

    return presetSingleRegister(REG_SETCC, val);
  }

  int setResistance(double resistance) {
    int32_t val;

    if ((resistance < modeValMin(MODE_CR)) ||
        (resistance > modeValMax(MODE_CR)))
      return -EINVAL;

    val = (int32_t)(resistance * 10.0);

    return presetSingleRegister(REG_SETCR, val);
  }

  int setPower(double power) {
    int32_t val;

    if ((power < modeValMin(MODE_CP)) ||
        (power > modeValMax(MODE_CP)))
      return -EINVAL;

    val = (int32_t)(power * 100.0);

    return presetSingleRegister(REG_SETCW, val);
  }

  int setModeValue(mode_t mode, double value) {
    switch(mode) {
    case MODE_CV: return setVoltage(value);
    case MODE_CC: return setCurrent(value);
    case MODE_CR: return setResistance(value);
    case MODE_CP: return setPower(value);
    default: break;
    }

    return -EINVAL;
  }

private:
  typedef enum {
    REG_ONOFF = 0x010E,
    REG_MODE  = 0x0110,
    REG_SETCV = 0x0112,
    REG_SETCC = 0x0116,
    REG_SETCR = 0x011A,
    REG_SETCW = 0x011E,
    REG_MEASU = 0x0122,
    REG_MEASI = 0x0126,
    REG_STAT  = 0x0300
  } regaddr_t;

  static bool statOutput(unsigned char byte) { return (byte & 0x01) != 0; };
  static mode_t statMode(unsigned char byte) { return (mode_t)((byte >> 1) & 0x03); };

  ssize_t readStatus() {
    ssize_t ret;
    size_t slen;
    uint8_t sbuf[8], rbuf[max_msglen];

    slen = IOheader(sbuf, OP_READAO, REG_STAT, 0);
    ret = (int)doIO(sbuf, slen, rbuf, sizeof(rbuf));
    if (ret < 0)
      return ret;
    if (ret < 11)
      return -ENODATA;
    if (rbuf[0] != getAddress())
      return -EFAULT;
    if (rbuf[1] != OP_READAO)
      return -ENOMSG;
    // rely on recv'd len to support buggy KP184 protocol
    ret -= 3;
    if ((size_t)ret > sizeof(statcache))
      return -ENOBUFS;
    memcpy(statcache, rbuf + 3, ret);

    return ret;
  }

  using mbRTU::presetSingleRegister;
  int presetSingleRegister(regaddr_t reg, int32_t val) {
    int rc;
    size_t slen;
    uint8_t sbuf[16], rbuf[16];

    slen = IOheader(sbuf, OP_WRITE1AO, reg, 1);
    // KP184 is not conforming at all
    sbuf[slen++] = 4;
    sbuf[slen++] = (uint8_t)((val >> 24) & 0xFF);
    sbuf[slen++] = (uint8_t)((val >> 16) & 0xFF);
    sbuf[slen++] = (uint8_t)((val >> 8) & 0xFF);
    sbuf[slen++] = (uint8_t)(val & 0xFF);
    rc = (int)doIO(sbuf, slen, rbuf, sizeof(rbuf));
    if (rc < 0)
      return rc;
    if (rc != 7)
      return -ENODATA;
    if (rbuf[0] != getAddress())
      return -EFAULT;
    if (rbuf[1] != OP_WRITE1AO)
      return -ENOMSG;
    if (memcmp(rbuf + 2, sbuf + 2, 4) != 0) // addr + code + reg[2] + val[2]
      return -ENODATA;

    return 0;
  }

  unsigned char statcache[18];
};

#endif /* _KP184_H */
