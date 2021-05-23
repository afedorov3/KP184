#ifndef _MBRTU_H
#define _MBRTU_H

#include <cstdint>
#include <cstring>
#include <cerrno>
#include <unistd.h>

#ifdef MBDEBUG
#  include "util.h"
#endif
#include "link.h"

typedef uint8_t devaddr_t;
typedef uint16_t regaddr_t;

template <size_t max_msglen_val = 260,
          devaddr_t def_devaddr_val = 1,
          devaddr_t min_devaddr_val = 0,
          devaddr_t max_devaddr_val = 247>
class mbRTU : public Link {
public:
  mbRTU():  m_devaddr(def_devaddr)
          , m_recvdelay(10000)
#ifdef MBDEBUG
          , m_debug(false)
#endif
  {
  }

  virtual int setAddress(devaddr_t devaddr) {
    if ((devaddr < min_devaddr) || (devaddr > max_devaddr))
      return -EINVAL;

    m_devaddr = devaddr;

    return 0;
  }

  virtual devaddr_t getAddress() { return m_devaddr; }

  virtual void setRecvDelay(useconds_t delay) { m_recvdelay = delay; }

#ifdef MBDEBUG
  virtual void setDebug(bool on) { m_debug = on; }

  virtual bool getDebug() { return m_debug; }
#endif

  // returns length of data copied into the buffer or -protocol error
  // if -EPROTO is returned, first byte in the buffer would be recv'd modbus error
  virtual ssize_t readHoldingRegisters(regaddr_t firstreg, uint16_t cnt,
                                    uint8_t buf[], size_t size) {
    ssize_t ret;
    size_t slen;
    uint8_t sbuf[8], rbuf[max_msglen];

    if (size == 0)
      return -ENOBUFS;

    slen = IOheader(sbuf, OP_READAO, firstreg, (int16_t)cnt);
    ret = (int)doIO(sbuf, slen, rbuf, sizeof(rbuf));
    if (ret < 0)
      return ret;
    if (ret < 3)
      return -ENODATA;
    if (rbuf[0] != m_devaddr)
      return -EFAULT;
    if (rbuf[1] == ERR_READAO) {
      buf[0] = rbuf[2];
      return -EPROTO;
    }
    if (rbuf[1] != OP_READAO)
      return -ENOMSG;
    slen = (size_t)ret;
    ret = (ssize_t)rbuf[2];
    if ((size_t)(ret + 3) != slen)
      return -ENODATA;
    if ((size_t)ret > size)
      return -ENOBUFS;

    memcpy(buf, rbuf + 3, ret);

    return ret;
  }

  // returns 0 on success, -protocol error or +modbus error
  virtual int presetSingleRegister(regaddr_t reg, int16_t val) {
    int rc;
    size_t slen;
    uint8_t sbuf[8], rbuf[8];

    slen = IOheader(sbuf, OP_WRITE1AO, reg, val);
    rc = (int)doIO(sbuf, slen, rbuf, sizeof(rbuf));
    if (rc < 0)
      return rc;
    if (rc < 3)
      return -ENODATA;
    if (rbuf[0] != m_devaddr)
      return -EFAULT;
    if (rbuf[1] == ERR_WRITE1AO)
      return (int)rbuf[2];
    if (rbuf[1] != OP_WRITE1AO)
      return -ENOMSG;
    if ((rc != 6) || memcmp(rbuf + 2, sbuf + 2, 4) != 0) // addr + code + reg[2] + val[2]
      return -ENODATA;

    return 0;
  }

  static devaddr_t defAddress() { return def_devaddr; }
  static devaddr_t minAddress() { return min_devaddr; }
  static devaddr_t maxAddress() { return max_devaddr; }
  static uint16_t CRC16(const uint8_t buf[], size_t len) {
    uint16_t crc = 0xFFFF;

    if(len == 0)
      return 0;

    while (len--) {
      crc ^= (uint16_t)*buf;
      for(int i = 0; i < 8; i++) {
        if (crc & 1) {
          crc >>= 1;
          crc ^= 0xA001;
        } else
          crc >>= 1;
      }
      buf++;
    }

    return crc;
  }

  // len is length of payload in the frame (excl. CRC)
  // returns total frame length (inc. CRC)
  static size_t addCRC(uint8_t buf[], size_t len) {
    uint16_t crc = CRC16(buf, len);
    buf[len++] = (uint8_t)((crc >> 8) & 0xFF);
    buf[len++] = (uint8_t)(crc & 0xFF);
    return len;
  }

  // len is full length of the frame (inc. CRC)
  // returns payload length on match (excl. CRC), -1 if no match
  static ssize_t checkCRC(const uint8_t buf[], size_t len) {
    if (len <= 2)
      return -ENODATA;

    len -= 2;
    uint16_t crc = CRC16(buf, len);
    if (buf[len] == (uint8_t)((crc >> 8) & 0xFF) &&
        buf[len + 1] == (uint8_t)(crc & 0xFF))
      return len;

    return -EIO;
  }

protected:
  typedef enum {
    OP_READAO = 0x03,
    ERR_READAO = OP_READAO | 0x80,
    OP_WRITE1AO = 0x06,
    ERR_WRITE1AO = OP_WRITE1AO | 0x80
  } opcode_t;

  static const size_t max_msglen = max_msglen_val;
  static const devaddr_t def_devaddr = def_devaddr_val;
  static const devaddr_t min_devaddr = min_devaddr_val;
  static const devaddr_t max_devaddr = max_devaddr_val;

  virtual size_t IOheader(uint8_t buf[], opcode_t code, regaddr_t reg, int16_t cv) {
    uint8_t *ptr = buf;
    *ptr++ = (uint8_t)m_devaddr;
    *ptr++ = (uint8_t)code;
    *ptr++ = (uint8_t)((reg >> 8) & 0xFF); *ptr++ = (uint8_t)(reg & 0xFF);
    *ptr++ = (uint8_t)((cv >> 8) & 0xFF); *ptr++ = (uint8_t)(cv & 0xFF);

    return (ptr - buf);
  }

  // len is total send payload length (excl. CRC)
  // returns recv'd payload length (excl. CRC)
  virtual ssize_t doIO(uint8_t sbuf[], size_t len, uint8_t rbuf[], size_t size) {
    ssize_t ret;

    if (len == 0)
      return -EINVAL;

    len = addCRC(sbuf, len);
#ifdef MBDEBUG
    if (m_debug)
      Util::printbuf(sbuf, len, "sent");
#endif

    flush(Link::QUEUE_IN);
    ret = send(sbuf, len);
    if (ret < 0)
      return ret;
    if ((size_t)ret == len) {
      usleep(m_recvdelay);
      if ((ret = recv(rbuf, size)) >= 0) {
#ifdef MBDEBUG
        if (m_debug)
          Util::printbuf(rbuf, ret, "recv'd");
#endif
        if (ret > 2)
          ret = checkCRC(rbuf, ret);
        else
          ret = -ENODATA;
      }
    }

    return ret;
  }

private:
  devaddr_t m_devaddr;
  useconds_t m_recvdelay;
#ifdef MBDEBUG
  bool m_debug;
#endif
};

#endif /* _MBRTU_H */
