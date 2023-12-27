#pragma once

#include "topic_level_iterator.hpp"
#include <stdexcept>
#include <string>

template<class Alloc = std::allocator<char>>
class TopicFilter {
public:
    using value_type = std::basic_string<char, std::char_traits<char>, Alloc>;
    using const_iterator = TopicLevelIterator;

    explicit TopicFilter(std::string_view str) : _value{str} {
        validateFilter();
    }

    explicit TopicFilter(value_type &&str) : _value{std::move(str)} {
        validateFilter();
    }

    TopicFilter(const TopicFilter &) = default;
    TopicFilter(TopicFilter &&) noexcept = default;
    TopicFilter &operator=(const TopicFilter &) = default;
    TopicFilter &operator=(TopicFilter &&) noexcept = default;
    ~TopicFilter() noexcept = default;

    explicit operator const value_type &() const noexcept {
        return _value;
    }

    template<class OtherAlloc>
    friend bool operator==(const TopicFilter &a, const TopicFilter<OtherAlloc> &b) noexcept {
        return a._value == b._value;
    }

    [[nodiscard]] bool match(std::string_view topic) const {
        TopicLevelIterator topicIter{topic};
        bool wildcard = false;
        auto [filterTail, topicTail] = std::mismatch(
            begin(),
            end(),
            topicIter.begin(),
            topicIter.end(),
            [&wildcard](std::string_view filterLevel, std::string_view topicLevel) {
                if(filterLevel == "#") {
                    wildcard = true;
                    return false;
                }
                return (filterLevel == "+") || (filterLevel == topicLevel);
            });
        return wildcard || ((filterTail == end()) && (topicTail == topicIter.end()));
    }

    struct Hash {
        size_t operator()(const TopicFilter &filter) const noexcept {
            return std::hash<std::string_view>{}(filter._value);
        }
    };

    [[nodiscard]] const_iterator begin() const {
        return TopicLevelIterator(_value).begin();
    }

    [[nodiscard]] const_iterator end() const {
        return TopicLevelIterator(_value).end();
    }

    [[nodiscard]] const value_type &get() const noexcept {
        return _value;
    }

private:
    value_type _value;

    void validateFilter() const {
        if(_value.empty()) {
            throw std::invalid_argument("Invalid topic filter");
        }
        bool last = false;
        for(auto level : *this) {
            if(last
               || ((level != "#") && (level != "+")
                   && (level.find_first_of("#+") != std::string_view::npos))) {
                throw std::invalid_argument("Invalid topic filter");
            }
            if(level == "#") {
                last = true;
            }
        }
    }
};
