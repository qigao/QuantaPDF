#ifndef QUANTAPDF_CORE_ATOMIC_REFCOUNT_H
#define QUANTAPDF_CORE_ATOMIC_REFCOUNT_H

#include <quantapdf/quantapdf.h>
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
#include <atomic>
using quantapdf_atomic_size = std::atomic<size_t>;
static_assert(sizeof(quantapdf_atomic_size) == sizeof(size_t),
    "std::atomic<size_t> must match size_t layout across C/C++ boundary");

static inline void quantapdf_refcount_init(
    quantapdf_atomic_size *counter, size_t initial) noexcept
{
    counter->store(initial, std::memory_order_relaxed);
}

static inline quantapdf_status quantapdf_refcount_retain(
    quantapdf_atomic_size *counter, quantapdf_status overflow_status) noexcept
{
    size_t current = counter->load(std::memory_order_relaxed);
    assert(current != 0u);
    while (current != SIZE_MAX) {
        if (counter->compare_exchange_weak(
                current, current + 1u,
                std::memory_order_relaxed, std::memory_order_relaxed))
            return QUANTAPDF_OK;
    }
    return overflow_status;
}

static inline int quantapdf_refcount_release(
    quantapdf_atomic_size *counter) noexcept
{
    size_t const previous = counter->fetch_sub(1u, std::memory_order_acq_rel);
    assert(previous != 0u);
    return previous == 1u ? 1 : 0;
}
#elif defined(_MSC_VER) && defined(__STDC_NO_ATOMICS__)
#include <intrin.h>
typedef volatile size_t quantapdf_atomic_size;

static inline void quantapdf_refcount_init(
    quantapdf_atomic_size *counter, size_t initial)
{
    *counter = initial;
}

static inline quantapdf_status quantapdf_refcount_retain(
    quantapdf_atomic_size *counter, quantapdf_status overflow_status)
{
    for (;;) {
        size_t current = *counter;
        assert(current != 0u);
        if (current == SIZE_MAX)
            return overflow_status;
#if defined(_M_X64) || defined(_M_ARM64)
        if ((size_t)_InterlockedCompareExchange64(
                (__int64 volatile *)counter,
                (__int64)(current + 1u),
                (__int64)current) == current)
            return QUANTAPDF_OK;
#else
        if ((size_t)_InterlockedCompareExchange(
                (long volatile *)counter,
                (long)(current + 1u),
                (long)current) == current)
            return QUANTAPDF_OK;
#endif
    }
}

static inline int quantapdf_refcount_release(quantapdf_atomic_size *counter)
{
#if defined(_M_X64) || defined(_M_ARM64)
    size_t const remaining = (size_t)_InterlockedDecrement64(
        (__int64 volatile *)counter);
#else
    size_t const remaining = (size_t)_InterlockedDecrement(
        (long volatile *)counter);
#endif
    return remaining == 0u ? 1 : 0;
}
#else
#include <stdatomic.h>
typedef _Atomic size_t quantapdf_atomic_size;
_Static_assert(sizeof(quantapdf_atomic_size) == sizeof(size_t),
    "_Atomic size_t must match size_t layout across C/C++ boundary");

static inline void quantapdf_refcount_init(
    quantapdf_atomic_size *counter, size_t initial)
{
    atomic_init(counter, initial);
}

static inline quantapdf_status quantapdf_refcount_retain(
    quantapdf_atomic_size *counter, quantapdf_status overflow_status)
{
    size_t current = atomic_load_explicit(counter, memory_order_relaxed);
    assert(current != 0u);
    while (current != SIZE_MAX) {
        if (atomic_compare_exchange_weak_explicit(
                counter, &current, current + 1u,
                memory_order_relaxed, memory_order_relaxed))
            return QUANTAPDF_OK;
    }
    return overflow_status;
}

static inline int quantapdf_refcount_release(quantapdf_atomic_size *counter)
{
    size_t const previous = atomic_fetch_sub_explicit(
        counter, 1u, memory_order_acq_rel);
    assert(previous != 0u);
    return previous == 1u ? 1 : 0;
}
#endif

#endif
