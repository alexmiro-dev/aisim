// infrastructure:: concurrency primitives — the threading mechanism the core is
// kept unaware of. Internal partition of aisim.infrastructure; used by the
// Executor, EventBus, and Logger. Never visible to domain/application.

module;

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>

export module aisim.infrastructure:concurrency;

namespace aisim::infrastructure {

/// @brief Satisfied when @p N is a non-zero power of two.
/// @tparam N Value to test.
template <std::size_t N>
concept PowerOfTwo = ((N > 0) && (N & (N - 1)) == 0);

/// @brief Lock-free, bounded MPMC queue (Vyukov bounded-queue algorithm).
///
/// Multiple producers and multiple consumers may call push() and pop()
/// concurrently without locks. Each slot carries a sequence counter that
/// gates access, so progress relies only on atomic CAS — no thread can block
/// another indefinitely. Both operations are non-blocking and wait-free for
/// the uncontended fast path.
///
/// @tparam T        Element type. Must be movable; default-constructed once
///                  per slot at construction time.
/// @tparam Capacity Number of slots. Must be a power of two so the index can
///                  be computed with a bit-mask rather than a modulo.
template <typename T, std::size_t Capacity>
    requires PowerOfTwo<Capacity>
class CircularQueue {
public:
    /// @brief Failure reasons returned through std::expected.
    enum class Error : int {
        QueueIsEmpty,  ///< pop() found no element ready to consume.
        QueueIsFull,   ///< push() found no slot ready to produce into.
    };

    /// @brief Constructs an empty queue, seeding each slot's sequence number
    ///        with its index so the first lap of pushes/pops lines up.
    CircularQueue() {
        for (std::size_t i = 0; i < Capacity; ++i) {
            slots_[i].seq.store(i, std::memory_order_relaxed);
        }
    }

    /// @brief Enqueues an element (non-blocking, multi-producer safe).
    /// @param value Element to move into the queue.
    /// @return Empty on success; Error::QueueIsFull if no slot is available.
    [[nodiscard]] std::expected<void, Error> push(T&& value) noexcept {
        auto pos = tail_.load(std::memory_order_relaxed);

        for (;;) {
            Slot& slot = slots_[pos & (Capacity - 1)];
            const auto seq = slot.seq.load(std::memory_order::acquire);
            const auto diff = static_cast<std::intptr_t>(seq - pos);

            if (diff == 0) {
                if (tail_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    slot.data = std::move(value);
                    slot.seq.store(pos + 1, std::memory_order_release);
                    return {};
                }
            } else if (diff < 0) {
                return std::unexpected(Error::QueueIsFull);
            } else {
                // Another producer claimed this slot first; reload and retry.
                pos = tail_.load(std::memory_order_relaxed);
            }
        }
    }

    /// @brief Dequeues an element (non-blocking, multi-consumer safe).
    /// @return The moved-out element on success; Error::QueueIsEmpty if no
    ///         element is ready to consume.
    [[nodiscard]] std::expected<T, Error> pop() noexcept {
        auto pos = head_.load(std::memory_order_relaxed);

        for (;;) {
            Slot& slot = slots_[pos & (Capacity - 1)];
            const auto seq = slot.seq.load(std::memory_order_acquire);
            const auto diff = static_cast<std::intptr_t>(seq - (pos + 1));

            if (diff == 0) {
                if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    T value = std::move(slot.data);
                    slot.seq.store(pos + Capacity, std::memory_order_release);
                    return value;
                }
            } else if (diff < 0) {
                return std::unexpected(Error::QueueIsEmpty);
            } else {
                // Another consumer claimed this slot first; reload and retry.
                pos = head_.load(std::memory_order_relaxed);
            }
        }
    }


private:
    /// @brief Assumed cache-line size; slots and counters are aligned to it to
    ///        avoid false sharing between concurrent producers and consumers.
    static constexpr std::size_t CACHE_LINE = 64;

    /// @brief One queue cell: its sequence counter plus the payload.
    ///
    /// @c seq encodes whose turn it is. A producer may write when
    /// @c seq == ticket; a consumer may read when @c seq == ticket + 1.
    /// Cache-line alignment keeps adjacent slots off the same line.
    struct alignas(CACHE_LINE) Slot {
        std::atomic<uint64_t> seq;  ///< Turn counter gating producer/consumer access.
        T data;                     ///< Stored element; valid only between matching push/pop.
    };

    std::array<Slot, Capacity> slots_;  ///< Ring storage; index masked by Capacity-1.

    /// @brief Next index to pop from; on its own cache line to avoid sharing
    ///        with @c tail_.
    alignas(CACHE_LINE) std::atomic<uint64_t> head_{0};
    /// @brief Next index to push to; on its own cache line to avoid sharing
    ///        with @c head_.
    alignas(CACHE_LINE) std::atomic<uint64_t> tail_{0};
};

}  // namespace aisim::infrastructure
