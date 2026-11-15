#pragma once

#include <cstddef>
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

} // namespace dex::detail
