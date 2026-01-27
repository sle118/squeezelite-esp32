#ifndef BELL_QUEUE_H
#define BELL_QUEUE_H

#include <atomic>
#include <condition_variable>
#include <queue>

namespace bell {
template <typename dataType>
class Queue {
 private:
  /// Queue
  std::queue<dataType> m_queue;
  /// Mutex to controll multiple access
  mutable std::mutex m_mutex;
  /// Conditional variable used to fire event
  std::condition_variable m_cv;
  /// Atomic variable used to terminate immediately wpop and wtpop functions
  std::atomic<bool> m_forceExit = false;

 public:
  /// <summary> Add a new element in the queue. </summary>
  /// <param name="data"> New element. </param>
  void push(dataType const& data) {
    m_forceExit.store(false);
    std::unique_lock<std::mutex> lk(m_mutex);
    m_queue.push(data);
    lk.unlock();
    m_cv.notify_one();
  }
  /// <summary> Check queue empty. </summary>
  /// <returns> True if the queue is empty. </returns>
  bool isEmpty() const {
    std::unique_lock<std::mutex> lk(m_mutex);
    return m_queue.empty();
  }
  /// <summary> Pop element from queue. </summary>
  /// <param name="popped_value"> [in,out] Element. </param>
  /// <returns> false if the queue is empty. </returns>
  bool pop(dataType& popped_value) {
    std::unique_lock<std::mutex> lk(m_mutex);
    if (m_queue.empty()) {
      return false;
    } else {
      popped_value = m_queue.front();
      m_queue.pop();
      return true;
    }
  }
  /// <summary> Wait and pop an element in the queue. </summary>
  /// <param name="popped_value"> [in,out] Element. </param>
  ///  <returns> False for forced exit. </returns>
  bool wpop(dataType& popped_value) {
    std::unique_lock<std::mutex> lk(m_mutex);
    m_cv.wait(lk,
              [&]() -> bool { return !m_queue.empty() || m_forceExit.load(); });
    if (m_forceExit.load())
      return false;
    popped_value = m_queue.front();
    m_queue.pop();
    return true;
  }
  /// <summary> Timed wait and pop an element in the queue. </summary>
  /// <param name="popped_value"> [in,out] Element. </param>
  /// <param name="milliseconds"> [in] Wait time. </param>
  ///  <returns> False for timeout or forced exit. </returns>
  bool wtpop(dataType& popped_value, long milliseconds = 1000) {
    std::unique_lock<std::mutex> lk(m_mutex);
    m_cv.wait_for(lk, std::chrono::milliseconds(milliseconds), [&]() -> bool {
      return !m_queue.empty() || m_forceExit.load();
    });
    if (m_forceExit.load())
      return false;
    if (m_queue.empty())
      return false;
    popped_value = m_queue.front();
    m_queue.pop();
    return true;
  }
  /// <summary> Queue size. </summary>
  int size() {
    std::unique_lock<std::mutex> lk(m_mutex);
    return static_cast<int>(m_queue.size());
  }
  /// <summary> Free the queue and force stop. </summary>
  void clear() {
    m_forceExit.store(true);
    std::unique_lock<std::mutex> lk(m_mutex);
    while (!m_queue.empty()) {
      //delete m_queue.front();
      m_queue.pop();
    }
    lk.unlock();
    m_cv.notify_one();
  }
  /// <summary> Check queue in forced exit state. </summary>
  bool isExit() const { return m_forceExit.load(); }
};
}  // namespace bell

#endif