#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <vector>
#include <assert.h>
#include "../log/log.h"

class ThreadPool {
public:
    // 构造函数：预先拉起指定数量的工作线程
    explicit ThreadPool(size_t threadCount = 8) : isClosed_(false) {
        assert(threadCount > 0);
        
        // 预分配内存，避免 vector 动态扩容开销
        threads_.reserve(threadCount); 
        
        for (size_t i = 0; i < threadCount; ++i) {
            // 使用 Lambda 表达式直接定义工作线程的“死循环逻辑”
            threads_.emplace_back([this]() {
                while (true) {
                    std::function<void()> task; // 用来接收从队列里拿出来的任务
                    
                    {
                        // 1. 加锁：准备去任务队列里拿任务
                        std::unique_lock<std::mutex> lock(mtx_);
                        
                        // 2. 阻塞等待：解决“虚假唤醒”的杀手锏
                        // 线程会在这里沉睡，直到满足两个条件之一才醒来：
                        // (1) 线程池要关闭了 (isClosed_ == true)
                        // (2) 任务队列里有任务了 (!tasks_.empty())
                        cond_.wait(lock, [this]() {
                            return isClosed_ || !tasks_.empty();
                        });

                        // 3. 退出机制：如果池子关了，且任务做完了，线程就可以安息了
                        if (isClosed_ && tasks_.empty()) {
                            return; 
                        }

                        // 4. 拿到任务，并将其移出队列 (std::move 避免拷贝开销)
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    } 
                    // ⚠️ 极其关键：离开上面的 {} 作用域，lock 会自动解锁！
                    LOG_INFO("[DEBUG] Thread pool: task running...");
                    // 5. 执行任务：必须在解锁后执行，否则多线程就退化成了串行运行！
                    task(); 
                }
            });
        }
    }

    // 析构函数：优雅停机 (Graceful Shutdown)
    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            isClosed_ = true; // 打上停机标记
        }
        cond_.notify_all(); // 唤醒所有还在沉睡的线程，让它们看到标记后自行了断

        // 阻塞主线程，等待所有工作线程把手头的活儿干完并安全退出
        for (auto& thread : threads_) {
            if (thread.joinable()) {
                thread.join(); 
            }
        }
    }

    // 核心投递接口：利用万能模板和完美转发，可以塞入任何形式的函数
    template<class F>
    void AddTask(F&& task) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            // 将任务完美转发进队列 (不需要复制，直接在队列内存里构造)
            tasks_.emplace(std::forward<F>(task)); 
        }
        cond_.notify_one(); // 敲钟！随机唤醒一个正在沉睡的工作线程来接客
    }

private:
    std::vector<std::thread> threads_;            // 线程数组
    std::queue<std::function<void()>> tasks_;     // 万能任务队列
    std::mutex mtx_;                              // 互斥锁 (保护任务队列)
    std::condition_variable cond_;                // 条件变量 (控制线程的沉睡与唤醒)
    bool isClosed_;                               // 线程池停机标志
};

#endif // THREADPOOL_H