#pragma once

#include <boost/asio.hpp>
#include <vector>
#include <thread>
#include <memory>
#include <atomic>

namespace chat {

/**
 * IO Context Pool
 *
 * Manages a pool of io_context objects and worker threads
 * for handling asynchronous I/O operations.
 */
class IOContextPool {
public:
    /**
     * Construct the pool with specified number of threads
     *
     * @param pool_size Number of IO threads (0 = hardware concurrency)
     */
    explicit IOContextPool(size_t pool_size = 0);

    ~IOContextPool();

    // Non-copyable
    IOContextPool(const IOContextPool&) = delete;
    IOContextPool& operator=(const IOContextPool&) = delete;

    /**
     * Start the pool (run all io_contexts)
     */
    void run();

    /**
     * Stop the pool
     */
    void stop();

    /**
     * Get an io_context from the pool (round-robin)
     */
    boost::asio::io_context& getIOContext();

    /**
     * Get the main io_context (for acceptor, timers, etc.)
     */
    boost::asio::io_context& getMainIOContext();

    /**
     * Get the pool size
     */
    size_t size() const { return io_contexts_.size(); }

    /**
     * Check if pool is running
     */
    bool isRunning() const { return running_.load(); }

private:
    using IOContextPtr = std::shared_ptr<boost::asio::io_context>;
    using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

    std::vector<IOContextPtr> io_contexts_;
    std::vector<WorkGuard> work_guards_;
    std::vector<std::thread> threads_;

    std::atomic<size_t> next_io_context_{0};
    std::atomic<bool> running_{false};
};

} // namespace chat
