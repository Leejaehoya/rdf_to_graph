#ifndef GRAPH_MEMORY_TRACKER_HPP
#define GRAPH_MEMORY_TRACKER_HPP

#include <cstddef>
#include <atomic>
#include <memory>

/**
 * RDFGraph 내부 컨테이너에서 사용하는 메모리 양을 추적하기 위한 전역 트래커
 *
 * - TrackingAllocator 와 함께 사용
 * - allocate / deallocate 시 add/sub 를 호출하여 바이트 수를 누적
 */
class GraphMemoryTracker {
public:
    /** 누적된 바이트 수에 추가 */
    static void add(std::size_t bytes);

    /** 누적된 바이트 수에서 감소 */
    static void sub(std::size_t bytes);

    /** 현재 누적된 바이트 수 반환 */
    static std::size_t current();

    /** 누적 값 초기화 (보통 RDFGraph 로딩 전에 호출) */
    static void reset();

private:
    static std::atomic<std::size_t> bytes_;
};

/**
 * std::allocator 를 래핑하여 GraphMemoryTracker 에 메모리 사용량을 보고하는 allocator
 */
template <typename T>
struct TrackingAllocator {
    using value_type = T;

    TrackingAllocator() noexcept = default;

    template <class U>
    TrackingAllocator(const TrackingAllocator<U>&) noexcept {}

    T* allocate(std::size_t n) {
        std::size_t bytes = n * sizeof(T);
        GraphMemoryTracker::add(bytes);
        return std::allocator<T>{}.allocate(n);
    }

    void deallocate(T* p, std::size_t n) noexcept {
        std::size_t bytes = n * sizeof(T);
        GraphMemoryTracker::sub(bytes);
        std::allocator<T>{}.deallocate(p, n);
    }

    template <class U>
    bool operator==(const TrackingAllocator<U>&) const noexcept {
        return true;
    }

    template <class U>
    bool operator!=(const TrackingAllocator<U>&) const noexcept {
        return false;
    }
};

#endif // GRAPH_MEMORY_TRACKER_HPP


