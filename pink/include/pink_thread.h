// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#ifndef PINK_INCLUDE_PINK_THREAD_H_
#define PINK_INCLUDE_PINK_THREAD_H_

#include <pthread.h>
#include <string>
#include <atomic>

#include "slash/include/slash_mutex.h"

namespace pink {

class Thread {
 public:
  Thread();
  virtual ~Thread();

  virtual int StartThread();
  virtual int StopThread();
  int JoinThread();

  bool should_stop() {
    return should_stop_.load();
  }

  void set_should_stop() {
    should_stop_.store(true);
  }

  bool is_running() {
    return running_;
  }

  pthread_t thread_id() const {
    return thread_id_;
  }

  std::string thread_name() const {
    return thread_name_;
  }

  void set_thread_name(const std::string& name) {
    thread_name_ = name;
  }

 protected:
  static void* RunThread(void* arg);
  virtual void *ThreadMain() = 0;

  slash::Mutex running_mu_;
  std::atomic<bool> should_stop_;
  bool running_;
  pthread_t thread_id_;
  std::string thread_name_;

  /*
   * No allowed copy and copy assign
   */
  Thread(const Thread&);
  void operator=(const Thread&);
};

}  // namespace pink
#endif  // PINK_INCLUDE_PINK_THREAD_H_
