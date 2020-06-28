// Copyright (c) 2018-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#ifndef _THREAD_POOL_H_
#define _THREAD_POOL_H_

#include <atomic>
#include <mutex>
#include <thread>

#include "pink/include/blocking_queue.h"
#include "pink/include/delay_queue.h"

namespace pink {

class ThreadPool {
 public:
  explicit ThreadPool(size_t tp_size)
      : tp_size_(tp_size) {}
  virtual ~ThreadPool() {
    Stop();
  }

  bool Start();
  void Stop();

 protected:
  virtual bool Init() { return true; }
  virtual void Destroy() {}
  virtual void Run() {}

 private:
  void runInThread() {
    Run();
  }

 private:
  std::mutex mut_;
  size_t tp_size_;
  std::vector<std::thread> threads_;
};

typedef void (*TaskFunc)(void*);

struct Task {
  TaskFunc func;
  void* arg;
  Task() : func(nullptr), arg(nullptr) {}
  Task(TaskFunc _func, void* _arg)
      : func(_func), arg(_arg) {}
};

class TaskExecutor : public ThreadPool {
 public:
  TaskExecutor(size_t tp_size, size_t q_capacity, bool grace = true)
      : ThreadPool(tp_size), running_(true), grace_(grace), bq_(q_capacity) {}
  virtual ~TaskExecutor() {}

  int Schedule(TaskFunc func, void* arg) {
    return bq_.Put(Task(func, arg));
  }
  size_t GetQueueSize() {
    return bq_.GetSize();
  }

 private:
  void Destroy() override;
  void Run() override;

 private:
  std::atomic<bool> running_;
  bool grace_;
  BlockingQueue<Task> bq_;
};

class DelayTaskExecutor : public ThreadPool {
 public:
  DelayTaskExecutor(size_t tp_size, bool grace = true)
      : ThreadPool(tp_size), running_(true), grace_(grace) {}
  virtual ~DelayTaskExecutor() {}

  int DelaySchedule(uint64_t timeout, TaskFunc func, void* arg) {
    return dq_.Put(timeout, Task(func, arg));
  }
  size_t GetQueueSize() {
    return dq_.GetSize();
  }

 private:
  void Destroy() override;
  void Run() override;

 private:
  std::atomic<bool> running_;
  bool grace_;
  DelayQueue<Task> dq_;
};

} // namespace pink
#endif // _THREAD_POOL_H_
