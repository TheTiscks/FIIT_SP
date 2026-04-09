#include <not_implemented.h>
#include "../include/allocator_sorted_list.h"


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
        static_cast<char*>(trusted) + sizeof(std::pmr::memory_resource*) + sizeof(allocator_with_fit_mode::fit_mode) + sizeof(size_t));
}

static inline void*& get_free_head(void* trusted) noexcept {
    return *reinterpret_cast<void**>(
        static_cast<char*>(trusted) + sizeof(std::pmr::memory_resource*) + sizeof(allocator_with_fit_mode::fit_mode) + sizeof(size_t) + sizeof(std::mutex));
}

static inline void* block_next(void* block) noexcept {
    return *reinterpret_cast<void**>(block);
}

static inline void set_block_next(void* block, void* next) noexcept {
    *reinterpret_cast<void**>(block) = next;
}

static inline size_t& block_size(void* block) noexcept {
    return *reinterpret_cast<size_t*>(static_cast<char*>(block) + sizeof(void*));
}

static inline void* block_start(void* block) noexcept {
    return static_cast<char*>(block) + sizeof(void*) + sizeof(size_t);
}

static inline void* block_from_user(void* user_ptr) noexcept {
    return static_cast<char*>(user_ptr) - sizeof(void*) - sizeof(size_t);
}

static inline bool is_address_in_range(void* trusted, void* addr) noexcept {
    char* start = static_cast<char*>(trusted);
    char* end = start + get_total_space(trusted);
    char* p = static_cast<char*>(addr);
    return p >= start && p < end;
}




allocator_sorted_list::~allocator_sorted_list()
{
    if (!_trusted_memory) {
        return;
    }
    get_mutex(_trusted_memory).~mutex();
    std::pmr::memory_resource* parent = get_parent(_trusted_memory);
    parent->deallocate(_trusted_memory, get_total_space(_trusted_memory), alignof(std::max_align_t));
    _trusted_memory = nullptr;
}

allocator_sorted_list::allocator_sorted_list(allocator_sorted_list &&other) noexcept : _trusted_memory(other._trusted_memory)
{
    other._trusted_memory = nullptr;
}

allocator_sorted_list &allocator_sorted_list::operator=(
    allocator_sorted_list &&other) noexcept
{
    if (this != &other) {
        this->~allocator_sorted_list();
        _trusted_memory = other._trusted_memory;
        other._trusted_memory = nullptr;
    }
    return *this;
}

allocator_sorted_list::allocator_sorted_list(
        size_t space_size,
        std::pmr::memory_resource *parent_allocator,
        allocator_with_fit_mode::fit_mode allocate_fit_mode)
{
    std::pmr::memory_resource* parent = parent_allocator ? parent_allocator : std::pmr::get_default_resource();
    _trusted_memory = parent->allocate(space_size, alignof(std::max_align_t));
    if (!_trusted_memory) {
        throw std::bad_alloc();
    }
    get_parent(_trusted_memory) = parent;
    get_fit_mode(_trusted_memory) = allocate_fit_mode;
    get_total_space(_trusted_memory) = space_size;
    new (&get_mutex(_trusted_memory)) std::mutex();
    get_free_head(_trusted_memory) = nullptr;
    void* first_block = static_cast<char*>(_trusted_memory) + allocator_metadata_size; // своб блок после метаданных
    size_t first_block_usable = space_size - allocator_metadata_size - block_metadata_size;
    if (first_block_usable > 0) {
        set_block_next(first_block, nullptr);
        block_size(first_block) = first_block_usable;
        get_free_head(_trusted_memory) = first_block;
    }
}

[[nodiscard]] void *allocator_sorted_list::do_allocate_sm(
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
    if (mode == fit_mode::first_fit) {
        while (curr) {
            if (block_size(curr) >= size) {
                best = curr;
                best_prev = prev;
                break;
            }
            prev = curr;
            curr = block_next(curr);
        }
    } else if (mode == fit_mode::the_best_fit) {
        size_t best_diff = ~size_t(0);
        while (curr) {
            size_t sz = block_size(curr);
            if (sz >= size && sz - size < best_diff) {
                best_diff = sz - size;
                best = curr;
                best_prev = prev;
            }
            prev = curr;
            curr = block_next(curr);
        }
    } else if (mode == fit_mode::the_worst_fit) {
        size_t best_sz = 0;
        while (curr) {
            size_t sz = block_size(curr);
            if (sz >= size && sz > best_sz) {
                best_sz = sz;
                best = curr;
                best_prev = prev;
            }
            prev = curr;
            curr = block_next(curr);
        }
    }
    if (!best) {
        throw std::bad_alloc();
    }
    size_t block_sz = block_size(best);
    if (best_prev) { // удаляем best из свободных
        set_block_next(best_prev, block_next(best));
    } else {
        get_free_head(_trusted_memory) = block_next(best);
    }
    size_t remaining = block_sz - size; // можно делить
    if (remaining >= block_metadata_size + 1) {  // мин полезный размер байт
        void* new_free = static_cast<char*>(block_start(best)) + size;
        block_size(new_free) = remaining - block_metadata_size;
        set_block_next(new_free, get_free_head(_trusted_memory));
        get_free_head(_trusted_memory) = new_free;
        block_size(best) = size;
    }
    return block_start(best);
}

allocator_sorted_list::allocator_sorted_list(const allocator_sorted_list &other)
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
        set_block_next(dst_block, nullptr);
        if (prev_dst) {
            set_block_next(prev_dst, dst_block);
        } else {
            get_free_head(_trusted_memory) = dst_block;
        }
        prev_dst = dst_block;
        src = block_next(src);
    }
}

allocator_sorted_list &allocator_sorted_list::operator=(const allocator_sorted_list &other)
{
    if (this == &other) {
        return *this;
    }
    this->~allocator_sorted_list();
    new (this) allocator_sorted_list(other);
    return *this;
}

bool allocator_sorted_list::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    return this == &other;
}

void allocator_sorted_list::do_deallocate_sm(
    void *at)
{
    if (!at) {
        return;
    }
    std::lock_guard<std::mutex> lock(get_mutex(_trusted_memory));
    void* block = block_from_user(at);
    if (!is_address_in_range(_trusted_memory, block)) {
        return;  // не наш блок
    }
    size_t sz = block_size(block);
    void* prev = nullptr; // блок в список свободных
    void* curr = get_free_head(_trusted_memory);
    while (curr && curr < block) {
        prev = curr;
        curr = block_next(curr);
    }
    set_block_next(block, curr);
    if (prev) {
        set_block_next(prev, block);
    } else {
        get_free_head(_trusted_memory) = block;
    }
    if (prev && static_cast<char*>(prev) + block_metadata_size + block_size(prev) == block) {// склейка с пред свободным
        block_size(prev) += block_metadata_size + block_size(block);
        set_block_next(prev, block_next(block));
        block = prev;
    }
    void* next = block_next(block); // склейка со след свободным
    if (next && static_cast<char*>(block) + block_metadata_size + block_size(block) == next) {
        block_size(block) += block_metadata_size + block_size(next);
        set_block_next(block, block_next(next));
    }
}

inline void allocator_sorted_list::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
    std::lock_guard<std::mutex> lock(get_mutex(_trusted_memory));
    get_fit_mode(_trusted_memory) = mode;
}

std::vector<allocator_test_utils::block_info> allocator_sorted_list::get_blocks_info() const noexcept
{
    return get_blocks_info_inner();
}


std::vector<allocator_test_utils::block_info> allocator_sorted_list::get_blocks_info_inner() const
{
    if (!_trusted_memory) {
        return {};
    }
    std::lock_guard<std::mutex> lock(get_mutex(_trusted_memory));
    std::vector<allocator_test_utils::block_info> result;
    char* ptr = static_cast<char*>(_trusted_memory) + allocator_metadata_size;
    char* end = static_cast<char*>(_trusted_memory) + get_total_space(_trusted_memory);
    while (ptr < end) {
        size_t sz = block_size(ptr);
        bool is_free = false;
        void* free_ptr = get_free_head(_trusted_memory);
        while (free_ptr) {
            if (free_ptr == ptr) {
                is_free = true;
                break;
            }
            free_ptr = block_next(free_ptr);
        }
        result.push_back({sz, !is_free});   // ← исправлено
        ptr += block_metadata_size + sz;
    }
    return result;
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_begin() const noexcept
{
    return sorted_free_iterator(get_free_head(_trusted_memory));
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_end() const noexcept
{
    return sorted_free_iterator(nullptr);
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::begin() const noexcept
{
    return sorted_iterator(_trusted_memory);
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::end() const noexcept
{
    return sorted_iterator(nullptr);
}


bool allocator_sorted_list::sorted_free_iterator::operator==(
        const allocator_sorted_list::sorted_free_iterator & other) const noexcept
{
    return _free_ptr == other._free_ptr;
}

bool allocator_sorted_list::sorted_free_iterator::operator!=(const sorted_free_iterator& other) const noexcept
{
    return !(*this == other);
}

allocator_sorted_list::sorted_free_iterator& allocator_sorted_list::sorted_free_iterator::operator++() & noexcept
{
    if (_free_ptr) _free_ptr = block_next(_free_ptr);
    return *this;
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::sorted_free_iterator::operator++(int n)
{
    sorted_free_iterator tmp = *this;
    ++(*this);
    return tmp;
}

size_t allocator_sorted_list::sorted_free_iterator::size() const noexcept
{
    return _free_ptr ? block_size(_free_ptr) : 0;
}

void* allocator_sorted_list::sorted_free_iterator::operator*() const noexcept
{
    return _free_ptr ? block_start(_free_ptr) : nullptr;
}

allocator_sorted_list::sorted_free_iterator::sorted_free_iterator() : _free_ptr(nullptr)
{
}

allocator_sorted_list::sorted_free_iterator::sorted_free_iterator(void* trusted) : _free_ptr(trusted)
{
}

bool allocator_sorted_list::sorted_iterator::operator==(const sorted_iterator& other) const noexcept
{
    return _current_ptr == other._current_ptr;
}

bool allocator_sorted_list::sorted_iterator::operator!=(const sorted_iterator& other) const noexcept
{
    return !(*this == other);
}

allocator_sorted_list::sorted_iterator& allocator_sorted_list::sorted_iterator::operator++() & noexcept
{
    if (!_current_ptr) {
        return *this;
    }
    size_t sz = block_size(_current_ptr);
    _current_ptr = static_cast<char*>(_current_ptr) + block_metadata_size + sz;
    if (_trusted_memory && _current_ptr >= static_cast<char*>(_trusted_memory) + get_total_space(_trusted_memory)) {
        _current_ptr = nullptr;
    }
    return *this;
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::sorted_iterator::operator++(int n)
{
    sorted_iterator tmp = *this;
    ++(*this);
    return tmp;
}

size_t allocator_sorted_list::sorted_iterator::size() const noexcept
{
    return _current_ptr ? block_size(_current_ptr) : 0;
}

void* allocator_sorted_list::sorted_iterator::operator*() const noexcept
{
    return _current_ptr ? block_start(_current_ptr) : nullptr;
}

allocator_sorted_list::sorted_iterator::sorted_iterator() : _free_ptr(nullptr), _current_ptr(nullptr), _trusted_memory(nullptr)
{
}

allocator_sorted_list::sorted_iterator::sorted_iterator(void* trusted) : _free_ptr(trusted), _trusted_memory(trusted)
{
    if (trusted){
        _current_ptr = static_cast<char*>(trusted) + allocator_metadata_size;
    } else {
        _current_ptr = nullptr;
    }
}

bool allocator_sorted_list::sorted_iterator::occupied() const noexcept
{
    if (!_current_ptr || !_trusted_memory) {
        return false;
    }
    void* free_ptr = get_free_head(_trusted_memory);
    while (free_ptr) {
        if (free_ptr == _current_ptr) {
            return false;
        }
        free_ptr = block_next(free_ptr);
    }
    return true;
}
