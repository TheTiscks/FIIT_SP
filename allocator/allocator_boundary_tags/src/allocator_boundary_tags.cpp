#include "../include/allocator_boundary_tags.h"
#include <new>
#include <cstring>
#include <limits>
#include <mutex>

static constexpr size_t BLOCK_META_SIZE = sizeof(size_t) + 3 * sizeof(void*);

inline static size_t& block_size(void* block) noexcept
{
    return *reinterpret_cast<size_t*>(block);
}
inline static bool& block_is_free(void* block) noexcept
{
    return *reinterpret_cast<bool*>(static_cast<char*>(block) + sizeof(size_t) + 2 * sizeof(void*));
}
inline static void*& block_prev(void* block) noexcept
{
    return *reinterpret_cast<void**>(static_cast<char*>(block) + sizeof(size_t));
}
inline static void*& block_next(void* block) noexcept
{
    return *reinterpret_cast<void**>(static_cast<char*>(block) + sizeof(size_t) + sizeof(void*));
}
inline static void* block_user_ptr(void* block) noexcept
{
    return static_cast<char*>(block) + BLOCK_META_SIZE;
}
inline static void* block_from_user(void* user_ptr) noexcept
{
    return static_cast<char*>(user_ptr) - BLOCK_META_SIZE;
}
inline static std::pmr::memory_resource*& alloc_parent(void* trusted) noexcept
{
    return *reinterpret_cast<std::pmr::memory_resource**>(trusted);
}
inline static allocator_with_fit_mode::fit_mode& alloc_fit_mode(void* trusted) noexcept
{
    return *reinterpret_cast<allocator_with_fit_mode::fit_mode*>(
        static_cast<char*>(trusted) + sizeof(std::pmr::memory_resource*));
}
inline static size_t& alloc_total_size(void* trusted) noexcept
{
    return *reinterpret_cast<size_t*>(
        static_cast<char*>(trusted) + sizeof(std::pmr::memory_resource*) +
            sizeof(allocator_with_fit_mode::fit_mode));
}
inline static std::mutex& alloc_mutex(void* trusted) noexcept
{
    return *reinterpret_cast<std::mutex*>(
        static_cast<char*>(trusted) + sizeof(std::pmr::memory_resource*) + sizeof(allocator_with_fit_mode::fit_mode) +
            sizeof(size_t));
}
inline static void*& alloc_first_block(void* trusted) noexcept
{
    return *reinterpret_cast<void**>(
        static_cast<char*>(trusted) + sizeof(std::pmr::memory_resource*) + sizeof(allocator_with_fit_mode::fit_mode) +
            sizeof(size_t) + sizeof(std::mutex));
}
inline static bool is_valid_block(void* trusted, void* block) noexcept
{
    char* start = static_cast<char*>(trusted);
    char* end = static_cast<char*>(trusted) + alloc_total_size(trusted);
    char* p = static_cast<char*>(block);
    return p >= start && p < end;
}

allocator_boundary_tags::~allocator_boundary_tags()
{
    if (!_trusted_memory) {
        return;
    }
    alloc_mutex(_trusted_memory).~mutex();
    alloc_parent(_trusted_memory)->deallocate(_trusted_memory, alloc_total_size(_trusted_memory),
        alignof(std::max_align_t));
    _trusted_memory = nullptr;
}

allocator_boundary_tags::allocator_boundary_tags(
    size_t space_size,
    std::pmr::memory_resource* parent_allocator,
    allocator_with_fit_mode::fit_mode allocate_fit_mode)
{
    if (!parent_allocator) {
        parent_allocator = std::pmr::get_default_resource();
    }
    size_t full_size = space_size + allocator_metadata_size;
    _trusted_memory = parent_allocator->allocate(full_size, alignof(std::max_align_t));
    if (!_trusted_memory) {
        throw std::bad_alloc();
    }
    alloc_parent(_trusted_memory) = parent_allocator;
    alloc_fit_mode(_trusted_memory) = allocate_fit_mode;
    alloc_total_size(_trusted_memory) = full_size;
    new (&alloc_mutex(_trusted_memory)) std::mutex();
    alloc_first_block(_trusted_memory) = nullptr;
    void* first = static_cast<char*>(_trusted_memory) + allocator_metadata_size;
    if (space_size >= BLOCK_META_SIZE) {
        block_size(first) = space_size;
        block_is_free(first) = true;
        block_prev(first) = nullptr;
        block_next(first) = nullptr;
        alloc_first_block(_trusted_memory) = first;
    }
}

allocator_boundary_tags::allocator_boundary_tags(const allocator_boundary_tags& other)
{
    if (!other._trusted_memory) {
        _trusted_memory = nullptr;
        return;
    }
    std::lock_guard<std::mutex> lock(alloc_mutex(other._trusted_memory));
    size_t total = alloc_total_size(other._trusted_memory);
    auto* parent = alloc_parent(other._trusted_memory);
    _trusted_memory = parent->allocate(total, alignof(std::max_align_t));
    if (!_trusted_memory) {
        throw std::bad_alloc();
    }
    std::memcpy(_trusted_memory, other._trusted_memory, total);
    new (&alloc_mutex(_trusted_memory)) std::mutex;
    ptrdiff_t offset = static_cast<char*>(_trusted_memory) - static_cast<char*>(other._trusted_memory);
    void* curr = static_cast<char*>(_trusted_memory) + allocator_metadata_size;
    void* end = static_cast<char*>(_trusted_memory) + total;
    while (curr < end) {
        if (block_prev(curr)) {
            block_prev(curr) = static_cast<char*>(block_prev(curr)) + offset;
        }
        if (block_next(curr)) {
            block_next(curr) = static_cast<char*>(block_next(curr)) + offset;
        }
        curr = static_cast<char*>(curr) + block_size(curr);
    }
}

allocator_boundary_tags& allocator_boundary_tags::operator=(const allocator_boundary_tags& other) {
    if (this != &other) {
        this->~allocator_boundary_tags();
        new (this) allocator_boundary_tags(other);
    }
    return *this;
}

allocator_boundary_tags::allocator_boundary_tags(allocator_boundary_tags&& other) noexcept
    : _trusted_memory(other._trusted_memory)
{
        other._trusted_memory = nullptr;
}

allocator_boundary_tags& allocator_boundary_tags::operator=(allocator_boundary_tags&& other) noexcept {
    if (this != &other) {
        this->~allocator_boundary_tags();
        _trusted_memory = other._trusted_memory;
        other._trusted_memory = nullptr;
    }
    return *this;
}

[[nodiscard]] void* allocator_boundary_tags::do_allocate_sm(size_t size) {
    std::lock_guard<std::mutex> lock(alloc_mutex(_trusted_memory));
    size_t required = size + BLOCK_META_SIZE;
    fit_mode mode = alloc_fit_mode(_trusted_memory);
    void* best = nullptr;
    size_t best_diff = std::numeric_limits<size_t>::max();
    size_t best_sz = 0;
    void* curr = static_cast<char*>(_trusted_memory) + allocator_metadata_size;
    void* end = static_cast<char*>(_trusted_memory) + alloc_total_size(_trusted_memory);
    bool found = false;
    while (curr < end && !found) {
        if (block_is_free(curr) && block_size(curr) >= required) {
            switch (mode) {
                case fit_mode::first_fit:
                    best = curr;
                    found = true;
                    break;
                case fit_mode::the_best_fit: {
                    size_t diff = block_size(curr) - required;
                    if (diff < best_diff) {
                        best_diff = diff;
                        best = curr;
                        if (diff == 0) {
                            found = true;
                        }
                    }
                    break;
                }
                case fit_mode::the_worst_fit:
                    if (block_size(curr) > best_sz) {
                        best_sz = block_size(curr); best = curr;
                    }
                    break;
            }
        }
        if (!found) {
            curr = static_cast<char*>(curr) + block_size(curr);
        }
    }
    if (!best) {
        throw std::bad_alloc();
    }
    void* prev_free = block_prev(best);
    void* next_free = block_next(best);
    if (prev_free) {
        block_next(prev_free) = next_free;
    } else if (alloc_first_block(_trusted_memory) == best) {
        alloc_first_block(_trusted_memory) = next_free;
    }
    if (next_free) {
        block_prev(next_free) = prev_free;
    }
    size_t remaining = block_size(best) - required;
    if (remaining >= BLOCK_META_SIZE) {
        block_size(best) = required;
        void* new_free = static_cast<char*>(best) + required;
        block_size(new_free) = remaining;
        block_is_free(new_free) = true;
        block_prev(new_free) = nullptr;
        block_next(new_free) = alloc_first_block(_trusted_memory);
        if (alloc_first_block(_trusted_memory)) {
            block_prev(alloc_first_block(_trusted_memory)) = new_free; //
        }
        alloc_first_block(_trusted_memory) = new_free;
    }
    block_is_free(best) = false;
    return block_user_ptr(best);
}

void allocator_boundary_tags::do_deallocate_sm(void* at) {
    if (!at) {
        return;
    }
    std::lock_guard<std::mutex> lock(alloc_mutex(_trusted_memory));
    void* block = block_from_user(at);
    if (!is_valid_block(_trusted_memory, block) || block_is_free(block)) {
        return;
    }
    block_is_free(block) = true;
    void* prev = nullptr;
    void* curr = alloc_first_block(_trusted_memory);
    while (curr && curr < block) {
        prev = curr; curr = block_next(curr);
    }
    block_prev(block) = prev;
    block_next(block) = curr;
    if (prev) {
        block_next(prev) = block;
    } else {
        alloc_first_block(_trusted_memory) = block;
    }
    if (curr) {
        block_prev(curr) = block;
    }
    void* next = block_next(block);
    if (next && static_cast<char*>(block) + block_size(block) == next) {
        block_size(block) += block_size(next);
        void* nn = block_next(next);
        block_next(block) = nn;
        if (nn) {
            block_prev(nn) = block;
        }
    }
    prev = block_prev(block);
    if (prev && static_cast<char*>(prev) + block_size(prev) == block) {
        block_size(prev) += block_size(block);
        block_next(prev) = block_next(block);
        if (block_next(block)) {
            block_prev(block_next(block)) = prev;
        }
    }
}

bool allocator_boundary_tags::do_is_equal(const std::pmr::memory_resource& other) const noexcept
{
    return this == &other;
}

void allocator_boundary_tags::set_fit_mode(allocator_with_fit_mode::fit_mode mode)
{
    std::lock_guard<std::mutex> lock(alloc_mutex(_trusted_memory));
    alloc_fit_mode(_trusted_memory) = mode;
}

std::vector<allocator_test_utils::block_info> allocator_boundary_tags::get_blocks_info() const
{
    return get_blocks_info_inner();
}

std::vector<allocator_test_utils::block_info> allocator_boundary_tags::get_blocks_info_inner() const
{
    if (!_trusted_memory) {
        return {};
    }
    std::lock_guard<std::mutex> lock(alloc_mutex(_trusted_memory));
    std::vector<allocator_test_utils::block_info> result;
    void* curr = static_cast<char*>(_trusted_memory) + allocator_metadata_size;
    void* end = static_cast<char*>(_trusted_memory) + alloc_total_size(_trusted_memory);
    while (curr < end) {
        result.push_back({ block_size(curr), !block_is_free(curr) });
        curr = static_cast<char*>(curr) + block_size(curr);
    }
    return result;
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::begin() const noexcept
{
    return boundary_iterator(_trusted_memory);
}
allocator_boundary_tags::boundary_iterator allocator_boundary_tags::end() const noexcept
{
    return boundary_iterator();
}

allocator_boundary_tags::boundary_iterator::boundary_iterator() : _occupied_ptr(nullptr), _occupied(false),
    _trusted_memory(nullptr)
{
}

allocator_boundary_tags::boundary_iterator::boundary_iterator(void* trusted) : _occupied_ptr(nullptr), _occupied(false),
    _trusted_memory(trusted)
{
    if (trusted) {
        _occupied_ptr = static_cast<char*>(trusted) + allocator_boundary_tags::allocator_metadata_size;
        void* end = static_cast<char*>(trusted) + alloc_total_size(trusted);
        if (_occupied_ptr >= end) {
            _occupied_ptr = nullptr;
        } else {
            _occupied = !block_is_free(_occupied_ptr);
        }
    }
}

bool allocator_boundary_tags::boundary_iterator::operator==(const boundary_iterator& other) const noexcept
{
    return _occupied_ptr == other._occupied_ptr;
}
bool allocator_boundary_tags::boundary_iterator::operator!=(const boundary_iterator& other) const noexcept
{
    return !(*this == other);
}

allocator_boundary_tags::boundary_iterator& allocator_boundary_tags::boundary_iterator::operator++() & noexcept
{
    if (!_occupied_ptr || !_trusted_memory) {
        return *this;
    }
    void* end = static_cast<char*>(_trusted_memory) + alloc_total_size(_trusted_memory);
    _occupied_ptr = static_cast<char*>(_occupied_ptr) + block_size(_occupied_ptr);
    if (_occupied_ptr >= end) {
        _occupied_ptr = nullptr;
        _occupied = false;
    } else {
        _occupied = !block_is_free(_occupied_ptr);
    }
    return *this;
}

allocator_boundary_tags::boundary_iterator& allocator_boundary_tags::boundary_iterator::operator--() & noexcept
{
    if (!_occupied_ptr || !_trusted_memory) {
        return *this;
    }
    void* start = static_cast<char*>(_trusted_memory) + allocator_boundary_tags::allocator_metadata_size;
    if (_occupied_ptr == start) {
        _occupied_ptr = nullptr; _occupied = false; return *this;
    }
    void* curr = start;
    void* prev = nullptr;
    while (curr && curr < _occupied_ptr) { prev = curr; curr = static_cast<char*>(curr) + block_size(curr); }
    _occupied_ptr = prev;
    if (_occupied_ptr) {
        _occupied = !block_is_free(_occupied_ptr);
    } else {
        _occupied = false;
    }
    return *this;
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::boundary_iterator::operator++(int)
{
    boundary_iterator tmp = *this;
    ++(*this);
    return tmp;
}
allocator_boundary_tags::boundary_iterator allocator_boundary_tags::boundary_iterator::operator--(int)
{
    boundary_iterator tmp = *this;
    --(*this);
    return tmp;
}

size_t allocator_boundary_tags::boundary_iterator::size() const noexcept
{
    return _occupied_ptr ? block_size(_occupied_ptr) - BLOCK_META_SIZE : 0;
}
bool allocator_boundary_tags::boundary_iterator::occupied() const noexcept
{
    return _occupied;
}

void* allocator_boundary_tags::boundary_iterator::operator*() const noexcept
{
    return _occupied_ptr ? block_user_ptr(_occupied_ptr) : nullptr;
}

void* allocator_boundary_tags::boundary_iterator::get_ptr() const noexcept
{
    return _occupied_ptr;
}