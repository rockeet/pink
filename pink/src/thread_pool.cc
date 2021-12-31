// Copyright (c) 2018-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include "pink/include/thread_pool.h"
#include "pink/src/pink_thread_name.h"
#include <terark/stdtypes.hpp>
#include <terark/circular_queue.hpp>
#include <sys/time.h>
#include <boost/noncopyable.hpp>
#include <util/core_local.h> // rocksdb

namespace pink {

constexpr size_t QUEUE_CAP = 63;

struct TaskQueue {
  terark::circular_queue<Task, true> queue_;
  std::priority_queue<TimeTask> time_queue_;
  slash::Mutex mu_;
  slash::CondVar rsignal_;
  slash::CondVar wsignal_;

  TaskQueue() : rsignal_(&mu_), wsignal_(&mu_) {
    // ignore user specified queue cap
    //queue_.queue().init(64); // cap=64 is enough
    queue_.init(QUEUE_CAP + 1);
  }
  void Schedule(ThreadPool* tp, TaskFunc func, void* arg);
  void DelaySchedule(ThreadPool* tp, uint64_t timeout, TaskFunc func, void* arg);
  void RunOnce(ThreadPool* tp);
};
template<class T>
class FakeCoreLocal {
  T m_val;
public:
  size_t NumCores() const { return 1; }
  T* Access() { return &m_val; }
  T* AccessAtCore(size_t) { return &m_val; }
};
#if 1
#define MyCoreLocal FakeCoreLocal
#else
// CoreLocalArray has some bug producing UB
#define MyCoreLocal rocksdb::CoreLocalArray
#endif
struct CoreLocalTaskQueue : MyCoreLocal<TaskQueue> {
  void Stop() {
    for (size_t i = 0, n = this->NumCores(); i < n; ++i) {
      TaskQueue* tq = this->AccessAtCore(i);
      tq->rsignal_.SignalAll();
      tq->wsignal_.SignalAll();
    }
  }
};
static CoreLocalTaskQueue g_tq;

class ThreadPool::Worker : boost::noncopyable {
  public:
    explicit Worker(ThreadPool* tp)
       : start_(false), thread_pool_(tp)
    {
    }
    static void* WorkerMain(void* arg);

    void WorkerRun();

    int start();
    int stop();

    std::atomic<bool> start_;
    ThreadPool* const thread_pool_;
    pthread_t thread_id_;
    std::string worker_name_;
};

void* ThreadPool::Worker::WorkerMain(void* arg) {
  static_cast<Worker*>(arg)->WorkerRun();
  return nullptr;
}

int ThreadPool::Worker::start() {
  if (!start_.load()) {
    if (pthread_create(&thread_id_, NULL, &WorkerMain, this)) {
      return -1;
    } else {
      start_.store(true);
      SetThreadName(thread_id_, thread_pool_->thread_pool_name() + "Worker");
    }
  }
  return 0;
}

int ThreadPool::Worker::stop() {
  if (start_.load()) {
    if (pthread_join(thread_id_, nullptr)) {
      return -1;
    } else {
      start_.store(false);
    }
  }
  return 0;
}

ThreadPool::ThreadPool(size_t worker_num,
                       size_t /*max_queue_size*/,
                       const std::string& thread_pool_name) :
  worker_num_(worker_num),
  thread_pool_name_(thread_pool_name),
  running_(false),
  should_stop_(false)
  {}

ThreadPool::~ThreadPool() {
  stop_thread_pool();
}

int ThreadPool::start_thread_pool() {
  if (!running_.load()) {
    should_stop_.store(false);
    for (size_t i = 0; i < worker_num_; ++i) {
      workers_.push_back(new Worker(this));
      int res = workers_[i]->start();
      if (res != 0) {
        return kCreateThreadError;
      }
    }
    running_.store(true);
  }
  return kSuccess;
}

int ThreadPool::stop_thread_pool() {
  int res = 0;
  if (running_.load()) {
    should_stop_.store(true);
    g_tq.Stop();
    for (const auto worker : workers_) {
      res = worker->stop();
      if (res != 0) {
        break;
      } else {
        delete worker;
      }
    }
    workers_.clear();
    running_.store(false);
  }
  return res;
}

bool ThreadPool::should_stop() {
  return should_stop_.load(std::memory_order_relaxed);
}

void ThreadPool::set_should_stop() {
  should_stop_.store(true, std::memory_order_relaxed);
}

void ThreadPool::Schedule(TaskFunc func, void* arg) {
  auto tsc = __builtin_ia32_rdtsc();
  auto idx = tsc % uint16_t(g_tq.NumCores());
  TaskQueue* tq = g_tq.AccessAtCore(idx);
  tq->Schedule(this, func, arg);
}
void TaskQueue::Schedule(ThreadPool* tp, TaskFunc func, void* arg) {
  mu_.Lock();
  while (queue_.size() >= QUEUE_CAP && !tp->should_stop()) {
    wsignal_.Wait();
  }
  if (!tp->should_stop()) {
    queue_.push_back(Task(func, arg));
    rsignal_.Signal();
  }
  mu_.Unlock();
}

/*
 * timeout is in millisecond
 */
void ThreadPool::DelaySchedule(
    uint64_t timeout, TaskFunc func, void* arg) {
  auto tsc = __builtin_ia32_rdtsc();
  auto idx = tsc % uint16_t(g_tq.NumCores());
  TaskQueue* tq = g_tq.AccessAtCore(idx);
  tq->DelaySchedule(this, timeout, func, arg);
}

void TaskQueue::DelaySchedule(
    ThreadPool* tp, uint64_t timeout, TaskFunc func, void* arg) {
  /*
   * pthread_cond_timedwait api use absolute API
   * so we need gettimeofday + timeout
   */
  struct timeval now;
  gettimeofday(&now, NULL);
  uint64_t exec_time = now.tv_sec * 1000000 + timeout * 1000 + now.tv_usec;

  mu_.Lock();
  if (!tp->should_stop()) {
    time_queue_.push(TimeTask(exec_time, func, arg));
    rsignal_.Signal();
  }
  mu_.Unlock();
}

void ThreadPool::cur_queue_size(size_t* qsize) {
  size_t n = 0;
  for (size_t i = 0; i < g_tq.NumCores(); ++i) {
    TaskQueue* w = g_tq.AccessAtCore(i);
    slash::MutexLock l(&w->mu_);
    n += w->queue_.size();
  }
  *qsize = n;
}

void ThreadPool::cur_time_queue_size(size_t* qsize) {
  size_t n = 0;
  for (size_t i = 0; i < g_tq.NumCores(); ++i) {
    TaskQueue* w = g_tq.AccessAtCore(i);
    n += w->time_queue_.size();
  }
  *qsize = n;
}

std::string ThreadPool::thread_pool_name() {
  return thread_pool_name_;
}

void ThreadPool::Worker::WorkerRun() {
  ThreadPool* tp = thread_pool_;
  while (!tp->should_stop()) {
    TaskQueue* tq = g_tq.Access();
    tq->RunOnce(tp);
  }
}

void TaskQueue::RunOnce(ThreadPool* tp) {
  mu_.Lock();
  while (queue_.empty() && time_queue_.empty() && !tp->should_stop()) {
    rsignal_.Wait();
  }
  if (tp->should_stop()) {
    mu_.Unlock();
    return;
  }
  if (!time_queue_.empty()) {
    struct timeval now;
    gettimeofday(&now, NULL);

    TimeTask time_task = time_queue_.top();
    uint64_t unow = now.tv_sec * 1000000 + now.tv_usec;
    if (unow / 1000 >= time_task.exec_time / 1000) {
      TaskFunc func = time_task.func;
      void* arg = time_task.arg;
      time_queue_.pop();
      mu_.Unlock();
      (*func)(arg);
      return;
    } else if (queue_.empty() && !tp->should_stop()) {
      rsignal_.TimedWait(
          static_cast<uint32_t>((time_task.exec_time - unow) / 1000));
      mu_.Unlock();
      return;
    }
  }
  if (!queue_.empty()) {
    TaskFunc func = queue_.front().func;
    void* arg = queue_.front().arg;
    queue_.pop_front();
    wsignal_.Signal();
    mu_.Unlock();
    (*func)(arg);
  }
}
}  // namespace pink
