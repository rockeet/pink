// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include "pink/include/pink_cli.h"

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>

namespace pink {

struct PinkCli::Rep {
  std::string peer_ip;
  int peer_port;
  int send_timeout;
  int recv_timeout;
  int connect_timeout;
  bool keep_alive;
  bool is_block;
  int sockfd;
  bool available;

  Rep() : send_timeout(0),
      recv_timeout(0),
      connect_timeout(1000),
      keep_alive(0),
      is_block(true),
      sockfd(-1),
      available(false) {
      }

  Rep(const std::string& ip, int port)
    : peer_ip(ip),
      peer_port(port),
      send_timeout(0),
      recv_timeout(0),
      connect_timeout(1000),
      keep_alive(0),
      is_block(true),
      sockfd(-1),
      available(false) {
      }
};

PinkCli::PinkCli(const std::string& ip, const int port)
  : rep_(new Rep(ip, port)) {
}

PinkCli::~PinkCli() {
  Close();
  delete rep_;
}

bool PinkCli::Available() const {
  return rep_->available;
}

Status PinkCli::Connect(const std::string &bind_ip) {
  return Connect(rep_->peer_ip, rep_->peer_port, bind_ip);
}

Status PinkCli::Connect(const std::string &ip, const int port,
    const std::string &bind_ip) {
  Rep* r = rep_;
  Status s;
  int rv;
  char cport[6];
  struct addrinfo hints, *servinfo, *p;
  snprintf(cport, sizeof(cport), "%d", port);
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  // We do not handle IPv6
  if ((rv = getaddrinfo(ip.c_str(), cport, &hints, &servinfo)) != 0) {
    return Status::IOError("connect getaddrinfo error for ", ip);
  }
  for (p = servinfo; p != NULL; p = p->ai_next) {
    if ((r->sockfd = socket(
            p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      continue;
    }

    // bind if needed
    if (!bind_ip.empty()) {
      struct sockaddr_in localaddr;
      localaddr.sin_family = AF_INET;
      localaddr.sin_addr.s_addr = inet_addr(bind_ip.c_str());
      localaddr.sin_port = 0;  // Any local port will do
      bind(r->sockfd, (struct sockaddr *)&localaddr, sizeof(localaddr));
    }


    int flags = fcntl(r->sockfd, F_GETFL, 0);
    fcntl(r->sockfd, F_SETFL, flags | O_NONBLOCK);
    fcntl(r->sockfd, F_SETFD, fcntl(r->sockfd, F_GETFD) | FD_CLOEXEC);

    if (connect(r->sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      if (errno == EHOSTUNREACH) {
        close(r->sockfd);
        continue;
      } else if (errno == EINPROGRESS ||
                 errno == EAGAIN ||
                 errno == EWOULDBLOCK) {
        struct pollfd wfd[1];

        wfd[0].fd = r->sockfd;
        wfd[0].events = POLLOUT;

        int res;
        if ((res = poll(wfd, 1, r->connect_timeout)) == -1) {
          close(r->sockfd);
          freeaddrinfo(servinfo);
          return Status::IOError("EHOSTUNREACH", "connect poll error");
        } else if (res == 0) {
          close(r->sockfd);
          freeaddrinfo(servinfo);
          return Status::Timeout("");
        }
        int val = 0;
        socklen_t lon = sizeof(int);

        if (getsockopt(r->sockfd, SOL_SOCKET, SO_ERROR, &val, &lon) == -1) {
          close(r->sockfd);
          freeaddrinfo(servinfo);
          return Status::IOError("EHOSTUNREACH",
                                 "connect host getsockopt error");
        }

        if (val) {
          close(r->sockfd);
          freeaddrinfo(servinfo);
          return Status::IOError("EHOSTUNREACH", "connect host error");
        }
      } else {
        close(r->sockfd);
        freeaddrinfo(servinfo);
        return Status::IOError("EHOSTUNREACH",
                               "The target host cannot be reached");
      }
    }

    struct sockaddr_in laddr;
    socklen_t llen = sizeof(laddr);
    getsockname(r->sockfd, (struct sockaddr*) &laddr, &llen);
    std::string lip(inet_ntoa(laddr.sin_addr));
    int lport = ntohs(laddr.sin_port);
    if (ip == lip && port == lport) {
      return Status::IOError("EHOSTUNREACH", "same ip port");
    }

    flags = fcntl(r->sockfd, F_GETFL, 0);
    fcntl(r->sockfd, F_SETFL, flags & ~O_NONBLOCK);
    freeaddrinfo(servinfo);

    // connect ok
    rep_->available = true;
    return s;
  }
  if (p == NULL) {
    s = Status::IOError(strerror(errno), "Can't create socket ");
    return s;
  }
  freeaddrinfo(servinfo);
  freeaddrinfo(p);
  set_tcp_nodelay();
  return s;
}

static int PollFd(int fd, int events, int ms) {
  pollfd fds[1];
  fds[0].fd = fd;
  fds[0].events = events;
  fds[0].revents = 0;

  int ret = ::poll(fds, 1, ms);
  if (ret > 0) {
      return fds[0].revents;
  }

  return ret;
}

static int CheckSockAliveness(int fd) {
  char buf[1];
  int ret;

  ret = PollFd(fd, POLLIN | POLLPRI, 0);
  if (0 < ret) {
    int num = ::recv(fd, buf, 1, MSG_PEEK);
    if (num == 0) {
      return -1;
    }
    if (num == -1) {
      int errnum = errno;
      if (errnum != EINTR && errnum != EAGAIN && errnum != EWOULDBLOCK) {
        return -1;
      }
    }
  }

  return 0;
}

int PinkCli::CheckAliveness() {
  int flag;
  bool block;
  int sock = fd();

  if (sock < 0) {
    return -1;
  }

  flag = fcntl(sock, F_GETFL, 0);
  block = !(flag & O_NONBLOCK);
  if (block) {
    fcntl(sock, F_SETFL, flag | O_NONBLOCK);
  }

  int ret = CheckSockAliveness(sock);

  if (block) {
    fcntl(sock, F_SETFL, flag);
  }

  return ret;
}

Status PinkCli::SendRaw(void *buf, size_t count) {
  char* wbuf = reinterpret_cast<char*>(buf);
  size_t nleft = count;
  int pos = 0;
  ssize_t nwritten;

  while (nleft > 0) {
    if ((nwritten = send(rep_->sockfd, wbuf + pos, nleft, 0)) < 0) {
      if (errno == EINTR) {
        continue;
      } else if (errno == EAGAIN) {
        return Status::Timeout("Send timeout");
      } else {
        return Status::IOError("write error " + std::string(strerror(errno)));
      }
    } else if (nwritten == 0) {
      return Status::IOError("write nothing");
    }

    nleft -= nwritten;
    pos += nwritten;
  }

  return Status::OK();
}

Status PinkCli::RecvRaw(void *buf, size_t *count) {
  Rep* r = rep_;
  char* rbuf = reinterpret_cast<char*>(buf);
  size_t nleft = *count;
  size_t pos = 0;
  ssize_t nread;

  while (nleft > 0) {
    if ((nread = read(r->sockfd, rbuf + pos, nleft)) < 0) {
      if (errno == EINTR) {
        continue;
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return Status::Timeout("Send timeout");
      } else {
        return Status::IOError("read error " + std::string(strerror(errno)));
      }
    } else if (nread == 0) {
      return Status::EndFile("socket closed");
    }
    nleft -= nread;
    pos += nread;
  }

  *count = pos;
  return Status::OK();
}

int PinkCli::fd() const {
  return rep_->sockfd;
}

void PinkCli::Close() {
  if (rep_->available) {
    close(rep_->sockfd);
    rep_->available = false;
    rep_->sockfd = -1;
  }
}

void PinkCli::set_connect_timeout(int connect_timeout) {
  rep_->connect_timeout = connect_timeout;
}

int PinkCli::set_send_timeout(int send_timeout) {
  Rep* r = rep_;
  int ret = 0;
  if (send_timeout > 0) {
    r->send_timeout = send_timeout;
    struct timeval timeout =
        {r->send_timeout / 1000, (r->send_timeout % 1000) * 1000};
    ret = setsockopt(
        r->sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  }
  return ret;
}

int PinkCli::set_recv_timeout(int recv_timeout) {
  Rep* r = rep_;
  int ret = 0;
  if (recv_timeout > 0) {
    r->recv_timeout = recv_timeout;
    struct timeval timeout =
        {r->recv_timeout / 1000, (r->recv_timeout % 1000) * 1000};
    ret = setsockopt(
        r->sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  }
  return ret;
}

int PinkCli::set_tcp_nodelay() {
  Rep* r = rep_;
  int val = 1;
  int ret = 0;
  ret = setsockopt(r->sockfd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
  return ret;
}

}  // namespace pink
