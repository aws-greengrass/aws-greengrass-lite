#pragma once

#include <stdexcept>
#include <string_view>

// TODO: Move to util namespace

struct TopicLevelIterator {
    using value_type = std::string_view;
    using difference_type = std::ptrdiff_t;
    using reference = const value_type &;
    using pointer = const value_type *;
    using iterator_category = std::input_iterator_tag;

    explicit TopicLevelIterator(std::string_view topic) noexcept
        : _str(topic), _index(0), _current(_str.substr(_index, _str.find('/', _index))) {
    }

    friend bool operator==(const TopicLevelIterator &a, const TopicLevelIterator &b) noexcept {
        return (a._index == b._index) && (a._str.data() == b._str.data());
    }

    friend bool operator!=(const TopicLevelIterator &a, const TopicLevelIterator &b) noexcept {
        return (a._index != b._index) || (a._str.data() != b._str.data());
    }

    reference operator*() const {
        if(_index == value_type::npos) {
            throw std::out_of_range{"Using depleted TopicLevelIterator."};
        }
        return _current;
    }

    pointer operator->() const {
        if(_index == value_type::npos) {
            throw std::out_of_range{"Using depleted TopicLevelIterator."};
        }
        return &_current;
    }

    TopicLevelIterator &operator++() {
        if(_index == value_type::npos) {
            throw std::out_of_range{"Using depleted TopicLevelIterator."};
        }
        _index += _current.length() + 1;
        if(_index > _str.length()) {
            _index = value_type::npos;
        } else {
            size_t nextIndex = _str.find('/', _index);
            if(nextIndex == value_type::npos) {
                nextIndex = _str.length();
            }
            _current = _str.substr(_index, nextIndex - _index);
        }
        return *this;
    }

    // NOLINTNEXTLINE(readability-const-return-type) Conflicting lints.
    const TopicLevelIterator operator++(int) {
        TopicLevelIterator tmp = *this;
        ++*this;
        return tmp;
    }

    [[nodiscard]] TopicLevelIterator begin() const {
        return *this;
    }
    [[nodiscard]] TopicLevelIterator end() const {
        return TopicLevelIterator{this->_str, value_type::npos};
    }

private:
    explicit TopicLevelIterator(std::string_view topic, size_t index) : _str(topic), _index(index) {
    }

    std::string_view _str;
    size_t _index;
    value_type _current;
};
