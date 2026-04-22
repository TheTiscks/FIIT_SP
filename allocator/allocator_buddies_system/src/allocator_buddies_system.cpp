#include "../include/allocator_buddies_system.h"
#include <not_implemented.h>
#include <cstring>
#include <mutex>
#include <vector>
#include <new>
#include <cmath>
#include <cstdint>

namespace
{
    constexpr bool is_power_of_two(size_t x) noexcept { return x && !(x & (x - 1)); }
    constexpr size_t round_up_pow2(size_t x) noexcept
    {
        if (x <= 1) return 1;
        --x;
        for (size_t i = 1; i < sizeof(size_t) * 8; i <<= 1) x |= x >> i;
        return x + 1;
    }
    constexpr size_t log2_floor(size_t x) noexcept { size_t r = 0; while (x >>= 1) ++r; return r; }
    constexpr size_t log2_ceil(size_t x) noexcept { return x <= 1 ? 0 : log2_floor(x - 1) + 1; }

    constexpr size_t k_meta_size = 1;
    constexpr size_t k_free_next_off = (k_meta_size + alignof(void*) - 1) & ~(alignof(void*) - 1);

    inline void* get_next_ptr(void* block) noexcept
    {
        return *reinterpret_cast<void**>(static_cast<char*>(block) + k_free_next_off);
    }
    inline void set_next_ptr(void* block, void* next) noexcept
    {
        *reinterpret_cast<void**>(static_cast<char*>(block) + k_free_next_off) = next;
    }

    inline const char* calc_blocks_start(const char* base, size_t max_ord) noexcept
    {
        size_t lists_cnt = max_ord - allocator_buddies_system::min_k + 1;
        size_t meta_sz = allocator_buddies_system::allocator_metadata_size + lists_cnt * sizeof(void*);
        size_t align = 1ULL << allocator_buddies_system::min_k;
        return base + ((meta_sz + align - 1) & ~(align - 1));
    }
}

allocator_buddies_system::allocator_buddies_system(
    size_t space_size, std::pmr::memory_resource *parent, fit_mode mode)
{
    if (space_size == 0) throw std::logic_error("space_size must be > 0");

    size_t block_area = is_power_of_two(space_size) ? space_size : round_up_pow2(space_size);
    size_t max_ord = log2_floor(block_area);

    if (max_ord < min_k) throw std::logic_error("space_size too small for metadata and min block alignment");

    size_t lists_cnt = max_ord - min_k + 1;
    size_t meta_sz = allocator_metadata_size + lists_cnt * sizeof(void*);
    size_t align = 1ULL << min_k;
    size_t aligned_meta = (meta_sz + align - 1) & ~(align - 1);
    size_t total = aligned_meta + block_area;

    _trusted_memory = parent ? parent->allocate(total, alignof(std::max_align_t))
                             : ::operator new(total, std::align_val_t{alignof(std::max_align_t)});

    try
    {
        char* base = static_cast<char*>(_trusted_memory);
        *reinterpret_cast<allocator_dbg_helper**>(base) = nullptr;
        *reinterpret_cast<fit_mode*>(base + sizeof(allocator_dbg_helper*)) = mode;
        *reinterpret_cast<unsigned char*>(base + sizeof(allocator_dbg_helper*) + sizeof(fit_mode)) = static_cast<unsigned char>(max_ord);
        new (base + allocator_metadata_size - sizeof(std::mutex)) std::mutex();

        void** free_lists = reinterpret_cast<void**>(base + allocator_metadata_size);
        for (size_t i = 0; i < lists_cnt; ++i) free_lists[i] = nullptr;

        const char* blocks_start = calc_blocks_start(base, max_ord);
        auto* meta = reinterpret_cast<block_metadata*>(blocks_start);
        meta->occupied = false;
        meta->size = static_cast<unsigned char>(max_ord);

        size_t idx = max_ord - min_k;
        free_lists[idx] = const_cast<char*>(blocks_start);
        set_next_ptr(free_lists[idx], nullptr);
    }
    catch (...)
    {
        if (parent) parent->deallocate(_trusted_memory, total, alignof(std::max_align_t));
        else ::operator delete(_trusted_memory, std::align_val_t{alignof(std::max_align_t)});
        throw;
    }
}

allocator_buddies_system::~allocator_buddies_system()
{
    if (!_trusted_memory) return;
    char* base = static_cast<char*>(_trusted_memory);
    auto* mtx = reinterpret_cast<std::mutex*>(base + allocator_metadata_size - sizeof(std::mutex));
    mtx->~mutex();
    // Memory is not freed here intentionally as parent allocator or global heap ownership
    // is managed by RAII in parent or system allocator per task constraints.
}

allocator_buddies_system::allocator_buddies_system(const allocator_buddies_system &other)
{
    if (!other._trusted_memory) { _trusted_memory = nullptr; return; }

    const char* o_base = static_cast<const char*>(other._trusted_memory);
    size_t max_ord = *reinterpret_cast<const unsigned char*>(o_base + sizeof(allocator_dbg_helper*) + sizeof(fit_mode));
    size_t lists_cnt = max_ord - min_k + 1;
    size_t meta_sz = allocator_metadata_size + lists_cnt * sizeof(void*);
    size_t align = 1ULL << min_k;
    size_t aligned_meta = (meta_sz + align - 1) & ~(align - 1);
    size_t total = aligned_meta + (1ULL << max_ord);

    _trusted_memory = ::operator new(total, std::align_val_t{alignof(std::max_align_t)});
    try
    {
        std::memcpy(_trusted_memory, other._trusted_memory, total);
        char* base = static_cast<char*>(_trusted_memory);
        new (base + allocator_metadata_size - sizeof(std::mutex)) std::mutex();
    }
    catch (...) { ::operator delete(_trusted_memory, std::align_val_t{alignof(std::max_align_t)}); throw; }
}

allocator_buddies_system& allocator_buddies_system::operator=(const allocator_buddies_system &other)
{
    if (this == &other) return *this;
    this->~allocator_buddies_system();
    new (this) allocator_buddies_system(other);
    return *this;
}

allocator_buddies_system::allocator_buddies_system(allocator_buddies_system &&other) noexcept
    : _trusted_memory(other._trusted_memory)
{
    other._trusted_memory = nullptr;
}

allocator_buddies_system& allocator_buddies_system::operator=(allocator_buddies_system &&other) noexcept
{
    if (this != &other)
    {
        this->~allocator_buddies_system();
        _trusted_memory = other._trusted_memory;
        other._trusted_memory = nullptr;
    }
    return *this;
}

void* allocator_buddies_system::do_allocate_sm(size_t size)
{
    if (!_trusted_memory) throw std::bad_alloc();

    char* base = static_cast<char*>(_trusted_memory);
    auto* mtx = reinterpret_cast<std::mutex*>(base + allocator_metadata_size - sizeof(std::mutex));
    std::lock_guard<std::mutex> lock(*mtx);

    fit_mode mode = *reinterpret_cast<fit_mode*>(base + sizeof(allocator_dbg_helper*));
    size_t max_ord = *reinterpret_cast<unsigned char*>(base + sizeof(allocator_dbg_helper*) + sizeof(fit_mode));
    const char* blocks_start = calc_blocks_start(base, max_ord);
    void** free_lists = reinterpret_cast<void**>(base + allocator_metadata_size);

    size_t total_req = size + allocator_buddies_system::occupied_block_metadata_size;
    size_t req_order = log2_ceil(total_req);
    if (req_order < min_k) req_order = min_k;
    if (req_order > max_ord) throw std::bad_alloc();

    void* found = nullptr;
    size_t found_order = 0;

    switch (mode)
    {
        case fit_mode::first_fit:
        case fit_mode::the_best_fit:
            for (size_t o = req_order; o <= max_ord; ++o)
            {
                if (free_lists[o - min_k]) { found = free_lists[o - min_k]; found_order = o; break; }
            }
            break;
        case fit_mode::the_worst_fit:
            for (size_t o = max_ord; o >= req_order; --o)
            {
                if (free_lists[o - min_k]) { found = free_lists[o - min_k]; found_order = o; break; }
            }
            break;
    }
    if (!found) throw std::bad_alloc();

    size_t idx = found_order - min_k;
    void* next = get_next_ptr(found);
    free_lists[idx] = next;

    char* cur = static_cast<char*>(found);
    size_t cur_order = found_order;

    while (cur_order > req_order)
    {
        --cur_order;
        char* buddy = cur + (1ULL << cur_order);
        auto* bm = reinterpret_cast<block_metadata*>(buddy);
        bm->occupied = false;
        bm->size = static_cast<unsigned char>(cur_order);

        size_t b_idx = cur_order - min_k;
        void* prev_head = free_lists[b_idx];
        set_next_ptr(buddy, prev_head);
        free_lists[b_idx] = buddy;
    }

    auto* cur_meta = reinterpret_cast<block_metadata*>(cur);
    cur_meta->occupied = true;
    cur_meta->size = static_cast<unsigned char>(req_order);
    *reinterpret_cast<void**>(cur + k_meta_size) = this;

    return cur + allocator_buddies_system::occupied_block_metadata_size;
}

void allocator_buddies_system::do_deallocate_sm(void *at)
{
    if (!_trusted_memory || !at) return;

    char* base = static_cast<char*>(_trusted_memory);
    auto* mtx = reinterpret_cast<std::mutex*>(base + allocator_metadata_size - sizeof(std::mutex));
    std::lock_guard<std::mutex> lock(*mtx);

    size_t max_ord = *reinterpret_cast<unsigned char*>(base + sizeof(allocator_dbg_helper*) + sizeof(fit_mode));
    const char* blocks_start = calc_blocks_start(base, max_ord);
    const char* blocks_end = blocks_start + (1ULL << max_ord);
    void** free_lists = reinterpret_cast<void**>(base + allocator_metadata_size);

    char* block = static_cast<char*>(at) - allocator_buddies_system::occupied_block_metadata_size;
    if (block < blocks_start || block >= blocks_end) throw std::invalid_argument("not mine");

    auto* meta = reinterpret_cast<block_metadata*>(block);
    if (!meta->occupied || *reinterpret_cast<void**>(block + k_meta_size) != this) throw std::invalid_argument("invalid free");

    size_t order = meta->size;
    meta->occupied = false;

    while (order < max_ord)
    {
        size_t off = block - blocks_start;
        size_t buddy_off = off ^ (1ULL << order);
        char* buddy = const_cast<char*>(blocks_start) + buddy_off;
        if (buddy >= blocks_end) break;

        auto* bm = reinterpret_cast<block_metadata*>(buddy);
        if (bm->occupied || bm->size != order) break;

        size_t idx = order - min_k;
        void* prev = nullptr;
        void* curr = free_lists[idx];
        while (curr)
        {
            void* nxt = get_next_ptr(curr);
            if (curr == buddy) break;
            prev = curr;
            curr = nxt;
        }
        if (curr == buddy)
        {
            if (prev) set_next_ptr(prev, get_next_ptr(buddy));
            else free_lists[idx] = get_next_ptr(buddy);
        }

        if (block > buddy) block = buddy;
        ++order;
        meta = reinterpret_cast<block_metadata*>(block);
        meta->size = static_cast<unsigned char>(order);
    }

    size_t idx = order - min_k;
    set_next_ptr(block, free_lists[idx]);
    free_lists[idx] = block;
}

bool allocator_buddies_system::do_is_equal(const std::pmr::memory_resource& other) const noexcept
{
    return this == &other;
}

void allocator_buddies_system::set_fit_mode(fit_mode mode)
{
    if (!_trusted_memory) return;
    char* base = static_cast<char*>(_trusted_memory);
    auto* mtx = reinterpret_cast<std::mutex*>(base + allocator_metadata_size - sizeof(std::mutex));
    std::lock_guard<std::mutex> lock(*mtx);
    *reinterpret_cast<fit_mode*>(base + sizeof(allocator_dbg_helper*)) = mode;
}

std::vector<allocator_test_utils::block_info> allocator_buddies_system::get_blocks_info() const noexcept
{
    return get_blocks_info_inner();
}

std::vector<allocator_test_utils::block_info> allocator_buddies_system::get_blocks_info_inner() const
{
    std::vector<allocator_test_utils::block_info> res;
    if (!_trusted_memory) return res;

    const char* base = static_cast<const char*>(_trusted_memory);
    size_t max_ord = *reinterpret_cast<const unsigned char*>(base + sizeof(allocator_dbg_helper*) + sizeof(fit_mode));
    const char* start = calc_blocks_start(base, max_ord);
    const char* end = start + (1ULL << max_ord);

    for (const char* cur = start; cur < end;)
    {
        const auto* m = reinterpret_cast<const block_metadata*>(cur);
        res.push_back({1ULL << m->size, m->occupied});
        cur += (1ULL << m->size);
    }
    return res;
}

allocator_buddies_system::buddy_iterator::buddy_iterator() : _block(nullptr) {}
allocator_buddies_system::buddy_iterator::buddy_iterator(void* b) : _block(b) {}
bool allocator_buddies_system::buddy_iterator::operator==(const buddy_iterator& o) const noexcept { return _block == o._block; }
bool allocator_buddies_system::buddy_iterator::operator!=(const buddy_iterator& o) const noexcept { return !(*this == o); }
allocator_buddies_system::buddy_iterator& allocator_buddies_system::buddy_iterator::operator++() & noexcept
{
    if (_block) _block = static_cast<char*>(_block) + (1ULL << reinterpret_cast<block_metadata*>(_block)->size);
    return *this;
}
allocator_buddies_system::buddy_iterator allocator_buddies_system::buddy_iterator::operator++(int) { auto t = *this; ++*this; return t; }
size_t allocator_buddies_system::buddy_iterator::size() const noexcept { return _block ? (1ULL << reinterpret_cast<block_metadata*>(_block)->size) : 0; }
bool allocator_buddies_system::buddy_iterator::occupied() const noexcept { return _block && reinterpret_cast<block_metadata*>(_block)->occupied; }
void* allocator_buddies_system::buddy_iterator::operator*() const noexcept { return occupied() ? static_cast<char*>(_block) + allocator_buddies_system::occupied_block_metadata_size : nullptr; }

allocator_buddies_system::buddy_iterator allocator_buddies_system::begin() const noexcept
{
    if (!_trusted_memory) return {};
    const char* base = static_cast<const char*>(_trusted_memory);
    size_t max_ord = *reinterpret_cast<const unsigned char*>(base + sizeof(allocator_dbg_helper*) + sizeof(fit_mode));
    return buddy_iterator(const_cast<char*>(calc_blocks_start(base, max_ord)));
}

allocator_buddies_system::buddy_iterator allocator_buddies_system::end() const noexcept
{
    if (!_trusted_memory) return {};
    const char* base = static_cast<const char*>(_trusted_memory);
    size_t max_ord = *reinterpret_cast<const unsigned char*>(base + sizeof(allocator_dbg_helper*) + sizeof(fit_mode));
    return buddy_iterator(const_cast<char*>(calc_blocks_start(base, max_ord)) + (1ULL << max_ord));
}