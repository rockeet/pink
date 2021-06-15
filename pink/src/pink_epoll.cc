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
#include "terark/hash_common.hpp"
#include "terark/circular_queue.hpp"
#include "terark/util/concurrent_queue.hpp"
//#include <boost/lockfree/queue.hpp>

namespace pink {

PinkItem::PinkItem(const int fd, const std::string &ip_port, NotifyType type) {
  memset(this, 0, sizeof(*this));
  fd_ = fd;
  notify_type_ = type;
  TERARK_VERIFY_LT(ip_port.size(), sizeof(ip_port_));
  memcpy(ip_port_, ip_port.c_str(), ip_port.size()+1);
}
PinkItem::~PinkItem() = default; // for shared_ptr<PinkConn>
void PinkItem::kill_sp_conn() {
  std::shared_ptr<class PinkConn> killer(std::move(conn));
}
void PinkItem::sp_conn_moved() {
  char moved[sizeof(std::shared_ptr<class PinkConn>)];
  new (moved) std::shared_ptr<class PinkConn>(std::move(conn));
}

using PinkItemStore   = std::aligned_storage_t<sizeof(PinkItem)>;
#if 1
using terark::util::concurrent_queue;
using terark::circular_queue;
using NotifyQueueBase = concurrent_queue<circular_queue<PinkItemStore, true> >;
#else
using FixedSized      = boost::lockfree::fixed_sized<true>;
using NotifyQueueBase = boost::lockfree::queue<PinkItemStore, FixedSized>;
#endif

class NotifyQueue : public NotifyQueueBase {
public:
  explicit NotifyQueue(size_t cap)
      : NotifyQueueBase(terark::__hsm_align_pow2(cap)-1) {
    this->queue().init(terark::__hsm_align_pow2(cap));
  }
};

PinkEpoll::PinkEpoll(int queue_limit) : timeout_(1000) {
  epfd_ = epoll_create1(EPOLL_CLOEXEC);
  if (epfd_ < 0) {
    log_err("epoll create fail");
    exit(1);
  }
  notify_queue_.reset(new NotifyQueue(queue_limit));
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
  notify_queue_->push_back(reinterpret_cast<PinkItemStore&>(it));
  it.sp_conn_moved(); // it has been moved to pipe queue
  return true;
}

int PinkEpoll::PopAllNotify(PinkItem* vec, int cap) {
  auto store = reinterpret_cast<PinkItemStore*>(vec);
  size_t n = notify_queue_->pop_front_n_nowait(store, cap);
  return int(n);
}

int PinkEpoll::PinkPoll(const int timeout) {
  static_assert(sizeof(PinkFiredEvent) == sizeof(epoll_event));
  static_assert(offsetof(PinkFiredEvent, mask) == offsetof(epoll_event, events));
  static_assert(offsetof(PinkFiredEvent, fd) == offsetof(epoll_event, data.fd));
  return epoll_wait(epfd_, (epoll_event*)events_, MAX_EVENTS, timeout);
}

}  // namespace pink
