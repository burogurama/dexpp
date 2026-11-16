#pragma once

#include <atomic>
#include <cstddef>
#include <exception>
#include <thread>
#include <utility>
#include <vector>

namespace dex::detail {

/** A group of threads that are all joined when the group is destroyed, like a
 *  std::vector<std::jthread>.
 *
 *  std::jthread itself is not used: the libc++ of Apple's Xcode 16 still
 *  treats it as experimental and leaves it out, so code using it does not
 *  build on macOS.  A private header: not part of the public API. */
class JoiningThreads
{
  public:
    JoiningThreads() = default;
    JoiningThreads(const JoiningThreads &) = delete;
    JoiningThreads &operator=(const JoiningThreads &) = delete;

    ~JoiningThreads()
    {
        for (std::thread &thread : threads_)
            if (thread.joinable())
                thread.join();
    }

    void reserve(std::size_t count) { threads_.reserve(count); }

    /** Start a thread that runs @p fn.  Throws std::system_error when the
     *  system cannot start one; the threads already started are kept. */
    template <typename Fn> void start(Fn &&fn) { threads_.emplace_back(std::forward<Fn>(fn)); }

  private:
    std::vector<std::thread> threads_;
};

/** Run fn(i) for every i in @p order on up to @p threads threads, the calling
 *  thread included.  An exception thrown by fn is kept and rethrown here once
 *  every thread has stopped — escaping a std::thread, it would std::terminate
 *  the process; when several throw, the one for the lowest i wins.  Every i
 *  must be smaller than order.size(). */
template <typename Fn>
void for_each_parallel(const std::vector<std::size_t> &order, std::size_t threads, Fn &&fn)
{
    std::vector<std::exception_ptr> errors(order.size());
    std::atomic<std::size_t> next{0};
    auto work = [&] {
        for (std::size_t k = next.fetch_add(1); k < order.size(); k = next.fetch_add(1)) {
            try {
                fn(order[k]);
            }
            catch (...) {
                errors[order[k]] = std::current_exception();
            }
        }
    };
    {
        JoiningThreads pool;
        try {
            for (std::size_t t = 1; t < threads; ++t)
                pool.start(work);
        }
        catch (const std::exception &) {
            // Could not start every thread (resource limits): the threads
            // that did start, plus this one, still drain the queue.
        }
        work();
    } // the pool joins here
    for (const auto &error : errors)
        if (error)
            std::rethrow_exception(error);
}

} // namespace dex::detail
