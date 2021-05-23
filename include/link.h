#ifndef _LINK_H
#define _LINK_H

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cerrno>
#include <ctime>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

typedef int sock_t;

class Link
{
public:
  typedef enum {
    NONE = 0,
    SERIAL,
    SOCKET
  } linktype_t;

  typedef enum {
    TIMEOUT_SEND = 1,
    TIMEOUT_RECV = 2,
  } timeout_t;

  typedef enum {
    QUEUE_IN = 1,
    QUEUE_OUT = 2,
    QUEUE_INOUT = 3
  } queue_t;

  Link() :
    m_fd(-1)
  , m_type(NONE)
  , m_timeout_send({ 2, 0 })
  , m_timeout_recv({ 0, 500000L }) {
  }

  ~Link() {
    close();
  }

  virtual int openSocket(const char addr[]) {
    const char *service = "8899";
    char *maddr, *host;
    sock_t sockfd;
    int rc;

    if ((host = maddr = strdup(addr)) == NULL) {
      perror("strdup");
      return -1;
    }

    // Separate host and port
    if (host[0] == '[') {
      host++;
      char *p = strchr(host + 1, ']');
      if (p) {
        *p++ = '\0';
        if (*p++ == ':') {
          service = p;
        }
      } else {
        fprintf(stderr, "Bad IPv6 addr specification: %s\n", addr);
        free(maddr);
        return -1;
      }
    } else {
      char *p = strchr(host, ':');
      if (p) {
        if (strchr(p + 1, ':') == NULL) { /* not an IPv6 addr */
          *p++ = '\0';
          service = p;
        }
      }
    }

    struct addrinfo *ai;
    struct addrinfo *aiptr;
    struct addrinfo hints;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    rc = getaddrinfo(host, service, &hints, &ai);

    free(maddr), service = host = maddr = NULL;

    if (rc != 0) {
      fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(rc));
      return -EFAULT;
    }

    for (aiptr = ai; aiptr != NULL; aiptr = aiptr->ai_next) {
      if ((sockfd = create_socket(aiptr->ai_family)) < 0) {
        rc = -errno;
        freeaddrinfo(ai);
        perror("create socket failed");
        return rc;
      }

      if ((rc = try_connect(sockfd, aiptr->ai_addr,
                aiptr->ai_addrlen)) != 0) {
        rc = -errno;
        ::close(sockfd);
        continue; /* next addr */
      }

      break;
    }

    freeaddrinfo(ai);

    if (aiptr == NULL) {
      fprintf(stderr, "Server connection fail: %s\n", strerror(errno));
      return rc;
    }

    if (m_fd >= 0)
      ::close(m_fd);

    m_fd = sockfd;
    m_type = SOCKET;
    m_addrstr = addr;
    m_confstr.erase();

    return 0;
  }

  // config format is baud_rate,char_size,parity,stop_bits
  virtual int openSerial(const char path[], const char config[]) {
    /* baud mapping code is from uucp */
    /* A table to map baud rates into index numbers.  */

    static struct sbaud_table {
      speed_t icode;
      long ibaud;
    } asSbaud_table[] = {
      { B50, 50 },
      { B75, 75 },
      { B110, 110 },
      { B134, 134 },
      { B150, 150 },
      { B200, 200 },
      { B300, 300 },
      { B600, 600 },
      { B1200, 1200 },
      { B1800, 1800 },
      { B2400, 2400 },
      { B4800, 4800 },
      { B9600, 9600 },
#ifdef B19200
      { B19200, 19200 },
#else /* ! defined (B19200) */
#ifdef EXTA
      { EXTA, 19200 },
#endif /* EXTA */
#endif /* ! defined (B19200) */
#ifdef B38400
      { B38400, 38400 },
#else /* ! defined (B38400) */
#ifdef EXTB
      { EXTB, 38400 },
#endif /* EXTB */
#endif /* ! defined (B38400) */
#ifdef B57600
      { B57600, 57600 },
#endif
#ifdef B76800
      { B76800, 76800 },
#endif
#ifdef B115200
      { B115200, 115200 },
#endif
#ifdef B230400
      { B230400, 230400 },
#else
#ifdef _B230400
      { _B230400, 230400 },
#endif /* _B230400 */
#endif /* ! defined (B230400) */
#ifdef B460800
      { B460800, 460800 },
#else
#ifdef _B460800
      { _B460800, 460800 },
#endif /* _B460800 */
#endif /* ! defined (B460800) */
#ifdef B500000
      { B500000, 500000 },
#endif
#ifdef B576000
      { B576000, 576000 },
#endif
#ifdef B921600
      { B921600, 921600 },
#endif
#ifdef B1000000
      { B1000000, 1000000 },
#endif
#ifdef B1152000
      { B1152000, 1152000 },
#endif
#ifdef B1500000
      { B1500000, 1500000 },
#endif
#ifdef B2000000
      { B2000000, 2000000 },
#endif
#ifdef B2500000
      { B2500000, 2500000 },
#endif
#ifdef B3000000
      { B3000000, 3000000 },
#endif
#ifdef B3500000
      { B3500000, 3500000 },
#endif
#ifdef B4000000
      { B4000000, 4000000 },
#endif
      { B0, 0 }
    };

#define CBAUD_TABLE ((int)(sizeof asSbaud_table / sizeof asSbaud_table[0]))

    int serfd, rc = -EINVAL;
    struct termios sattr;
    speed_t cbaud = B115200;

    serfd = ::open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (serfd == -1) {
      rc = -errno;
      fprintf(stderr, "%s: Unable to open: %s.\n", path, strerror(errno));
      return rc;
    }

    if (tcgetattr(serfd, &sattr)) {
      rc = -errno;
      perror("tcgetattr");
      goto serfail;
    }

    // default config
    sattr.c_cflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    // set to 8-bits, no parity, 1 stop bit
    sattr.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    sattr.c_cflag &= ~CSIZE;
    sattr.c_cflag |= CS8;
    sattr.c_cflag |= (CLOCAL | CREAD);

    sattr.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

    sattr.c_oflag = 0; // no remapping, no delays

    while (config) {
      char *eptr;
      int i;
      long ibaud;

      ibaud = strtol(config, &eptr, 10);
      if (ibaud <= 0) {
        fprintf(stderr, "Invalid baud rate definition %ld\n", ibaud);
        goto serfail;
      }
      for(i = 0; i < CBAUD_TABLE && ibaud != asSbaud_table[i].ibaud; i++);
      if (i >= CBAUD_TABLE) {
        fprintf(stderr, "Baud rate %ld is unsupported\n", ibaud);
        goto serfail;
      }
      cbaud = asSbaud_table[i].icode;

      if (*eptr == '\0') break;
      if (*eptr++ != ',' || *eptr == '\0') {
        fprintf(stderr, "Invalid port configuration string %s\n", config);
        goto serfail;
      }

      sattr.c_cflag &= ~CSIZE;
      switch(*eptr) {
#ifdef CS5
      case '5': sattr.c_cflag |= CS5; break;
#endif
#ifdef CS6
      case '6': sattr.c_cflag |= CS6; break;
#endif
#ifdef CS7
      case '7': sattr.c_cflag |= CS7; break;
#endif
      case '8': sattr.c_cflag |= CS8; break;
      default: fprintf(stderr, "Character size %c is unsupported\n", *eptr);
        goto serfail;
      }
      eptr++;

      if (*eptr == '\0') break;
      if (*eptr++ != ',' || *eptr == '\0') {
        fprintf(stderr, "Invalid port configuration string %s\n", config);
        goto serfail;
      }

      sattr.c_cflag &= ~(PARENB | PARODD);
      switch(*eptr) {
      case 'N':
      case 'n': break;
      case 'E':
      case 'e': sattr.c_cflag |= PARENB; break;
      case 'O':
      case 'o': sattr.c_cflag |= PARENB | PARODD; break;
      default:
        fprintf(stderr, "Parity definition %c is unsupported\n", *eptr);
        goto serfail;
      }
      eptr++;

      if (*eptr == '\0') break;
      if (*eptr++ != ',' || *eptr == '\0') {
        fprintf(stderr, "Invalid port configuration string %s\n", config);
        goto serfail;
      }

      switch(*eptr) {
      case '1': sattr.c_cflag &= ~CSTOPB; break;
      case '2': sattr.c_cflag |= CSTOPB; break;
      default:
        fprintf(stderr, "Stop bits definition %c is unsupported\n", *eptr);
        goto serfail;
      }
      eptr++;

      if (*eptr != '\0')
        fprintf(stderr, "Excessive port configuration string\n");

      break;
    }

    // set speed of port
    if (cfsetispeed(&sattr, cbaud) || cfsetospeed(&sattr, cbaud)) {
      rc = -errno;
      fprintf(stderr, "%s: Unable to set port rate: %s\n", path, strerror(errno));
      goto serfail;
    }

    tcflush(serfd, TCIOFLUSH);
    if (tcsetattr(serfd, TCSANOW, &sattr)) {
      rc = -errno;
      perror("tcsetattr");
      goto serfail;
    }

    if (m_fd >= 0)
      ::close(m_fd);

    m_fd = serfd;
    m_type = SERIAL;
    m_addrstr = path;
    m_confstr = config;

    return 0;

serfail:
    ::close(serfd);
    return rc;
  }

  virtual int getHandle() { return m_fd; }

  virtual int open(linktype_t type, const char link[], const char config[]) {
    switch(type) {
      case SERIAL: return openSerial(link, config);
      case SOCKET: return openSocket(link);
      default: break;
    }
    return -EINVAL;
  }

  virtual int reOpen() {
    ::close(m_fd), m_fd = -1;
    return open(m_type, m_addrstr.c_str(), m_confstr.c_str());
  }

  virtual int close() {
    int rc;
    if (m_fd == -1)
      return 0;

    rc = ::close(m_fd);
    if (rc == 0) {
      m_fd = -1;
      m_type = NONE;
      m_addrstr.erase();
      m_confstr.erase();
    }

    return rc;
  }

  virtual int flush(queue_t queue) {
    int tcqsel;

    if (m_fd < 0)
      return -ENXIO;

    if (m_type != SERIAL)
      return 0;

    switch (queue) {
    case QUEUE_IN: tcqsel = TCIFLUSH; break;
    case QUEUE_OUT: tcqsel = TCOFLUSH; break;
    case QUEUE_INOUT: tcqsel = TCIOFLUSH; break;
    default:
      return -EINVAL;
    }

    if (tcflush(m_fd, tcqsel) < 0)
      return -errno;

    return 0;
  }

  virtual ssize_t send(const uint8_t buf[], size_t len) {
    fd_set write_fd;
    struct timeval timeout;
    ssize_t rc = -EFAULT;

    if (m_fd < 0)
      return -ENXIO;

    memcpy(&timeout, &m_timeout_send, sizeof(timeout));
    do {
      FD_ZERO (&write_fd);
      FD_SET (m_fd, &write_fd);

      if ((rc = select (m_fd + 1, NULL, &write_fd, NULL, &timeout)) < 0) {
        rc = -errno;
        if (errno == EINTR)
          continue;
        perror ("select");
        return rc;
      } else if (rc == 0) { // timeout
        rc = -ETIMEDOUT;
      } else { // we have only one descriptor
        rc = write(m_fd, buf, len);
        if (rc < 0) {
          return -errno;
        }
      }
    }  while(0);

    return rc;
  }

  virtual ssize_t recv(uint8_t buf[], size_t size) {
    fd_set read_fd;
    struct timeval timeout;
    ssize_t rc = -EFAULT;

    if (m_fd < 0)
      return -ENXIO;

    memcpy(&timeout, &m_timeout_recv, sizeof(timeout));
    do {
      FD_ZERO (&read_fd);
      FD_SET (m_fd, &read_fd);

      if ((rc = select (m_fd + 1, &read_fd, NULL, NULL, &timeout)) < 0) {
        if (errno == EINTR)
          continue;
        rc = -errno;
        perror ("select");
        return rc;
      } else if (rc == 0) { // timeout
        rc = -ETIMEDOUT;
      } else { // we have only one descriptor
        rc = read(m_fd, buf, size);
        if (rc < 0)
          return -errno;
      }
    }  while(0);

    return rc;
  }

  virtual void setTimeout(int ms, timeout_t sel) {
    struct timeval tv;
    tv.tv_sec = (time_t)ms / 1000;
    tv.tv_usec = (time_t)ms % 1000 * 1000;

    if (sel & TIMEOUT_SEND)
      memcpy(&m_timeout_send, &tv, sizeof(m_timeout_send));
    if (sel & TIMEOUT_RECV)
      memcpy(&m_timeout_recv, &tv, sizeof(m_timeout_recv));
  }

  virtual linktype_t getLinkType() { return m_type; }

  static const char *linkTypeStr(linktype_t type) {
    const char* linktypestr[SOCKET + 1] = { "none", "serial", "socket" };
    if (type > SOCKET) return "N/A";
    return linktypestr[type];
  }

private:
  // following code parts are from apcupsd software
  /*
   * Creates socket of specified address family and makes it non-blocking
   * Returns -errno on error
   * Returns socket file descriptor otherwise
   */
  static sock_t create_socket(unsigned short family) {
    sock_t sockfd;
    int nonblock = 1;
    int rc;

    /* Open a TCP socket */
    if ((sockfd = socket(family, SOCK_STREAM | SOCK_CLOEXEC, 0)) < 0) {
     return -errno;
    }

    /* Set socket to non-blocking mode */
    if (ioctl(sockfd, FIONBIO, &nonblock) != 0) {
      rc = -errno;
      ::close(sockfd);
      return rc;
    }

    return sockfd;
  }

  /*
   * Try to connect specified sockfd to specified addr
   * Returns -errno on error
   * Returns 0 if connection was successfull
   */
  static int try_connect(sock_t sockfd, struct sockaddr *addr, socklen_t addrlen) {
    int rc;

    /* Initiate connection attempt */
    rc = connect(sockfd, addr, addrlen);
    if (rc == -1 && errno != EINPROGRESS) {
      return -errno;
    }

    /* If connection is in progress, wait for it to complete */
    if (rc == -1) {
      struct timeval timeout;
      fd_set fds;
      int err;
      socklen_t errlen = sizeof(err);

      do {
        /* Expect connection within 5 seconds */
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        FD_ZERO(&fds);
        FD_SET(sockfd, &fds);

        /* Wait for connection to complete */
        rc = select(sockfd + 1, NULL, &fds, NULL, &timeout);
        switch (rc) {
        case -1: /* select error */
          if (errno == EINTR || errno == EAGAIN)
            continue;
          err = -errno;
          return err;
        case 0: /* timeout */
          return -ETIMEDOUT;
        }
      }
      while (rc == -1 && (errno == EINTR || errno == EAGAIN));
      /* Connection completed? Check error status. */
      if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &err, &errlen) == -1) {
        rc = -errno;
        return rc;
      }
      if (errlen != sizeof(err)) {
        return -EINVAL;
      }
      if (err) {
        return -err;
      }
    }

    return 0;
  }
  // apcupsd code parts ends here

  int m_fd;
  linktype_t m_type;
  std::string m_addrstr;
  std::string m_confstr;
  struct timeval m_timeout_send;
  struct timeval m_timeout_recv;
};

#endif /* _LINK_H */
