#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <chrono> //改用新版时间库
#include <mutex>
#include <condition_variable>
#include <semaphore> // c++20
#include <vector>
//#include "../lock/locker.h" // 不使用手动封装的类型，使用现代c++写法
//using namespace std;

template <class T>
class block_queue
{
public:
  block_queue(int max_size = 1000) : m_array(max_size) { // 这里使用初始化列表来为vector分配初始空间
    if (max_size <= 0){
      exit(-1);
    }

    m_size = 0;
    m_max_size = max_size;
    //m_array = new T[m_max_size]; // 原实现
    m_back = 0; // 原实现的首尾下标都是-1，back是当前的最后一个，front是第一个的前一个（左开右闭），我将其改为左闭右开更符合我的习惯
    m_front = 0;
    m_is_closed = false;
  }

  void close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_is_closed = true;
    m_cond.notify_all(); // 唤醒所有阻塞的消费者，准备退出
  }

  void clear (){
    std::lock_guard<std::mutex> lock(m_mutex);
    m_size = 0;
    m_back = 0;
    m_front = 0;
    m_array.clear();
  }

  // 由于改为vector实现，我认为应该不用特意写析构函数
  // ~block_queue(){
  //   std::lock_guard<std::mutex> lock(m_mutex);
  //   if (m_array != NULL)
  //       delete [] m_array;
  // }

  bool full (){
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_size >= m_max_size;
  }

  bool empty (){
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_size <= 0;
  }

  bool front (T &value){
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_size <= 0) return false;
    value = m_array[m_front];
    return true;
  }

  bool back (T &value){
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_size <= 0) return false;
    value = m_array[m_back];
    return true;
  }

  int size (){
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_size;
  }

  int max_size (){
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_max_size;
  }

  bool push (T &item){
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_size >= m_max_size){
      // m_cond.notify_all(); // 其实不需要
      return false;
    }
    m_array[m_back] = item;
    m_back = (m_back + 1) % m_max_size;
    m_size++;
    m_cond.notify_one();
    return true;
  }

  bool pop (T &item){
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cond.wait(lock, [this]() { // 重要！现代的条件变量写法，使用lambda表达式
      return m_size > 0 || m_is_closed;
    });
    if (m_size == 0 && m_is_closed) {
      return false;
    }
    item = m_array[m_front];
    m_front = (m_front + 1) % m_max_size;
    m_size--;
    return true;
  }

  bool pop (T &item, int ms_timeout){ // 超时处理
    std::unique_lock<std::mutex> lock(m_mutex);
    bool success = m_cond.wait_for(lock, std::chrono::milliseconds(ms_timeout), [this]() {
        return m_size > 0 || m_is_closed;
    });
    if (!success || (m_size == 0 && m_is_closed)) return false;

    item = m_array[m_front];
    m_front = (m_front + 1) % m_max_size;
    m_size--;
    return true;
  }

private:
  std::mutex m_mutex;
  std::condition_variable m_cond;
  std::vector<T> m_array; // 将 T *m_array 改为vector实现

  int m_size;
  int m_max_size;
  int m_front;
  int m_back;
  bool m_is_closed;
};

#endif