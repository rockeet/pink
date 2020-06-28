#ifndef _DELAY_QUEUE_H_
#define _DELAY_QUEUE_H_

#include <condition_variable>
#include <mutex>
#include <thread>
#include <map>
#include <sys/time.h>

using namespace std;

namespace pink {

template<typename T>
class DelayQueue {
 public:
  explicit DelayQueue() : closed_(false) {
  }
  virtual ~DelayQueue() {}

  size_t GetSize() {
    std::lock_guard<std::mutex> lk(mut_);
    return queue_.size();
  }

  int Put(uint64_t time, T elem); 
  int Take(T* pElem); 
  void Close();
  void Shutdown();

 private:
  std::map<uint64_t, T> queue_;
  bool closed_;
  std::mutex mut_;
  std::condition_variable r_cv_;
};

template<typename T>
int DelayQueue<T>::Put(uint64_t timeout, T elem) {
  std::unique_lock<std::mutex> ulk(mut_);
  if (closed_) {
    return 0;
  }

  struct timeval now;
  gettimeofday(&now, NULL);
  uint64_t exec_time = now.tv_sec * 1000000 + now.tv_usec + timeout * 1000;
  queue_[exec_time] = elem;
  r_cv_.notify_one();
  return 1;
}

template<typename T>
int DelayQueue<T>::Take(T* pElem) {
  if (!pElem) {
    return -1;
  }

  std::unique_lock<std::mutex> ulk(mut_);

  while (true) {
    while (queue_.empty()) {
      if (closed_) {
        return 0;
      }
      r_cv_.wait(ulk);
    }

    uint64_t exec_time = queue_.begin()->first;
    struct timeval now;
    gettimeofday(&now, NULL);
    uint64_t unow = now.tv_sec * 1000000 + now.tv_usec;
    if (unow / 1000 >= exec_time / 1000) {
      break;
    }
    r_cv_.wait_for(ulk, std::chrono::milliseconds((exec_time - unow) / 1000));
  }

  *pElem = queue_.begin()->second;
  queue_.erase(queue_.begin());

  return 1;
}

template<typename T>
void DelayQueue<T>::Close() {
  {
    std::unique_lock<std::mutex> ulk(mut_);
    closed_ = true;
  }
  r_cv_.notify_all();
}

} // namespace pink
#endif // _DELAY_QUEUE_H_
