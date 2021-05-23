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
  KP184() :
    m_sw(0),
    m_mode(MODE_CV),
    m_cres(100.0),
    m_cpow(10.0),
    m_volt(15.213),
    m_curr(1.0)
  {
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

  int openSocket(const char addr[]) { return 0; }

  int openSerial(const char path[], const char config[]) { return 0; }

  int getHandle() { return 0; }

  int open(linktype_t type, const char link[], const char config[]) { return 0; }

  int reOpen() { return 0; }

  int close() { return 0; }

  int flush(queue_t queue) { return 0; }

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
    out = m_sw;
    return 0;
  }

  int getMode(mode_t &mode, bool fromcache = false) {

    mode = m_mode;
    return 0;
  }

  int getVoltage(double &voltage, bool fromcache = false) {

    voltage = m_volt;

    return 0;
  }

  int getCurrent(double &current, bool fromcache = false) {

    current = m_sw ? m_curr : 0.0;

    return 0;
  }

  int getPower(double &power, bool fromcache = false) {

    power = m_volt * m_curr;

    return 0;
  }

  int setOutput(bool on) {

    m_sw = on;

    return 0;
  }

  int setMode(mode_t mode) {

    m_mode = mode;

    return 0;
  }

  int setVoltage(double voltage) {

    if ((voltage < modeValMin(MODE_CV)) ||
        (voltage > modeValMax(MODE_CV)))
      return -EINVAL;

    m_volt = voltage;

    return 0;
  }

  int setCurrent(double current) {

    if ((current < modeValMin(MODE_CC)) ||
        (current > modeValMax(MODE_CC)))
      return -EINVAL;

    m_curr = current;

    return 0;
  }

  int setResistance(double resistance) {

    if ((resistance < modeValMin(MODE_CR)) ||
        (resistance > modeValMax(MODE_CR)))
      return -EINVAL;

    m_cres = resistance;

    return 0;
  }

  int setPower(double power) {

    if ((power < modeValMin(MODE_CP)) ||
        (power > modeValMax(MODE_CP)))
      return -EINVAL;

    m_cpow = power;

    return 0;
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

  bool m_sw;
  mode_t m_mode;
  double m_cres;
  double m_cpow;
  double m_volt;
  double m_curr;

};

#endif /* _KP184_H */
