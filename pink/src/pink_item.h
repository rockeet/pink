// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#ifndef PINK_SRC_PINK_ITEM_H_
#define PINK_SRC_PINK_ITEM_H_

#include <assert.h>
#include <string.h>
#include <string>
#include <memory>

#include "pink/include/pink_define.h"

namespace pink {

class PinkItem {
 public:
  PinkItem() {}
  PinkItem(const int fd, const std::string &ip_port, NotifyType type = kNotiConnect);
  ~PinkItem();

  int fd() const {
    return fd_;
  }
  const std::string ip_port() const {
    return ip_port_;
  }

  NotifyType notify_type() const {
    return notify_type_;
  }

  void kill_sp_conn();
  void sp_conn_moved();

  std::shared_ptr<class PinkConn> conn;
 private:
  char ip_port_[16+27];
  NotifyType notify_type_;
  int fd_;
};
static_assert(sizeof(PinkItem) == 64);

}  // namespace pink
#endif  // PINK_SRC_PINK_ITEM_H_
