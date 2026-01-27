#include "IOContextPool.hpp"
#include "../util/Logger.hpp"

namespace chat {

IOContextPool::IOContextPool(size_t pool_size) {
    if (pool_size == 0) {
        pool_size = std::thread::hardware_concurrency();
        if (pool_size == 0) {
            pool_size = 4;  // Default fallback
        }
    }

    // Create io_contexts and work guards
    for (size_t i = 0; i < pool_size; ++i) {
        auto io_context = std::make_shared<boost::asio::io_context>();
        io_contexts_.push_back(io_context);
        work_guards_.emplace_back(boost::asio::make_work_guard(*io_context));
    }

    LOG_INFO("IOContextPool created with {} threads", pool_size);
}

IOContextPool::~IOContextPool() {
    stop();
}

void IOContextPool::run() {
    if (running_.exchange(true)) {
        return;  // Already running
    }

    // Create threads for each io_context
    for (size_t i = 0; i < io_contexts_.size(); ++i) {
        threads_.emplace_back([this, i]() {
            try {
                LOG_DEBUG("IO thread {} started", i);
                io_contexts_[i]->run();
                LOG_DEBUG("IO thread {} stopped", i);
            } catch (const std::exception& e) {
                LOG_ERROR("IO thread {} exception: {}", i, e.what());
            }
        });
    }

    LOG_INFO("IOContextPool started with {} threads", threads_.size());
}

void IOContextPool::stop() {
    if (!running_.exchange(false)) {
        return;  // Already stopped
    }

    LOG_INFO("Stopping IOContextPool...");

    // Release work guards to allow io_context::run() to return
    work_guards_.clear();

    // Stop all io_contexts
    for (auto& io_context : io_contexts_) {
        io_context->stop();
    }

    // Join all threads
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    threads_.clear();

    LOG_INFO("IOContextPool stopped");
}

boost::asio::io_context& IOContextPool::getIOContext() {
    // Round-robin distribution
    size_t index = next_io_context_.fetch_add(1) % io_contexts_.size();
    return *io_contexts_[index];
}

boost::asio::io_context& IOContextPool::getMainIOContext() {
    // Return the first io_context for main operations
    return *io_contexts_[0];
}

} // namespace chat
