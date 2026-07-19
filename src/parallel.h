#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// A tiny persistent thread pool for data-parallel loops (parallel-for).
//
// Workers are created once and reused, so per-frame parallel work (e.g. battle
// AI over hundreds of soldiers) pays no thread-creation cost. `For` splits the
// index range into grain-sized chunks that idle workers and the calling thread
// grab via an atomic cursor (simple work-stealing), then blocks until the whole
// range is done. Small ranges run inline with no synchronisation overhead.
//
// Usage:
//   ThreadPool::Global().For(0, n, 32, [&](int i){ compute(i); });
// The callback must be safe to run concurrently for distinct i (write only to
// per-index storage; treat everything else as read-only).
// ---------------------------------------------------------------------------
class ThreadPool {
public:
    static ThreadPool& Global() {
        static ThreadPool pool;
        return pool;
    }

    explicit ThreadPool(unsigned workers = 0) {
        if (workers == 0) {
            const unsigned hw = std::thread::hardware_concurrency();
            workers = (hw > 1) ? hw - 1 : 0;   // the calling thread also works
        }
        workerCount_ = workers;
        for (unsigned i = 0; i < workerCount_; ++i)
            threads_.emplace_back([this] { WorkerLoop(); });
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            stop_ = true;
            ++generation_;
        }
        wake_.notify_all();
        for (std::thread& t : threads_) t.join();
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Run fn(i) for i in [begin, end). Blocks until every index completes.
    void For(int begin, int end, int grain, const std::function<void(int)>& fn) {
        if (end <= begin) return;
        const int total = end - begin;
        if (workerCount_ == 0 || total <= grain) {   // not worth dispatching
            for (int i = begin; i < end; ++i) fn(i);
            return;
        }

        {
            std::lock_guard<std::mutex> lk(mutex_);
            fn_ = &fn;
            begin_ = begin;
            end_ = end;
            grain_ = grain;
            cursor_.store(begin, std::memory_order_relaxed);
            remaining_.store(static_cast<int>(workerCount_) + 1, std::memory_order_relaxed);
            ++generation_;
        }
        wake_.notify_all();

        RunChunks();   // the calling thread participates

        std::unique_lock<std::mutex> lk(doneMutex_);
        done_.wait(lk, [this] { return remaining_.load(std::memory_order_acquire) == 0; });
        fn_ = nullptr;
    }

private:
    void RunChunks() {
        for (;;) {
            const int i = cursor_.fetch_add(grain_, std::memory_order_relaxed);
            if (i >= end_) break;
            const int j = (i + grain_ < end_) ? i + grain_ : end_;
            for (int k = i; k < j; ++k) (*fn_)(k);
        }
        if (remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            std::lock_guard<std::mutex> lk(doneMutex_);
            done_.notify_one();
        }
    }

    void WorkerLoop() {
        unsigned lastGen = 0;
        for (;;) {
            {
                std::unique_lock<std::mutex> lk(mutex_);
                wake_.wait(lk, [this, lastGen] { return stop_ || generation_ != lastGen; });
                if (stop_) return;
                lastGen = generation_;
            }
            RunChunks();
        }
    }

    std::vector<std::thread> threads_;
    unsigned                 workerCount_ = 0;

    std::mutex              mutex_;
    std::condition_variable wake_;
    unsigned               generation_ = 0;
    bool                   stop_ = false;

    // Current batch (published under mutex_, then read lock-free by workers).
    const std::function<void(int)>* fn_ = nullptr;
    int              begin_ = 0, end_ = 0, grain_ = 1;
    std::atomic<int> cursor_{ 0 };
    std::atomic<int> remaining_{ 0 };

    std::mutex              doneMutex_;
    std::condition_variable done_;
};
