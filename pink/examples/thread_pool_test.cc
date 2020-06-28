// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>

#include <string>
#include <iostream>
#include <mutex>

#include "pink/include/thread_pool.h"

using namespace std;

uint64_t NowMicros() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return static_cast<uint64_t>(tv.tv_sec) * 1000000 + tv.tv_usec;
}

static std::mutex print_mut;

void task(void *arg) {
  {
  std::lock_guard<std::mutex> lk(print_mut);
  std::cout << pthread_self() << " doing task: " << *((int *)arg) << " time(micros): " << NowMicros() << std::endl;
  }
  usleep(20);
  delete (int*)arg;
}

int main() {
  size_t qsize = 0;

  pink::TaskExecutor task_executor(10, 100);
  std::cout << "Testing ThreadPool Executor ... " << std::endl;
  task_executor.Start();
  for (int i = 0; i < 1000; i++) {
    int *pi = new int(i);
    task_executor.Schedule(task, (void*)pi);
    std::lock_guard<std::mutex> lk(print_mut);
    std::cout << pthread_self() << " putting task: " << i << std::endl;
  }
  task_executor.Stop();
  while (qsize > 0) {
    qsize = task_executor.GetQueueSize();
    std::cout << "waiting, current queue size:" << qsize << std::endl;
    sleep(1);
  }
  std::cout << std::endl << std::endl << std::endl;

  qsize = 0;
  pink::DelayTaskExecutor delay_task_executor(10);
  std::cout << "Testing ThreadPool Executor ... " << std::endl;
  delay_task_executor.Start();
  for (int i = 0; i < 100; i++) {
    int *pi = new int(i);
    delay_task_executor.DelaySchedule(i * 100, task, (void*)pi);
    std::lock_guard<std::mutex> lk(print_mut);
    std::cout << pthread_self() << " putting delay task: " << i << " time(micros): " << NowMicros() << std::endl;
  }
  delay_task_executor.Stop();
  while (qsize > 0) {
    qsize = delay_task_executor.GetQueueSize();
    std::cout << "waiting, current queue size: " << qsize << std::endl;
    sleep(1);
  }
  std::cout << std::endl << std::endl;

  return 0;
}
