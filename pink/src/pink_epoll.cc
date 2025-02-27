// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include "pink/src/pink_epoll.h"
#include "pink/include/pink_conn.h"

#include <linux/version.h>
#include <fcntl.h>

#include "pink/include/pink_define.h"
#include "slash/include/xdebug.h"
#include "terark/stdtypes.hpp"

namespace pink {

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
PinkItem::PinkItem(const int fd, const std::string &ip_port, NotifyType type) {
  memset(this, 0, sizeof(*this));
  fd_ = fd;
  notify_type_ = type;
  TERARK_VERIFY_LT(ip_port.size(), sizeof(ip_port_));
  memcpy(ip_port_, ip_port.c_str(), ip_port.size()+1);
}
#pragma GCC diagnostic pop

PinkItem::~PinkItem() = default; // for shared_ptr<PinkConn>
void PinkItem::kill_sp_conn() {
  std::shared_ptr<class PinkConn> killer(std::move(conn));
}
void PinkItem::sp_conn_moved() {
  char moved[sizeof(std::shared_ptr<class PinkConn>)];
  new (moved) std::shared_ptr<class PinkConn>(std::move(conn));
}

PinkEpoll::PinkEpoll(int /*queue_limit*/) {
  epfd_ = epoll_create1(EPOLL_CLOEXEC);
  if (epfd_ < 0) {
    log_err("epoll create fail");
    exit(1);
  }
  int fds[2];
  if (pipe(fds)) {
    exit(-1);
  }
  notify_receive_fd_ = fds[0];
  notify_send_fd_ = fds[1];

  fcntl(notify_receive_fd_, F_SETFD, fcntl(notify_receive_fd_, F_GETFD) | FD_CLOEXEC);
  fcntl(notify_send_fd_, F_SETFD, fcntl(notify_send_fd_, F_GETFD) | FD_CLOEXEC);

  PinkAddEvent(notify_receive_fd_, EPOLLIN | EPOLLERR | EPOLLHUP);
}

PinkEpoll::~PinkEpoll() {
  close(epfd_);
}

int PinkEpoll::PinkAddEvent(const int fd, const int mask) {
  struct epoll_event ee;
  ee.data.fd = fd;
  ee.events = mask;
  return epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ee);
}

int PinkEpoll::PinkModEvent(const int fd, const int old_mask, const int mask) {
  struct epoll_event ee;
  ee.data.fd = fd;
  ee.events = (old_mask | mask);
  return epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ee);
}

int PinkEpoll::PinkDelEvent(const int fd) {
  return epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
}

int PinkEpoll::PinkAddEvent(PinkConn* conn, const int mask) {
  struct epoll_event ee;
  ee.data.ptr = conn;
  ee.events = mask;
  return epoll_ctl(epfd_, EPOLL_CTL_ADD, conn->fd(), &ee);
}

int PinkEpoll::PinkModEvent(PinkConn* conn, const int old_mask, const int mask) {
  struct epoll_event ee;
  ee.data.ptr = conn;
  ee.events = (old_mask | mask);
  return epoll_ctl(epfd_, EPOLL_CTL_MOD, conn->fd(), &ee);
}

int PinkEpoll::PinkDelEvent(PinkConn* conn) {
  return epoll_ctl(epfd_, EPOLL_CTL_DEL, conn->fd(), nullptr);
}

bool PinkEpoll::Register(PinkItem&& it, bool force) {
#if 1
  auto nWrite = write(notify_send_fd_, &it, sizeof(PinkItem));
  TERARK_VERIFY_EQ(nWrite, sizeof(PinkItem));
  it.sp_conn_moved(); // it has been moved to pipe queue
  return true;
#else
  bool success = false;
  notify_queue_protector_.Lock();
  if (force ||
      queue_limit_ == kUnlimitedQueue ||
      notify_queue_.size() < static_cast<size_t>(queue_limit_)) {
    notify_queue_.push(std::move(it));
    success = true;
  }
  notify_queue_protector_.Unlock();
  if (success) {
    write(notify_send_fd_, "", 1);
  }
  return success;
#endif
}

#if 0
PinkItem PinkEpoll::notify_queue_pop() {
  notify_queue_protector_.Lock();
  PinkItem it = std::move(notify_queue_.front());
  notify_queue_.pop();
  notify_queue_protector_.Unlock();
  return std::move(it);
}
#endif

int PinkEpoll::PinkPoll(const int timeout) {
  static_assert(sizeof(PinkFiredEvent) == sizeof(epoll_event));
  static_assert(offsetof(PinkFiredEvent, mask) == offsetof(epoll_event, events));
  static_assert(offsetof(PinkFiredEvent, fd) == offsetof(epoll_event, data.fd));
  return epoll_wait(epfd_, (epoll_event*)events_, MAX_EVENTS, timeout);
}

}  // namespace pink
