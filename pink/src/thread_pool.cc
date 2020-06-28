// Copyright (c) 2018-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include "pink/include/thread_pool.h"

namespace pink {

bool ThreadPool::Start() {
  std::lock_guard<std::mutex> lk(mut_);
  if (!Init()) {
    return false;
  }
  for (size_t i = 0; i < tp_size_; i++) {
    threads_.push_back(std::thread(&ThreadPool::runInThread, this));
  }
  return true;
}

void ThreadPool::Stop() {
  Destroy();
  std::lock_guard<std::mutex> lk(mut_);
  for (size_t i = 0; i < threads_.size(); i++) {
    threads_[i].join();
  }
  threads_.clear();
}

void TaskExecutor::Run() {
  while (running_.load() || grace_) {
    Task task;
    int ret = bq_.Take(&task);
    if (ret <= 0) {
      break;
    }
    if (task.func != nullptr) {
      (*task.func)(task.arg);
    }
  }
}

void TaskExecutor::Destroy() {
  running_.store(false);
  bq_.Close();
}

void DelayTaskExecutor::Run() {
  while (running_.load() || grace_) {
    Task task;
    int ret = dq_.Take(&task);
    if (ret <= 0) {
      break;
    }
    if (task.func != nullptr) {
      (*task.func)(task.arg);
    }
  }
}

void DelayTaskExecutor::Destroy() {
  running_.store(false);
  dq_.Close();
}

} // namespace pink
