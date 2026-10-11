// Concurrent-access stress test.  The AnalysisContext implementation is built
// around lazy, mutex-guarded per-DEX caches (strings, class data, code items,
// instructions, CFGs) and std::call_once whole-program singletons (class
// index, call graph, class hierarchy, xrefs).  Nothing in the API states that
// concurrent *reads* of one context are safe, and no test pinned it — so this
// exercises exactly that: many threads racing to trigger the first-access lazy
// initialisation of the same context, then reading through the handles.
//
// The assertions (every thread agrees on what it observed) catch torn reads;
// the real oracle is ThreadSanitizer, under which a missing or wrong lock
// surfaces as a reported data race.  Build with -DDEXPP_SANITIZE=thread.

#include <gtest/gtest.h>

#include <cstddef>
#include <latch>
#include <optional>
#include <thread>
#include <vector>

#include "dex.hpp"

using namespace dex;

namespace {

// One thread's view of the context, computed by walking every lazy surface.
struct Observed
{
    std::size_t classes = 0;
    std::size_t strings = 0;
    std::size_t cg_nodes = 0;
    std::size_t xref_strings = 0;
    std::size_t instructions = 0;
    std::size_t cfg_blocks = 0;
    bool hierarchy_nonempty = false;

    bool operator==(const Observed &) const = default;
};

Observed walk(const AnalysisContext &ctx)
{
    Observed o;
    // Whole-program singletons (call_once): all racing threads hit these
    // uninitialised on the first iteration.
    o.strings = ctx.strings().size();
    o.cg_nodes = ctx.call_graph().nodes().size();
    o.xref_strings = ctx.xrefs().referenced_strings().size();
    o.hierarchy_nonempty = !ctx.class_hierarchy().empty();

    // Per-DEX lazy caches, reached through handles: instruction and CFG caches
    // (same map, different keys, concurrently) plus find_class's class index.
    for (const auto &c : ctx.classes()) {
        ++o.classes;
        (void)ctx.find_class(c.name());
        for (const auto &m : c.methods()) {
            o.instructions += m.instructions().size();
            o.cfg_blocks += m.cfg().blocks().size();
            (void)m.calls();
        }
    }
    return o;
}

} // namespace

TEST(ThreadedStress, ConcurrentLazyInitAndReads)
{
    constexpr int kThreads = 8;
    constexpr int kIterations = 40;

    for (int iter = 0; iter < kIterations; ++iter) {
        // A fresh context each iteration so the first-access races (lazy cache
        // fills, call_once) actually run every time, not just once overall.
        auto ctx = AnalysisContext::from_dex_files(
            {"tests/data/classes.dex", "tests/data/fibonacci.dex", "tests/data/polymorphic.dex"});
        ASSERT_TRUE(ctx.has_value()) << ctx.error().message;

        std::vector<Observed> seen(kThreads);
        {
            std::latch start(kThreads);
            std::vector<std::jthread> threads;
            threads.reserve(kThreads);
            for (int t = 0; t < kThreads; ++t) {
                threads.emplace_back([&, t] {
                    start.arrive_and_wait(); // release all threads together
                    seen[t] = walk(*ctx);    // each writes its own slot
                });
            }
        } // jthreads join here

        for (int t = 1; t < kThreads; ++t)
            EXPECT_EQ(seen[t], seen[0]) << "thread " << t << " disagrees on iteration " << iter
                                        << " (torn read or lost update under concurrency)";

        // Sanity: the walk actually observed a non-trivial program.
        EXPECT_GT(seen[0].cg_nodes, 0u);
        EXPECT_GT(seen[0].instructions, 0u);
        EXPECT_TRUE(seen[0].hierarchy_nonempty);
    }
}

TEST(ThreadedStress, SameMethodCachesAreStableUnderContention)
{
    // Many threads request instructions() and cfg() for the SAME method at
    // once.  The caches must hand back one stable span / reference regardless
    // of which thread wins the first decode.
    auto ctx = AnalysisContext::from_dex("tests/data/fibonacci.dex");
    ASSERT_TRUE(ctx.has_value()) << ctx.error().message;

    auto cls = ctx->find_class("LFibonacci;");
    ASSERT_TRUE(cls.has_value());
    std::optional<Method> target;
    for (const auto &m : cls->methods())
        if (m.name() == "compute")
            target = m;
    ASSERT_TRUE(target.has_value());

    constexpr int kThreads = 16;
    std::vector<const Instruction *> insn_ptr(kThreads);
    std::vector<const Cfg *> cfg_ptr(kThreads);
    {
        std::latch start(kThreads);
        std::vector<std::jthread> threads;
        threads.reserve(kThreads);
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&, t] {
                start.arrive_and_wait();
                insn_ptr[t] = target->instructions().data();
                cfg_ptr[t] = &target->cfg();
            });
        }
    }

    for (int t = 1; t < kThreads; ++t) {
        EXPECT_EQ(insn_ptr[t], insn_ptr[0]) << "instructions() span not stable across threads";
        EXPECT_EQ(cfg_ptr[t], cfg_ptr[0]) << "cfg() reference not stable across threads";
    }
}
