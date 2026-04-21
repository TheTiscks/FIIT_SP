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
    throw not_implemented("allocator_boundary_tags::allocator_boundary_tags(allocator_boundary_tags &&) noexcept", "your code should be here...");
}

allocator_boundary_tags &allocator_boundary_tags::operator=(
    allocator_boundary_tags &&other) noexcept
{
    throw not_implemented("allocator_boundary_tags &allocator_boundary_tags::operator=(allocator_boundary_tags &&) noexcept", "your code should be here...");
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
    throw not_implemented("[[nodiscard]] void *allocator_boundary_tags::do_allocate_sm(size_t)", "your code should be here...");
}

void allocator_boundary_tags::do_deallocate_sm(
    void *at)
{
    throw not_implemented("void allocator_boundary_tags::do_deallocate_sm(void *)", "your code should be here...");
}

inline void allocator_boundary_tags::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
    throw not_implemented("inline void allocator_boundary_tags::set_fit_mode(allocator_with_fit_mode::fit_mode)", "your code should be here...");
}


std::vector<allocator_test_utils::block_info> allocator_boundary_tags::get_blocks_info() const
{
    throw not_implemented("std::vector<allocator_test_utils::block_info> allocator_boundary_tags::get_blocks_info() const", "your code should be here...");
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::begin() const noexcept
{
    throw not_implemented("allocator_boundary_tags::boundary_iterator allocator_boundary_tags::begin() const noexcept", "your code should be here...");
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::end() const noexcept
{
    throw not_implemented("allocator_boundary_tags::boundary_iterator allocator_boundary_tags::end() const noexcept", "your code should be here...");
}

std::vector<allocator_test_utils::block_info> allocator_boundary_tags::get_blocks_info_inner() const
{
    throw not_implemented("std::vector<allocator_test_utils::block_info> allocator_boundary_tags::get_blocks_info_inner() const", "your code should be here...");
}

allocator_boundary_tags::allocator_boundary_tags(const allocator_boundary_tags &other)
{
    throw not_implemented("allocator_boundary_tags::allocator_boundary_tags(const allocator_boundary_tags &other)", "your code should be here...");
}

allocator_boundary_tags &allocator_boundary_tags::operator=(const allocator_boundary_tags &other)
{
    throw not_implemented("allocator_boundary_tags &allocator_boundary_tags::operator=(const allocator_boundary_tags &other)", "your code should be here...");
}

bool allocator_boundary_tags::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    throw not_implemented("bool allocator_boundary_tags::do_is_equal(const std::pmr::memory_resource &other) const noexcept", "your code should be here...");
}

bool allocator_boundary_tags::boundary_iterator::operator==(
        const allocator_boundary_tags::boundary_iterator &other) const noexcept
{
    throw not_implemented("bool allocator_boundary_tags::boundary_iterator::operator==(const allocator_boundary_tags::boundary_iterator &) const noexcept", "your code should be here...");
}

bool allocator_boundary_tags::boundary_iterator::operator!=(
        const allocator_boundary_tags::boundary_iterator & other) const noexcept
{
    throw not_implemented("bool allocator_boundary_tags::boundary_iterator::operator!=(const allocator_boundary_tags::boundary_iterator &) const noexcept", "your code should be here...");
}

allocator_boundary_tags::boundary_iterator &allocator_boundary_tags::boundary_iterator::operator++() & noexcept
{
    throw not_implemented("allocator_boundary_tags::boundary_iterator &allocator_boundary_tags::boundary_iterator::operator++() & noexcept", "your code should be here...");
}

allocator_boundary_tags::boundary_iterator &allocator_boundary_tags::boundary_iterator::operator--() & noexcept
{
    throw not_implemented("allocator_boundary_tags::boundary_iterator &allocator_boundary_tags::boundary_iterator::operator--() & noexcept", "your code should be here...");
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::boundary_iterator::operator++(int n)
{
    throw not_implemented("allocator_boundary_tags::boundary_iterator allocator_boundary_tags::boundary_iterator::operator++(int n)", "your code should be here...");
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::boundary_iterator::operator--(int n)
{
    throw not_implemented("allocator_boundary_tags::boundary_iterator allocator_boundary_tags::boundary_iterator::operator--(int n)", "your code should be here...");
}

size_t allocator_boundary_tags::boundary_iterator::size() const noexcept
{
    throw not_implemented("size_t allocator_boundary_tags::boundary_iterator::size() const noexcept", "your code should be here...");
}

bool allocator_boundary_tags::boundary_iterator::occupied() const noexcept
{
    throw not_implemented("bool allocator_boundary_tags::boundary_iterator::occupied() const noexcept", "your code should be here...");
}

void* allocator_boundary_tags::boundary_iterator::operator*() const noexcept
{
    throw not_implemented("void* allocator_boundary_tags::boundary_iterator::operator*() const noexcept", "your code should be here...");
}

allocator_boundary_tags::boundary_iterator::boundary_iterator()
{
    throw not_implemented("allocator_boundary_tags::boundary_iterator::boundary_iterator()", "your code should be here...");
}

allocator_boundary_tags::boundary_iterator::boundary_iterator(void *trusted)
{
    throw not_implemented("allocator_boundary_tags::boundary_iterator::boundary_iterator(void *)", "your code should be here...");
}

void *allocator_boundary_tags::boundary_iterator::get_ptr() const noexcept
{
    throw not_implemented("void *allocator_boundary_tags::boundary_iterator::get_ptr() const noexcept", "your code should be here...");
}
