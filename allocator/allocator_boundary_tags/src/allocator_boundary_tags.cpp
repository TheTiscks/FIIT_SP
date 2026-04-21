#include <not_implemented.h>
#include "../include/allocator_boundary_tags.h"
#include <new>

static inline std::pmr::memory_resource*& get_parent(void* trusted) noexcept {
    return *reinterpret_cast<std::pmr::memory_resource**>(trusted);
}

static inline allocator_with_fit_mode::fit_mode& get_fit_mode(void* trusted) noexcept {
    return *reinterpret_cast<allocator_with_fit_mode::fit_mode*>(
        static_cast<char*>(trusted) + sizeof(std::pmr::memory_resource*));
}

static inline size_t& get_total_space(void* trusted) noexcept {
    return *reinterpret_cast<size_t*>(
        static_cast<char*>(trusted) + sizeof(std::pmr::memory_resource*) + sizeof(allocator_with_fit_mode::fit_mode));
}

static inline std::mutex& get_mutex(void* trusted) noexcept {
    return *reinterpret_cast<std::mutex*>(
        static_cast<char*>(trusted) + sizeof(std::pmr::memory_resource*) + sizeof(allocator_with_fit_mode::fit_mode)
        + sizeof(size_t));
}

static inline void*& get_free_head(void* trusted) noexcept {
    return *reinterpret_cast<void**>(
        static_cast<char*>(trusted) + sizeof(std::pmr::memory_resource*) + sizeof(allocator_with_fit_mode::fit_mode)
        + sizeof(size_t) + sizeof(std::mutex));
}


static constexpr size_t block_header_size = sizeof(size_t) + sizeof(bool);
static constexpr size_t block_footer_size = sizeof(size_t);
static constexpr size_t min_free_block_size = block_header_size + block_footer_size + sizeof(void*) * 2;

inline static size_t& block_size(void* block) noexcept {
    return *reinterpret_cast<size_t*>(block);
}

inline static bool& block_used(void* block) noexcept {
    return *reinterpret_cast<bool*>(static_cast<char*>(block) + sizeof(size_t));
}

inline static void*& block_prev_free(void* block) noexcept {
    return *reinterpret_cast<void**>(static_cast<char*>(block) + block_header_size);
}

inline static void*& block_next_free(void* block) noexcept {
    return *reinterpret_cast<void**>(static_cast<char*>(block) + block_header_size + sizeof(void*));
}

inline static size_t& block_footer(void* block) noexcept {
    return *reinterpret_cast<size_t*>(static_cast<char*>(block) + block_size(block) - block_footer_size);
}

inline static void* block_from_user(void* user_ptr) noexcept {
    return static_cast<char*>(user_ptr) - block_header_size;
}

inline static void* block_data(void* block) noexcept {
    return static_cast<char*>(block) + block_header_size;
}

static inline bool is_address_in_range(void* trusted, void* addr) noexcept {
    char* start = static_cast<char*>(trusted);
    char* end = start + get_total_space(trusted);
    char* p = static_cast<char*>(addr);
    return p >= start && p < end;
}


allocator_boundary_tags::~allocator_boundary_tags()
{
    if (!_trusted_memory) {
        return;
    }
    get_mutex(_trusted_memory).~mutex();
    std::pmr::memory_resource* parent = get_parent(_trusted_memory);
    parent->deallocate(_trusted_memory, get_total_space(_trusted_memory), alignof(std::max_align_t));
    _trusted_memory = nullptr;
}

allocator_boundary_tags::allocator_boundary_tags(
    allocator_boundary_tags &&other) noexcept
{
    if (!other._trusted_memory) {
        _trusted_memory = nullptr;
        return;
    }
    std::lock_guard<std::mutex> lock(get_mutex(other._trusted_memory));
    size_t space_size = get_total_space(other._trusted_memory);
    std::pmr::memory_resource* parent = get_parent(other._trusted_memory);
    _trusted_memory = parent->allocate(space_size, alignof(std::max_align_t));
    if (!_trusted_memory) {
        throw std::bad_alloc();
    }
    get_parent(_trusted_memory) = parent;
    get_fit_mode(_trusted_memory) = get_fit_mode(other._trusted_memory);
    get_total_space(_trusted_memory) = space_size;
    new (&get_mutex(_trusted_memory)) std::mutex();
    get_free_head(_trusted_memory) = nullptr;
    void* src = get_free_head(other._trusted_memory); // копируем список свободных
    void* prev_dst = nullptr;
    while (src) {
        size_t sz = block_size(src);
        void* dst_block = static_cast<char*>(_trusted_memory) + (static_cast<char*>(src) - static_cast<char*>(other._trusted_memory));
        block_size(dst_block) = sz;
        block_used(dst_block) = false;
        block_prev_free(dst_block) = nullptr;
        block_next_free(dst_block) = nullptr;
        block_footer(dst_block) = sz;
        if (prev_dst) {
            block_next_free(prev_dst) = dst_block;
        } else {
            get_free_head(_trusted_memory) = dst_block;
        }
        prev_dst = dst_block;
        src = block_next_free(src);
    }
}

allocator_boundary_tags &allocator_boundary_tags::operator=(
    allocator_boundary_tags &&other) noexcept
{
    if (this != &other) {
        this->~allocator_boundary_tags();
        _trusted_memory = other._trusted_memory;
        other._trusted_memory = nullptr;
    }
    return *this;
}

allocator_boundary_tags::allocator_boundary_tags(allocator_boundary_tags&& other) noexcept
    : _trusted_memory(other._trusted_memory)
{
    other._trusted_memory = nullptr;
}

/** If parent_allocator* == nullptr you should use std::pmr::get_default_resource()
 */
allocator_boundary_tags::allocator_boundary_tags(
        size_t space_size,
        std::pmr::memory_resource *parent_allocator,
        allocator_with_fit_mode::fit_mode allocate_fit_mode)
{
    throw not_implemented("allocator_boundary_tags::allocator_boundary_tags(size_t,std::pmr::memory_resource *,logger *,allocator_with_fit_mode::fit_mode)", "your code should be here...");
}

[[nodiscard]] void *allocator_boundary_tags::do_allocate_sm(
    size_t size)
{
    if (size == 0) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(get_mutex(_trusted_memory));
    fit_mode mode = get_fit_mode(_trusted_memory);
    void* best = nullptr;
    void* best_prev = nullptr;
    void* prev = nullptr;
    void* curr = get_free_head(_trusted_memory);
    if (mode == fit_mode::first_fit) { // ищем подходящий блок
        while (curr) {
            size_t usable = block_size(curr) - block_header_size - block_footer_size;
            if (usable >= size) {
                best = curr;
                best_prev = prev;
                break;
            }
            prev = curr;
            curr = block_next_free(curr);
        }
    } else if (mode == fit_mode::the_best_fit) {
        size_t best_diff = ~size_t(0);
        while (curr) {
            size_t usable = block_size(curr) - block_header_size - block_footer_size;
            if (usable >= size && usable - size < best_diff) {
                best_diff = usable - size;
                best = curr;
                best_prev = prev;
            }
            prev = curr;
            curr = block_next_free(curr);
        }
    } else if (mode == fit_mode::the_worst_fit) {
        size_t best_sz = 0;
        while (curr) {
            size_t usable = block_size(curr) - block_header_size - block_footer_size;
            if (usable >= size && usable > best_sz) {
                best_sz = usable;
                best = curr;
                best_prev = prev;
            }
            prev = curr;
            curr = block_next_free(curr);
        }
    }
    if (!best) {
        throw std::bad_alloc();
    }
    // удалим best
    if (best_prev) {
        block_next_free(best_prev) = block_next_free(best);
    } else {
        get_free_head(_trusted_memory) = block_next_free(best);
    }
    if (block_next_free(best)) {
        block_prev_free(block_next_free(best)) = best_prev;
    }
    size_t total_block_sz = block_size(best);
    size_t needed_total = size + block_header_size + block_footer_size;
    size_t remaining = total_block_sz - needed_total;
    if (remaining >= min_free_block_size) {
        block_size(best) = needed_total; // разделяем на выделенный и своб
        block_used(best) = true;
        block_footer(best) = needed_total;
        void* new_free = static_cast<char*>(best) + needed_total;
        block_size(new_free) = remaining;
        block_used(new_free) = false;
        block_prev_free(new_free) = nullptr;
        block_next_free(new_free) = get_free_head(_trusted_memory);
        if (get_free_head(_trusted_memory)) {
            block_prev_free(get_free_head(_trusted_memory)) = new_free;
        }
        get_free_head(_trusted_memory) = new_free;
        block_footer(new_free) = remaining;
    } else {
        block_used(best) = true; // исп весь блок
    }
    return block_data(best);
}

void allocator_boundary_tags::do_deallocate_sm(
    void *at)
{
    if (!at) {
        return;
    }
    std::lock_guard<std::mutex> lock(get_mutex(_trusted_memory));
    void* block = block_from_user(at);
    if (!is_address_in_range(_trusted_memory, block)) {
        return;
    }
    block_used(block) = false;
    void* next_block = static_cast<char*>(block) + block_size(block); // Проверка - след блок
    char* end = static_cast<char*>(_trusted_memory) + get_total_space(_trusted_memory);
    if (next_block < end && !block_used(next_block)) {
        void* next_prev = block_prev_free(next_block); // объед с next_block, удаляем next из свободных
        void* next_next = block_next_free(next_block);
        if (next_prev) {
            block_next_free(next_prev) = next_next;
        } else {
            get_free_head(_trusted_memory) = next_next;
        }
        if (next_next) {
            block_prev_free(next_next) = next_prev;
        }
        block_size(block) += block_size(next_block);
        block_footer(block) = block_size(block);
    }
    if (block > static_cast<char*>(_trusted_memory) + allocator_metadata_size) { // проверка - пред. блок
        size_t prev_size = *reinterpret_cast<size_t*>(static_cast<char*>(block) - block_footer_size);
        void* prev_block = static_cast<char*>(block) - prev_size;
        if (prev_block >= static_cast<char*>(_trusted_memory) + allocator_metadata_size && !block_used(prev_block)) {
            void* prev_prev = block_prev_free(prev_block); // объед с prev_block
            void* prev_next = block_next_free(prev_block);
            if (prev_prev) {
                block_next_free(prev_prev) = prev_next;
            } else {
                get_free_head(_trusted_memory) = prev_next;
            }
            if (prev_next) {
                block_prev_free(prev_next) = prev_prev;
            }
            block_size(prev_block) += block_size(block);
            block_footer(prev_block) = block_size(prev_block);
            block = prev_block;
        }
    }
    void* prev = nullptr; // +блок в список свободных
    void* curr = get_free_head(_trusted_memory);
    while (curr && curr < block) {
        prev = curr;
        curr = block_next_free(curr);
    }
    block_prev_free(block) = prev;
    block_next_free(block) = curr;
    if (prev) {
        block_next_free(prev) = block;
    } else {
        get_free_head(_trusted_memory) = block;
    }
    if (curr) {
        block_prev_free(curr) = block;
    }
}

bool allocator_boundary_tags::do_is_equal(const std::pmr::memory_resource& other) const noexcept
{
    return this == &other;
}

inline void allocator_boundary_tags::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
    std::lock_guard<std::mutex> lock(get_mutex(_trusted_memory));
    get_fit_mode(_trusted_memory) = mode;
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
    std::lock_guard<std::mutex> lock(get_mutex(_trusted_memory));
    std::vector<allocator_test_utils::block_info> result;
    void* curr = static_cast<char*>(_trusted_memory) + allocator_metadata_size;
    void* end = static_cast<char*>(_trusted_memory) + get_total_space(_trusted_memory);
    while (curr < end) {
        size_t sz = block_size(curr) - block_header_size - block_footer_size;
        bool occupied = block_used(curr);
        result.push_back({sz, occupied});
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
    return boundary_iterator(nullptr);
}


allocator_boundary_tags& allocator_boundary_tags::operator=(const allocator_boundary_tags& other)
{
    if (this != &other) {
        this->~allocator_boundary_tags();
        new (this) allocator_boundary_tags(other);
    }
    return *this;
}
bool allocator_boundary_tags::boundary_iterator::operator==(
        const allocator_boundary_tags::boundary_iterator &other) const noexcept
{
    return _occupied_ptr == other._occupied_ptr;
}

bool allocator_boundary_tags::boundary_iterator::operator!=(
        const allocator_boundary_tags::boundary_iterator & other) const noexcept
{
    return !(*this == other);
}

allocator_boundary_tags::boundary_iterator &allocator_boundary_tags::boundary_iterator::operator++() & noexcept
{
    if (!_occupied_ptr) {
        return *this;
    }
    size_t sz = block_size(_occupied_ptr);
    _occupied_ptr = static_cast<char*>(_occupied_ptr) + sz;
    if (_trusted_memory && _occupied_ptr >= static_cast<char*>(_trusted_memory) + get_total_space(_trusted_memory)) {
        _occupied_ptr = nullptr;
    }
    return *this;
}

allocator_boundary_tags::boundary_iterator &allocator_boundary_tags::boundary_iterator::operator--() & noexcept
{
    if (!_occupied_ptr || !_trusted_memory) {
        return *this;
    }
    if (_occupied_ptr == static_cast<char*>(_trusted_memory) + allocator_metadata_size) {
        _occupied_ptr = nullptr;
        return *this;
    }
    size_t prev_sz = *reinterpret_cast<size_t*>(static_cast<char*>(_occupied_ptr) - block_footer_size);
    _occupied_ptr = static_cast<char*>(_occupied_ptr) - prev_sz;
    return *this;
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::boundary_iterator::operator++(int n)
{
    boundary_iterator tmp = *this;
    ++(*this);
    return tmp;
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::boundary_iterator::operator--(int n)
{
    boundary_iterator tmp = *this;
    --(*this);
    return tmp;
}

size_t allocator_boundary_tags::boundary_iterator::size() const noexcept
{
    if (!_occupied_ptr) {
        return 0;
    }
    return block_size(_occupied_ptr) - block_header_size - block_footer_size;
}

bool allocator_boundary_tags::boundary_iterator::occupied() const noexcept
{
    return _occupied_ptr ? block_used(_occupied_ptr) : false;
}

void* allocator_boundary_tags::boundary_iterator::operator*() const noexcept
{
    return _occupied_ptr ? block_data(_occupied_ptr) : nullptr;
}

allocator_boundary_tags::boundary_iterator::boundary_iterator()
    : _occupied_ptr(nullptr), _occupied(false), _trusted_memory(nullptr)
{
}

allocator_boundary_tags::boundary_iterator::boundary_iterator(void *trusted)
    : _occupied_ptr(trusted ? static_cast<char*>(trusted) + allocator_metadata_size : nullptr), _occupied(false),
      _trusted_memory(trusted)
{
    if (_trusted_memory && _occupied_ptr >= static_cast<char*>(_trusted_memory) + get_total_space(_trusted_memory)) {
        _occupied_ptr = nullptr;
    }
}

void* allocator_boundary_tags::boundary_iterator::get_ptr() const noexcept
{
    return _occupied_ptr;
}
