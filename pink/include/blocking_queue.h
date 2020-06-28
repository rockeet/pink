#ifndef _BLOCKING_QUEUE_H_
#define _BLOCKING_QUEUE_H_

#include <condition_variable>
#include <mutex>
#include <thread>
#include <queue>

using namespace std;

namespace pink {

template<typename T>
class BlockingQueue {
 public:
  explicit BlockingQueue(size_t capacity) : capacity_(capacity), closed_(false) {
  }
  virtual ~BlockingQueue() {}

  size_t GetCapacity() {
    return capacity_;
  }
  size_t GetSize() {
    std::lock_guard<std::mutex> lk(mut_);
    return queue_.size();
  }

  int Put(T elem); 
  int Take(T* pElem); 
  void Close();

 private:
  int capacity_;
  std::queue<T> queue_;
  bool closed_;
  std::mutex mut_;
  std::condition_variable r_cv_;
  std::condition_variable w_cv_;
};

template<typename T>
int BlockingQueue<T>::Put(T elem) {
  std::unique_lock<std::mutex> ulk(mut_);
  while (queue_.size() >= capacity_) {
    if (closed_) {
      return 0;
    }
    w_cv_.wait(ulk);
  }
  if (closed_) {
    return 0;
  }
  queue_.push(elem);
  r_cv_.notify_one();
  return 1;
}

template<typename T>
int BlockingQueue<T>::Take(T* pElem) {
  if (!pElem) {
    return -1;
  }
  std::unique_lock<std::mutex> ulk(mut_);
  while (queue_.empty()) {
    if (closed_) {
      return 0;
    }
    r_cv_.wait(ulk);
  }
  *pElem = queue_.front();
  queue_.pop();
  if (!closed_) {
    w_cv_.notify_one();
  }
  return 1;
}

template<typename T>
void BlockingQueue<T>::Close() {
  {
    std::unique_lock<std::mutex> ulk(mut_);
    closed_ = true;
  }
  w_cv_.notify_all();
  r_cv_.notify_all();
}

} // namespace pink
#endif // _BLOCKING_QUEUE_H_
