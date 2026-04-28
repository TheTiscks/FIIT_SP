#ifndef SYS_PROG_B_TREE_H
#define SYS_PROG_B_TREE_H
#include <iterator>
#include <utility>
#include <boost/container/static_vector.hpp>
#include <stack>
#include <pp_allocator.h>
#include <associative_container.h>
#include <not_implemented.h>
#include <initializer_list>
#include <optional>

template <typename tkey, typename tvalue, comparator<tkey> compare = std::less<tkey>, std::size_t t = 5>
class B_tree final : private compare
{
public:
    using tree_data_type = std::pair<tkey, tvalue>;
    using tree_data_type_const = std::pair<const tkey, tvalue>;
    using value_type = tree_data_type_const;

private:
    static constexpr const size_t minimum_keys_in_node = t - 1;
    static constexpr const size_t maximum_keys_in_node = 2 * t - 1;

    inline bool compare_keys(const tkey& lhs, const tkey& rhs) const;
    inline bool compare_pairs(const tree_data_type& lhs, const tree_data_type& rhs) const;

    struct btree_node
    {
        boost::container::static_vector<tree_data_type, maximum_keys_in_node + 1> _keys;
        boost::container::static_vector<btree_node*, maximum_keys_in_node + 2> _pointers;
        btree_node() noexcept : _keys(), _pointers() {}
    };

    pp_allocator<value_type> _allocator;
    btree_node* _root;
    size_t _size;

    pp_allocator<value_type> get_allocator() const noexcept;

    void swap(B_tree& other)
    {
        using std::swap;
        swap(static_cast<compare&>(*this), static_cast<compare&>(other));
        swap(_allocator, other._allocator);
        swap(_root, other._root);
        swap(_size, other._size);
    }

    btree_node* create_node()
    {
        size_t items_needed = (sizeof(btree_node) + sizeof(value_type) - 1) / sizeof(value_type);
        void* raw_memory = _allocator.allocate(items_needed);
        btree_node* node = reinterpret_cast<btree_node*>(raw_memory);
        new (node) btree_node();
        return node;
    }

    void destroy_node(btree_node* node)
    {
        if (node == nullptr) {
            return;
        }
        size_t items_needed = (sizeof(btree_node) + sizeof(value_type) - 1) / sizeof(value_type);
        node->~btree_node();
        _allocator.deallocate(reinterpret_cast<value_type*>(node), items_needed);
    }

    btree_node* copy_node(btree_node* src, btree_node* parent = nullptr)
    {
        if (!src) {
            return nullptr;
        }
        btree_node* dst = create_node();
        dst->_keys = src->_keys;
        for (auto* child : src->_pointers) {
            dst->_pointers.push_back(copy_node(child, dst));
        }
        return dst;
    }

    void clear_subtree(btree_node* node)
    {
        if (!node) {
            return;
        }
        for (auto* child : node->_pointers) {
            clear_subtree(child);
        }
        destroy_node(node);
    }

    size_t find_key_position(btree_node* node, const tkey& key) const
    {
        size_t pos = 0;
        while (pos < node->_keys.size() && compare_keys(node->_keys[pos].first, key)) {
            ++pos;
        }
        return pos;
    }

    bool remove_key(const tkey& key)
    {
        if (!_root) {
            return false;
        }
        bool removed = remove_from_node(_root, key);
        if (removed) {
            --_size;
        }
        if (_root && _root->_keys.empty() && !_root->_pointers.empty()) {
            btree_node* old_root = _root;
            _root = _root->_pointers[0];
            destroy_node(old_root);
        }
        return removed;
    }

    bool remove_from_node(btree_node* node, const tkey& key)
    {
        size_t pos = find_key_position(node, key);
        bool found = (pos < node->_keys.size() && node->_keys[pos].first == key);

        if (found && node->_pointers.empty()) {
            node->_keys.erase(node->_keys.begin() + pos);
            return true;
        }

        if (found && !node->_pointers.empty()) {
            btree_node* predecessor = node->_pointers[pos];
            while (!predecessor->_pointers.empty()) {
                predecessor = predecessor->_pointers.back();
            }
            tree_data_type pred_key = predecessor->_keys.back();
            remove_from_node(node->_pointers[pos], pred_key.first);
            node->_keys[pos] = pred_key;
            return true;
        }

        if (!found && node->_pointers.empty()) {
            return false;
        }

        btree_node* child = node->_pointers[pos];
        if (child->_keys.size() == minimum_keys_in_node) {
            if (pos > 0 && node->_pointers[pos - 1]->_keys.size() > minimum_keys_in_node) {
                btree_node* left = node->_pointers[pos - 1];
                child->_keys.insert(child->_keys.begin(), node->_keys[pos - 1]);
                node->_keys[pos - 1] = left->_keys.back();
                left->_keys.pop_back();
                if (!left->_pointers.empty()) {
                    child->_pointers.insert(child->_pointers.begin(), left->_pointers.back());
                    left->_pointers.pop_back();
                }
            } else if (pos + 1 < node->_pointers.size() && node->_pointers[pos + 1]->_keys.size() > minimum_keys_in_node) {
                btree_node* right = node->_pointers[pos + 1];
                child->_keys.push_back(node->_keys[pos]);
                node->_keys[pos] = right->_keys.front();
                right->_keys.erase(right->_keys.begin());
                if (!right->_pointers.empty()) {
                    child->_pointers.push_back(right->_pointers.front());
                    right->_pointers.erase(right->_pointers.begin());
                }
            } else if (pos > 0) {
                btree_node* left = node->_pointers[pos - 1];
                left->_keys.push_back(node->_keys[pos - 1]);
                left->_keys.insert(left->_keys.end(), child->_keys.begin(), child->_keys.end());
                left->_pointers.insert(left->_pointers.end(), child->_pointers.begin(), child->_pointers.end());
                node->_keys.erase(node->_keys.begin() + pos - 1);
                node->_pointers.erase(node->_pointers.begin() + pos);
                destroy_node(child);
                child = left;
            } else if (pos + 1 < node->_pointers.size()) {
                btree_node* right = node->_pointers[pos + 1];
                child->_keys.push_back(node->_keys[pos]);
                child->_keys.insert(child->_keys.end(), right->_keys.begin(), right->_keys.end());
                child->_pointers.insert(child->_pointers.end(), right->_pointers.begin(), right->_pointers.end());
                node->_keys.erase(node->_keys.begin() + pos);
                node->_pointers.erase(node->_pointers.begin() + pos + 1);
                destroy_node(right);
            }
        }

        return remove_from_node(child, key);
    }

    std::optional<std::pair<tree_data_type, btree_node*>> insert_impl(btree_node* node, const tree_data_type& data);
    std::optional<std::pair<tree_data_type, btree_node*>> insert_impl(btree_node* node, tree_data_type&& data);


public:
    explicit B_tree(const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>())
        : compare(cmp), _allocator(alloc), _root(nullptr), _size(0)
    {}

    explicit B_tree(pp_allocator<value_type> alloc, const compare& comp = compare())
        : compare(comp), _allocator(alloc), _root(nullptr), _size(0)
    {}

    template<input_iterator_for_pair<tkey, tvalue> iterator>
    explicit B_tree(iterator begin, iterator end, const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>())
        : compare(cmp), _allocator(alloc), _root(nullptr), _size(0)
    {
        for (auto it = begin; it != end; ++it) {
            insert(*it);
        }
    }

    B_tree(std::initializer_list<std::pair<tkey, tvalue>> data, const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>())
        : compare(cmp), _allocator(alloc), _root(nullptr), _size(0)
    {
        for (const auto& item : data) {
            insert(item);
        }
    }

    B_tree(const B_tree& other)
        : compare(other), _allocator(other._allocator), _root(nullptr), _size(0)
    {
        if (other._root) {
            _root = copy_node(other._root, nullptr);
            _size = other._size;
        }
    }

    B_tree(B_tree&& other) noexcept
        : compare(std::move(other)), _allocator(std::move(other._allocator)), _root(other._root), _size(other._size)
    {
        other._root = nullptr;
        other._size = 0;
    }

    B_tree& operator=(const B_tree& other)
    {
        if (this != &other) {
            B_tree temp(other);
            swap(temp);
        }
        return *this;
    }

    B_tree& operator=(B_tree&& other) noexcept
    {
        if (this != &other) {
            clear();
            compare::operator=(std::move(other));
            _allocator = std::move(other._allocator);
            _root = other._root;
            _size = other._size;
            other._root = nullptr;
            other._size = 0;
        }
        return *this;
    }

    ~B_tree() noexcept
    {
        clear();
    }

    class btree_iterator;
    class btree_reverse_iterator;
    class btree_const_iterator;
    class btree_const_reverse_iterator;

    class btree_iterator final
    {
        std::stack<std::pair<btree_node**, size_t>> _path;
        size_t _index;

    public:
        using value_type = tree_data_type_const;
        using reference = value_type&;
        using pointer = value_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = btree_iterator;

        friend class B_tree;
        friend class btree_reverse_iterator;
        friend class btree_const_iterator;
        friend class btree_const_reverse_iterator;

        reference operator*() const noexcept;
        pointer operator->() const noexcept;
        self& operator++();
        self operator++(int);
        self& operator--();
        self operator--(int);
        bool operator==(const self& other) const noexcept;
        bool operator!=(const self& other) const noexcept;
        size_t depth() const noexcept;
        size_t current_node_keys_count() const noexcept;
        bool is_terminate_node() const noexcept;
        size_t index() const noexcept;

        explicit btree_iterator(const std::stack<std::pair<btree_node**, size_t>>& path = std::stack<std::pair<btree_node**, size_t>>(), size_t index = 0);
    };

    class btree_const_iterator final
    {
        std::stack<std::pair<btree_node* const*, size_t>> _path;
        size_t _index;

    public:
        using value_type = tree_data_type_const;
        using reference = const value_type&;
        using pointer = const value_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = btree_const_iterator;

        friend class B_tree;
        friend class btree_reverse_iterator;
        friend class btree_iterator;
        friend class btree_const_reverse_iterator;

        btree_const_iterator(const btree_iterator& it) noexcept;
        reference operator*() const noexcept;
        pointer operator->() const noexcept;
        self& operator++();
        self operator++(int);
        self& operator--();
        self operator--(int);
        bool operator==(const self& other) const noexcept;
        bool operator!=(const self& other) const noexcept;
        size_t depth() const noexcept;
        size_t current_node_keys_count() const noexcept;
        bool is_terminate_node() const noexcept;
        size_t index() const noexcept;

        explicit btree_const_iterator(const std::stack<std::pair<btree_node* const*, size_t>>& path = std::stack<std::pair<btree_node* const*, size_t>>(), size_t index = 0);
    };

    class btree_reverse_iterator final
    {
        std::stack<std::pair<btree_node**, size_t>> _path;
        size_t _index;

    public:
        using value_type = tree_data_type_const;
        using reference = value_type&;
        using pointer = value_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = btree_reverse_iterator;

        friend class B_tree;
        friend class btree_iterator;
        friend class btree_const_iterator;
        friend class btree_const_reverse_iterator;

        btree_reverse_iterator(const btree_iterator& it);
        operator btree_iterator() const noexcept;
        reference operator*() const noexcept;
        pointer operator->() const noexcept;
        self& operator++();
        self operator++(int);
        self& operator--();
        self operator--(int);
        bool operator==(const self& other) const noexcept;
        bool operator!=(const self& other) const noexcept;
        size_t depth() const noexcept;
        size_t current_node_keys_count() const noexcept;
        bool is_terminate_node() const noexcept;
        size_t index() const noexcept;

        explicit btree_reverse_iterator(const std::stack<std::pair<btree_node**, size_t>>& path = std::stack<std::pair<btree_node**, size_t>>(), size_t index = 0);
    };

    class btree_const_reverse_iterator final
    {
        std::stack<std::pair<btree_node* const*, size_t>> _path;
        size_t _index;

    public:
        using value_type = tree_data_type_const;
        using reference = const value_type&;
        using pointer = const value_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = btree_const_reverse_iterator;

        friend class B_tree;
        friend class btree_reverse_iterator;
        friend class btree_const_iterator;
        friend class btree_iterator;

        btree_const_reverse_iterator(const btree_reverse_iterator& it) noexcept;
        operator btree_const_iterator() const noexcept;
        reference operator*() const noexcept;
        pointer operator->() const noexcept;
        self& operator++();
        self operator++(int);
        self& operator--();
        self operator--(int);
        bool operator==(const self& other) const noexcept;
        bool operator!=(const self& other) const noexcept;
        size_t depth() const noexcept;
        size_t current_node_keys_count() const noexcept;
        bool is_terminate_node() const noexcept;
        size_t index() const noexcept;

        explicit btree_const_reverse_iterator(const std::stack<std::pair<btree_node* const*, size_t>>& path = std::stack<std::pair<btree_node* const*, size_t>>(), size_t index = 0);
    };

    friend class btree_iterator;
    friend class btree_const_iterator;
    friend class btree_reverse_iterator;
    friend class btree_const_reverse_iterator;

    tvalue& at(const tkey&);
    const tvalue& at(const tkey&) const;
    tvalue& operator[](const tkey& key);
    tvalue& operator[](tkey&& key);

    btree_iterator begin();
    btree_iterator end();
    btree_const_iterator begin() const;
    btree_const_iterator end() const;
    btree_const_iterator cbegin() const;
    btree_const_iterator cend() const;
    btree_reverse_iterator rbegin();
    btree_reverse_iterator rend();
    btree_const_reverse_iterator rbegin() const;
    btree_const_reverse_iterator rend() const;
    btree_const_reverse_iterator crbegin() const;
    btree_const_reverse_iterator crend() const;

    size_t size() const noexcept;
    bool empty() const noexcept;
    btree_iterator find(const tkey& key);
    btree_const_iterator find(const tkey& key) const;
    btree_iterator lower_bound(const tkey& key);
    btree_const_iterator lower_bound(const tkey& key) const;
    btree_iterator upper_bound(const tkey& key)
    {
        auto it = lower_bound(key);
        if (it != end() && it->first == key) ++it;
        return it;
    }
    btree_const_iterator upper_bound(const tkey& key) const
    {
        auto it = lower_bound(key);
        if (it != end() && it->first == key) ++it;
        return it;
    }
    bool contains(const tkey& key) const;

    void clear() noexcept;
    std::pair<btree_iterator, bool> insert(const tree_data_type& data)
    {
        if (contains(data.first)) {
            return {find(data.first), false};
        }
        if (!_root) {
            _root = create_node();
            _root->_keys.push_back(data);
            ++_size;
            return {begin(), true};
        }
        auto split_res = insert_impl(_root, data);
        if (split_res) {
            auto& [up_key, right_node] = *split_res;
            btree_node* new_root = create_node();
            new_root->_keys.push_back(std::move(up_key));
            new_root->_pointers.push_back(_root);
            new_root->_pointers.push_back(right_node);
            _root = new_root;
        }
        return {find(data.first), true};
    }
    std::pair<btree_iterator, bool> insert(tree_data_type&& data)
    {
        tkey key = data.first;
        if (contains(key)) {
            return {find(key), false};
        }
        if (!_root) {
            _root = create_node();
            _root->_keys.push_back(std::move(data));
            ++_size;
            return {begin(), true};
        }
        auto split_res = insert_impl(_root, std::move(data));
        if (split_res) {
            auto& [up_key, right_node] = *split_res;
            btree_node* new_root = create_node();
            new_root->_keys.push_back(std::move(up_key));
            new_root->_pointers.push_back(_root);
            new_root->_pointers.push_back(right_node);
            _root = new_root;
        }
        return {find(key), true};
    }
    template<typename... Args>
    std::pair<btree_iterator, bool> emplace(Args&&... args);
    btree_iterator insert_or_assign(const tree_data_type& data);
    btree_iterator insert_or_assign(tree_data_type&& data);
    template<typename... Args>
    btree_iterator emplace_or_assign(Args&&... args);
    btree_iterator erase(btree_iterator pos);
    btree_iterator erase(btree_const_iterator pos);
    btree_iterator erase(btree_iterator beg, btree_iterator en);
    btree_iterator erase(btree_const_iterator beg, btree_const_iterator en);
    btree_iterator erase(const tkey& key);
};


template<std::input_iterator iterator,
    comparator<typename std::iterator_traits<iterator>::value_type::first_type> compare =
        std::less<typename std::iterator_traits<iterator>::value_type::first_type>,
    std::size_t t = 5,typename U>
B_tree(iterator begin, iterator end, const compare& cmp = compare(), pp_allocator<U> = pp_allocator<U>())
    -> B_tree<typename std::iterator_traits<iterator>::value_type::first_type,
              typename std::iterator_traits<iterator>::value_type::second_type,
              compare,
              t>;

template<
    typename tkey, typename tvalue,
    comparator<tkey> compare = std::less<tkey>, std::size_t t = 5, typename U
>
B_tree(std::initializer_list<std::pair<tkey, tvalue>> data, const compare& cmp = compare(),
       pp_allocator<U> = pp_allocator<U>())
    -> B_tree<tkey, tvalue, compare, t>;


template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::compare_pairs(const B_tree::tree_data_type& lhs,
                                                     const B_tree::tree_data_type& rhs) const
{
    return compare_keys(lhs.first, rhs.first);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::compare_keys(const tkey& lhs, const tkey& rhs) const
{
    return compare::operator()(lhs, rhs);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
pp_allocator<typename B_tree<tkey, tvalue, compare, t>::value_type>
B_tree<tkey, tvalue, compare, t>::get_allocator() const noexcept
{
    return _allocator;
}


template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>::btree_iterator::btree_iterator(
    const std::stack<std::pair<btree_node**, size_t>>& path, size_t index)
    : _path(path), _index(index)
{}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator::reference
B_tree<tkey, tvalue, compare, t>::btree_iterator::operator*() const noexcept
{
    auto& top = _path.top();
    return *reinterpret_cast<value_type*>(&(*top.first)->_keys[top.second]);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator::pointer
B_tree<tkey, tvalue, compare, t>::btree_iterator::operator->() const noexcept
{
    return &**this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator&
B_tree<tkey, tvalue, compare, t>::btree_iterator::operator++()
{
    if (_path.empty()) {
        return *this;
    }

    btree_node* current = *(_path.top().first);
    size_t current_idx = _path.top().second;

    if (!current->_pointers.empty()) {
        btree_node** next_ptr = &current->_pointers[current_idx + 1];
        btree_node* next_node = *next_ptr;
        _path.push({next_ptr, 0});

        while (!next_node->_pointers.empty()) {
            next_ptr = &next_node->_pointers[0];
            next_node = *next_ptr;
            _path.push({next_ptr, 0});
        }

        _index = 0;
        return *this;
    }

    if (current_idx + 1 < current->_keys.size()) {
        _path.top().second = current_idx + 1;
        _index = current_idx + 1;
        return *this;
    }

    while (!_path.empty()) {
        btree_node* finished_node = *(_path.top().first);
        _path.pop();

        if (_path.empty()) {
            break;
        }

        btree_node* parent = *(_path.top().first);
        size_t child_idx = 0;
        while (child_idx < parent->_pointers.size() && parent->_pointers[child_idx] != finished_node) {
            ++child_idx;
        }

        if (child_idx < parent->_keys.size()) {
            _path.top().second = child_idx;
            _index = child_idx;
            return *this;
        }
    }

    _index = 0;
    return *this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator
B_tree<tkey, tvalue, compare, t>::btree_iterator::operator++(int)
{
    btree_iterator tmp = *this;
    ++(*this);
    return tmp;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator&
B_tree<tkey, tvalue, compare, t>::btree_iterator::operator--()
{
    if (_path.empty()) {
        return *this;
    }

    btree_node* current = *(_path.top().first);
    size_t current_idx = _path.top().second;

    if (current_idx > 0) {
        --current_idx;
        if (!current->_pointers.empty()) {
            btree_node* next_node = current->_pointers[current_idx];
            _path.push({&current->_pointers[current_idx], 0});

            while (!next_node->_pointers.empty()) {
                next_node = next_node->_pointers.back();
                _path.push({&next_node->_pointers.back(), next_node->_pointers.size() - 1});
            }

            _index = next_node->_keys.size() - 1;
        } else {
            _index = current_idx;
        }
        _path.top().second = _index;
    } else {
        btree_node* finished_node = current;
        _path.pop();

        while (!_path.empty()) {
            btree_node* parent = *(_path.top().first);
            size_t child_idx = 0;
            while (child_idx < parent->_pointers.size() && parent->_pointers[child_idx] != finished_node) {
                ++child_idx;
            }

            if (child_idx > 0) {
                _path.top().second = child_idx - 1;
                _index = child_idx - 1;
                return *this;
            }

            finished_node = parent;
            _path.pop();
        }

        _index = 0;
    }

    return *this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator
B_tree<tkey, tvalue, compare, t>::btree_iterator::operator--(int)
{
    btree_iterator tmp = *this;
    --(*this);
    return tmp;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::btree_iterator::operator==(const self& other) const noexcept
{
    if (_path.size() != other._path.size()) {
        return false;
    }
    if (_path.empty() && other._path.empty()) {
        return true;
    }
    if (_path.empty() || other._path.empty()) {
        return false;
    }
    return (*_path.top().first == *other._path.top().first) && (_index == other._index);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::btree_iterator::operator!=(const self& other) const noexcept
{
    return !(*this == other);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t B_tree<tkey, tvalue, compare, t>::btree_iterator::depth() const noexcept
{
    return _path.empty() ? 0 : _path.size() - 1;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t B_tree<tkey, tvalue, compare, t>::btree_iterator::current_node_keys_count() const noexcept
{
    return _path.empty() ? 0 : (*_path.top().first)->_keys.size();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::btree_iterator::is_terminate_node() const noexcept
{
    return _path.empty() || (*_path.top().first)->_pointers.empty();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t B_tree<tkey, tvalue, compare, t>::btree_iterator::index() const noexcept
{
    return _index;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>::btree_const_iterator::btree_const_iterator(
    const std::stack<std::pair<btree_node* const*, size_t>>& path, size_t index)
    : _path(path), _index(index)
{}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>::btree_const_iterator::btree_const_iterator(const btree_iterator& it) noexcept
    : _index(it._index)
{
    std::stack<std::pair<btree_node* const*, size_t>> tmp;
    std::stack<std::pair<btree_node**, size_t>> copy = it._path;
    while (!copy.empty()) {
        tmp.push({copy.top().first, copy.top().second});
        copy.pop();
    }
    while (!tmp.empty()) {
        _path.push(tmp.top());
        tmp.pop();
    }
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_iterator::reference
B_tree<tkey, tvalue, compare, t>::btree_const_iterator::operator*() const noexcept
{
    auto& top = _path.top();
    return *reinterpret_cast<const value_type*>(&(*top.first)->_keys[top.second]);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_iterator::pointer
B_tree<tkey, tvalue, compare, t>::btree_const_iterator::operator->() const noexcept
{
    return &**this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_iterator&
B_tree<tkey, tvalue, compare, t>::btree_const_iterator::operator++()
{
    if (_path.empty()) {
        return *this;
    }

    btree_node* current = const_cast<btree_node*>(*_path.top().first);
    size_t current_idx = _path.top().second;

    if (!current->_pointers.empty()) {
        btree_node** next_ptr = &current->_pointers[current_idx + 1];
        btree_node* next_node = *next_ptr;
        _path.push({next_ptr, 0});

        while (!next_node->_pointers.empty()) {
            next_ptr = &next_node->_pointers[0];
            next_node = *next_ptr;
            _path.push({next_ptr, 0});
        }

        _index = 0;
        return *this;
    }

    if (current_idx + 1 < current->_keys.size()) {
        _path.top().second = current_idx + 1;
        _index = current_idx + 1;
        return *this;
    }

    while (!_path.empty()) {
        btree_node* finished_node = *(_path.top().first);
        _path.pop();

        if (_path.empty()) {
            break;
        }

        btree_node* parent = const_cast<btree_node*>(*_path.top().first);
        size_t child_idx = 0;
        while (child_idx < parent->_pointers.size() && parent->_pointers[child_idx] != finished_node) {
            ++child_idx;
        }

        if (child_idx < parent->_keys.size()) {
            _path.top().second = child_idx;
            _index = child_idx;
            return *this;
        }
    }

    _index = 0;
    return *this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_iterator
B_tree<tkey, tvalue, compare, t>::btree_const_iterator::operator++(int)
{
    btree_const_iterator tmp = *this;
    ++(*this);
    return tmp;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_iterator&
B_tree<tkey, tvalue, compare, t>::btree_const_iterator::operator--()
{
    if (_path.empty()) {
        return *this;
    }

    btree_node* current = const_cast<btree_node*>(*(_path.top().first));
    size_t current_idx = _path.top().second;

    if (current_idx > 0) {
        --current_idx;
        if (!current->_pointers.empty()) {
            btree_node* next_node = current->_pointers[current_idx];
            _path.push({&current->_pointers[current_idx], 0});

            while (!next_node->_pointers.empty()) {
                next_node = next_node->_pointers.back();
                _path.push({&next_node->_pointers.back(), next_node->_pointers.size() - 1});
            }

            _index = next_node->_keys.size() - 1;
        } else {
            _index = current_idx;
        }
        _path.top().second = _index;
    } else {
        btree_node* finished_node = current;
        _path.pop();

        while (!_path.empty()) {
            btree_node* parent = const_cast<btree_node*>(*_path.top().first);
            size_t child_idx = 0;
            while (child_idx < parent->_pointers.size() && parent->_pointers[child_idx] != finished_node) {
                ++child_idx;
            }

            if (child_idx > 0) {
                _path.top().second = child_idx - 1;
                _index = child_idx - 1;
                return *this;
            }

            finished_node = parent;
            _path.pop();
        }

        _index = 0;
    }

    return *this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_iterator
B_tree<tkey, tvalue, compare, t>::btree_const_iterator::operator--(int)
{
    btree_const_iterator tmp = *this;
    --(*this);
    return tmp;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::btree_const_iterator::operator==(const self& other) const noexcept
{
    if (_path.size() != other._path.size()) {
        return false;
    }
    if (_path.empty() && other._path.empty()) {
        return true;
    }
    if (_path.empty() || other._path.empty()) {
        return false;
    }
    return (*_path.top().first == *other._path.top().first) && (_index == other._index);
}


template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::btree_const_iterator::operator!=(const self& other) const noexcept
{
    return !(*this == other);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t B_tree<tkey, tvalue, compare, t>::btree_const_iterator::depth() const noexcept
{
    return _path.empty() ? 0 : _path.size() - 1;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t B_tree<tkey, tvalue, compare, t>::btree_const_iterator::current_node_keys_count() const noexcept
{
    return _path.empty() ? 0 : (*_path.top().first)->_keys.size();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::btree_const_iterator::is_terminate_node() const noexcept
{
    return _path.empty() || (*_path.top().first)->_pointers.empty();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t B_tree<tkey, tvalue, compare, t>::btree_const_iterator::index() const noexcept
{
    return _index;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::btree_reverse_iterator(
    const std::stack<std::pair<btree_node**, size_t>>& path, size_t index)
    : _path(path), _index(index)
{}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::btree_reverse_iterator(const btree_iterator& it)
    : _path(it._path), _index(it._index)
{}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::operator
B_tree<tkey, tvalue, compare, t>::btree_iterator() const noexcept
{
    return btree_iterator(_path, _index);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::reference
B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::operator*() const noexcept
{
    auto& top = _path.top();
    return reinterpret_cast<std::pair<const tkey, tvalue>&>((*top.first)->_keys[top.second]);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::pointer
B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::operator->() const noexcept
{
    return &**this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator&
B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::operator++()
{
    if (_index > 0) {
        --_index;
    } else {
        _path.pop();
        while (!_path.empty() && _index == 0) {
            _index = _path.top().second;
            _path.pop();
        }
        if (!_path.empty()) {
            btree_node* current = (*_path.top().first);
            while (!current->_pointers.empty()) {
                _path.push({&current->_pointers.back(), current->_pointers.size() - 1});
                current = current->_pointers.back();
            }
            _index = current->_keys.size() - 1;
        }
    }
    return *this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator
B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::operator++(int)
{
    btree_reverse_iterator tmp = *this;
    ++(*this);
    return tmp;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator&
B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::operator--()
{
    if (_index + 1 < (*_path.top().first)->_keys.size()) {
        ++_index;
    } else {
        auto& top = _path.top();
        btree_node* current = (*top.first);
        if (_index + 1 < current->_pointers.size()) {
            _path.push({&current->_pointers[_index + 1], 0});
            current = current->_pointers[_index + 1];
            while (!current->_pointers.empty()) {
                _path.push({&current->_pointers[0], 0});
                current = current->_pointers[0];
            }
            _index = 0;
        } else {
            _path.pop();
            if (!_path.empty()) {
                _index = _path.top().second + 1;
            }
        }
    }
    return *this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator
B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::operator--(int)
{
    btree_reverse_iterator tmp = *this;
    --(*this);
    return tmp;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::operator==(const self& other) const noexcept
{
    if (_path.size() != other._path.size()) {
        return false;
    }
    if (_path.empty() && other._path.empty()) {
        return true;
    }
    if (_path.empty() || other._path.empty()) {
        return false;
    }
    return (*_path.top().first == *other._path.top().first) && (_index == other._index);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::operator!=(const self& other) const noexcept
{
    return !(*this == other);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::depth() const noexcept
{
    return _path.empty() ? 0 : _path.size() - 1;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::current_node_keys_count() const noexcept
{
    return _path.empty() ? 0 : (*_path.top().first)->_keys.size();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::is_terminate_node() const noexcept
{
    return _path.empty() || (*_path.top().first)->_pointers.empty();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator::index() const noexcept
{
    return _index;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::btree_const_reverse_iterator(
    const std::stack<std::pair<btree_node* const*, size_t>>& path, size_t index)
    : _path(path), _index(index)
{}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::btree_const_reverse_iterator(
    const btree_reverse_iterator& it) noexcept
{
    _index = it._index;
    std::stack<std::pair<btree_node* const*, size_t>> tmp;
    std::stack<std::pair<btree_node**, size_t>> copy = it._path;
    while (!copy.empty()) {
        tmp.push({copy.top().first, copy.top().second});
        copy.pop();
    }
    while (!tmp.empty()) {
        _path.push(tmp.top());
        tmp.pop();
    }
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::operator
B_tree<tkey, tvalue, compare, t>::btree_const_iterator() const noexcept
{
    return btree_const_iterator(_path, _index);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::reference
B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::operator*() const noexcept
{
    auto& top = _path.top();
    return reinterpret_cast<const std::pair<const tkey, tvalue>&>((*top.first)->_keys[top.second]);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::pointer
B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::operator->() const noexcept
{
    return &**this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator&
B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::operator++()
{
    if (_index > 0) {
        --_index;
    } else {
        _path.pop();
        while (!_path.empty() && _index == 0) {
            _index = _path.top().second;
            _path.pop();
        }
        if (!_path.empty()) {
            btree_node* current = const_cast<btree_node*>(*_path.top().first);
            while (!current->_pointers.empty()) {
                _path.push({&current->_pointers.back(), current->_pointers.size() - 1});
                current = current->_pointers.back();
            }
            _index = current->_keys.size() - 1;
        }
    }
    return *this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator
B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::operator++(int)
{
    btree_const_reverse_iterator tmp = *this;
    ++(*this);
    return tmp;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator&
B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::operator--()
{
    if (_index + 1 < (*_path.top().first)->_keys.size()) {
        ++_index;
    } else {
        auto& top = _path.top();
        btree_node* current = const_cast<btree_node*>(*top.first);
        if (_index + 1 < current->_pointers.size()) {
            _path.push({&current->_pointers[_index + 1], 0});
            current = current->_pointers[_index + 1];
            while (!current->_pointers.empty()) {
                _path.push({&current->_pointers[0], 0});
                current = current->_pointers[0];
            }
            _index = 0;
        } else {
            _path.pop();
            if (!_path.empty()) {
                _index = _path.top().second + 1;
            }
        }
    }
    return *this;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator
B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::operator--(int)
{
    btree_const_reverse_iterator tmp = *this;
    --(*this);
    return tmp;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::operator==(const self& other) const noexcept
{
    if (_path.size() != other._path.size()) {
        return false;
    }
    if (_path.empty() && other._path.empty()) {
        return true;
    }
    if (_path.empty() || other._path.empty()) {
        return false;
    }
    return (*_path.top().first == *other._path.top().first) && (_index == other._index);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::operator!=(const self& other) const noexcept
{
    return !(*this == other);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::depth() const noexcept
{
    return _path.empty() ? 0 : _path.size() - 1;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::current_node_keys_count() const noexcept
{
    return _path.empty() ? 0 : (*_path.top().first)->_keys.size();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::is_terminate_node() const noexcept
{
    return _path.empty() || (*_path.top().first)->_pointers.empty();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator::index() const noexcept
{
    return _index;
}


template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
tvalue& B_tree<tkey, tvalue, compare, t>::at(const tkey& key)
{
    auto it = find(key);
    if (it == end()) {
        throw std::out_of_range("Key not found");
    }
    return const_cast<tvalue&>(it->second);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
const tvalue& B_tree<tkey, tvalue, compare, t>::at(const tkey& key) const
{
    auto it = find(key);
    if (it == end()) {
        throw std::out_of_range("Key not found");
    }
    return it->second;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
tvalue& B_tree<tkey, tvalue, compare, t>::operator[](const tkey& key)
{
    auto it = find(key);
    if (it == end()) {
        auto [new_it, inserted] = insert({key, tvalue{}});
        return const_cast<tvalue&>(new_it->second);
    }
    return const_cast<tvalue&>(it->second);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
tvalue& B_tree<tkey, tvalue, compare, t>::operator[](tkey&& key)
{
    return (*this)[key];
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator
B_tree<tkey, tvalue, compare, t>::begin()
{
    if (!_root) {
        return end();
    }
    std::stack<std::pair<btree_node**, size_t>> path;
    path.push({&_root, 0});
    btree_node* current = _root;
    while (!current->_pointers.empty()) {
        path.push({&current->_pointers[0], 0});
        current = current->_pointers[0];
    }
    return btree_iterator(path, 0);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator
B_tree<tkey, tvalue, compare, t>::end()
{
    return btree_iterator();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_iterator
B_tree<tkey, tvalue, compare, t>::begin() const
{
    return const_cast<B_tree*>(this)->begin();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_iterator
B_tree<tkey, tvalue, compare, t>::end() const
{
    return const_cast<B_tree*>(this)->end();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_iterator
B_tree<tkey, tvalue, compare, t>::cbegin() const
{
    return begin();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_iterator
B_tree<tkey, tvalue, compare, t>::cend() const
{
    return end();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator
B_tree<tkey, tvalue, compare, t>::rbegin()
{
    return btree_reverse_iterator(end());
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_reverse_iterator
B_tree<tkey, tvalue, compare, t>::rend()
{
    return btree_reverse_iterator(begin());
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator
B_tree<tkey, tvalue, compare, t>::rbegin() const
{
    return btree_const_reverse_iterator(end());
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator
B_tree<tkey, tvalue, compare, t>::rend() const
{
    return btree_const_reverse_iterator(begin());
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator
B_tree<tkey, tvalue, compare, t>::crbegin() const
{
    return rbegin();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_reverse_iterator
B_tree<tkey, tvalue, compare, t>::crend() const
{
    return rend();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
size_t B_tree<tkey, tvalue, compare, t>::size() const noexcept
{
    return _size;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::empty() const noexcept
{
    return _size == 0;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator
B_tree<tkey, tvalue, compare, t>::find(const tkey& key)
{
    if (!_root) {
        return end();
    }
    std::stack<std::pair<btree_node**, size_t>> path;
    path.push({&_root, 0});
    btree_node* current = _root;
    while (current) {
        size_t pos = find_key_position(current, key);
        path.top().second = pos;
        if (pos < current->_keys.size() && current->_keys[pos].first == key) {
            return btree_iterator(path, pos);
        }
        if (current->_pointers.empty()) {
            break;
        }
        btree_node** next_node_ptr = &current->_pointers[pos];
        current = *next_node_ptr;
        path.push({next_node_ptr, 0});
    }
    return end();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_iterator
B_tree<tkey, tvalue, compare, t>::find(const tkey& key) const
{
    return const_cast<B_tree*>(this)->find(key);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator
B_tree<tkey, tvalue, compare, t>::lower_bound(const tkey& key)
{
    if (!_root) {
        return end();
    }
    auto it = begin();
    auto e = end();
    while (it != e && compare_keys(it->first, key)) {
        ++it;
    }
    return it;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_const_iterator
B_tree<tkey, tvalue, compare, t>::lower_bound(const tkey& key) const
{
    return const_cast<B_tree*>(this)->lower_bound(key);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
bool B_tree<tkey, tvalue, compare, t>::contains(const tkey& key) const
{
    return find(key) != end();
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
void B_tree<tkey, tvalue, compare, t>::clear() noexcept
{
    clear_subtree(_root);
    _root = nullptr;
    _size = 0;
}


template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
template<typename... Args>
std::pair<typename B_tree<tkey, tvalue, compare, t>::btree_iterator, bool>
B_tree<tkey, tvalue, compare, t>::emplace(Args&&... args)
{
    return insert(tree_data_type(std::forward<Args>(args)...));
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator
B_tree<tkey, tvalue, compare, t>::insert_or_assign(const tree_data_type& data)
{
    auto it = find(data.first);
    if (it != end()) {
        const_cast<tvalue&>(it->second) = data.second;
        return it;
    }
    return insert(data).first;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator
B_tree<tkey, tvalue, compare, t>::insert_or_assign(tree_data_type&& data)
{
    return insert_or_assign(data);
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
template<typename... Args>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator
B_tree<tkey, tvalue, compare, t>::emplace_or_assign(Args&&... args)
{
    return insert_or_assign(tree_data_type(std::forward<Args>(args)...));
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator
B_tree<tkey, tvalue, compare, t>::erase(btree_iterator pos)
{
    if (pos == end()) {
        return end();
    }
    auto next = pos;
    ++next;
    remove_key(pos->first);
    return next;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator
B_tree<tkey, tvalue, compare, t>::erase(btree_const_iterator pos)
{
    return erase(btree_iterator(pos));
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator
B_tree<tkey, tvalue, compare, t>::erase(btree_iterator beg, btree_iterator en)
{
    while (beg != en) {
        beg = erase(beg);
    }
    return beg;
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator
B_tree<tkey, tvalue, compare, t>::erase(btree_const_iterator beg, btree_const_iterator en)
{
    return erase(btree_iterator(beg), btree_iterator(en));
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
typename B_tree<tkey, tvalue, compare, t>::btree_iterator
B_tree<tkey, tvalue, compare, t>::erase(const tkey& key)
{
    auto it = find(key);
    if (it == end()) {
        return end();
    }
    auto next = it;
    ++next;
    remove_key(key);
    return next;
}


template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
std::optional<std::pair<typename B_tree<tkey, tvalue, compare, t>::tree_data_type, typename B_tree<tkey, tvalue, compare, t>::btree_node*>>
B_tree<tkey, tvalue, compare, t>::insert_impl(btree_node* node, const tree_data_type& data)
{
    if (node->_pointers.empty()) {
        size_t pos = find_key_position(node, data.first);
        node->_keys.insert(node->_keys.begin() + pos, data);
        ++_size;

        if (node->_keys.size() > maximum_keys_in_node) {
            btree_node* new_node = create_node();
            size_t mid = t;
            auto up_key = std::move(node->_keys[mid]);
            new_node->_keys.assign(
                std::make_move_iterator(node->_keys.begin() + mid + 1),
                std::make_move_iterator(node->_keys.end()));
            node->_keys.erase(node->_keys.begin() + mid, node->_keys.end());
            return std::make_pair(std::move(up_key), new_node);
        }
        return std::nullopt;
    } else {
        size_t pos = find_key_position(node, data.first);
        btree_node* child = node->_pointers[pos];
        auto split_res = insert_impl(child, data);
        if (split_res) {
            auto& [up_key, right_node] = *split_res;
            node->_keys.insert(node->_keys.begin() + pos, std::move(up_key));
            node->_pointers.insert(node->_pointers.begin() + pos + 1, right_node);
            if (node->_keys.size() > maximum_keys_in_node) {
                btree_node* new_node2 = create_node();
                size_t mid = t;
                auto up_key2 = std::move(node->_keys[mid]);
                new_node2->_keys.assign(
                    std::make_move_iterator(node->_keys.begin() + mid + 1),
                    std::make_move_iterator(node->_keys.end()));
                new_node2->_pointers.assign(
                    std::make_move_iterator(node->_pointers.begin() + mid + 1),
                    std::make_move_iterator(node->_pointers.end()));
                node->_keys.erase(node->_keys.begin() + mid, node->_keys.end());
                node->_pointers.erase(node->_pointers.begin() + mid + 1, node->_pointers.end());
                return std::make_pair(std::move(up_key2), new_node2);
            }
        }
        return std::nullopt;
    }
}

template<typename tkey, typename tvalue, comparator<tkey> compare, std::size_t t>
std::optional<std::pair<typename B_tree<tkey, tvalue, compare, t>::tree_data_type, typename B_tree<tkey, tvalue, compare, t>::btree_node*>>
B_tree<tkey, tvalue, compare, t>::insert_impl(btree_node* node, tree_data_type&& data)
{
    if (node->_pointers.empty()) {
        size_t pos = find_key_position(node, data.first);
        node->_keys.insert(node->_keys.begin() + pos, std::move(data));
        ++_size;
        if (node->_keys.size() > maximum_keys_in_node) {
            btree_node* new_node = create_node();
            size_t mid = t;
            auto up_key = std::move(node->_keys[mid]);
            new_node->_keys.assign(
                std::make_move_iterator(node->_keys.begin() + mid + 1),
                std::make_move_iterator(node->_keys.end()));
            node->_keys.erase(node->_keys.begin() + mid, node->_keys.end());
            return std::make_pair(std::move(up_key), new_node);
        }
        return std::nullopt;
    } else {
        size_t pos = find_key_position(node, data.first);
        btree_node* child = node->_pointers[pos];
        auto split_res = insert_impl(child, std::move(data));
        if (split_res) {
            auto& [up_key, right_node] = *split_res;
            node->_keys.insert(node->_keys.begin() + pos, std::move(up_key));
            node->_pointers.insert(node->_pointers.begin() + pos + 1, right_node);
            if (node->_keys.size() > maximum_keys_in_node) {
                btree_node* new_node2 = create_node();
                size_t mid = t;
                auto up_key2 = std::move(node->_keys[mid]);
                new_node2->_keys.assign(
                    std::make_move_iterator(node->_keys.begin() + mid + 1),
                    std::make_move_iterator(node->_keys.end()));
                new_node2->_pointers.assign(
                    std::make_move_iterator(node->_pointers.begin() + mid + 1),
                    std::make_move_iterator(node->_pointers.end()));
                node->_keys.erase(node->_keys.begin() + mid, node->_keys.end());
                node->_pointers.erase(node->_pointers.begin() + mid + 1, node->_pointers.end());
                return std::make_pair(std::move(up_key2), new_node2);
            }
        }
        return std::nullopt;
    }
}

#endif