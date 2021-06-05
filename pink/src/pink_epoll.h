// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#ifndef PINK_SRC_PINK_EPOLL_H_
#define PINK_SRC_PINK_EPOLL_H_
#include <queue>
#include "sys/epoll.h"

#include "pink/src/pink_item.h"
#include "slash/include/slash_mutex.h"

namespace pink {

// same layout to epoll_event
struct PinkFiredEvent {
  uint32_t mask;
  union { int fd; void* ptr; };
} __EPOLL_PACKED;

class PinkConn;
class PinkEpoll {
 public:
  static const int kUnlimitedQueue = -1;
  PinkEpoll(int queue_limit = kUnlimitedQueue);
  ~PinkEpoll();
  int PinkAddEvent(const int fd, const int mask);
  int PinkDelEvent(const int fd);
  int PinkModEvent(const int fd, const int old_mask, const int mask);

  int PinkAddEvent(PinkConn*, const int mask);
  int PinkDelEvent(PinkConn*);
  int PinkModEvent(PinkConn*, const int old_mask, const int mask);

  int PinkPoll(const int timeout);

  PinkFiredEvent* firedevent() { return events_; }

  int notify_receive_fd() {
    return notify_receive_fd_;
  }
  int notify_send_fd() {
    return notify_send_fd_;
  }
  PinkItem notify_queue_pop();

  bool Register(PinkItem&& it, bool force);
  bool Deregister(const PinkItem& it) { return false; }

 private:
  int epfd_;
  int timeout_;

  /*
   * The PbItem queue is the fd queue, receive from dispatch thread
   */
  int queue_limit_;
  slash::Mutex notify_queue_protector_;
  std::queue<PinkItem> notify_queue_;

  /*
   * These two fd receive the notify from dispatch thread
   */
  int notify_receive_fd_;
  int notify_send_fd_;

  const static size_t MAX_EVENTS = 1024;
  PinkFiredEvent events_[MAX_EVENTS];
};

}  // namespace pink
#endif  // PINK_SRC_PINK_EPOLL_H_
