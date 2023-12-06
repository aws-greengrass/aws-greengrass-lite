//
// Created by Julicher, Joe on 12/5/23.
//

#pragma once

#include "topic_level_iterator.hpp"

class TopicFilter {
public:
    using const_iterator = TopicLevelIterator;

    explicit TopicFilter(std::string_view str) : _value{str} {
        validateFilter();
    }

    explicit TopicFilter(std::string &&str) : _value{std::move(str)} {
        validateFilter();
    }

    TopicFilter(const TopicFilter &) = default;
    TopicFilter(TopicFilter &&) noexcept = default;
    TopicFilter &operator=(const TopicFilter &) = default;
    TopicFilter &operator=(TopicFilter &&) noexcept = default;
    ~TopicFilter() noexcept = default;

    explicit operator const std::string &() const noexcept {
        return _value;
    }

    friend bool operator==(const TopicFilter &a, const TopicFilter &b) noexcept {
        return a._value == b._value;
    }

    [[nodiscard]] bool match(std::string_view topic) const {
        TopicLevelIterator topicIter{topic};
        bool hash = false;
        auto [filterTail, topicTail] = std::mismatch(
            begin(),
            end(),
            topicIter.begin(),
            topicIter.end(),
            [&hash](std::string_view filterLevel, std::string_view topicLevel) {
                if(filterLevel == "#") {
                    hash = true;
                    return true;
                }
                return (filterLevel == "+") || (filterLevel == topicLevel);
            });
        return hash || ((filterTail == end()) && (topicTail == topicIter.end()));
    }

    struct Hash {
        size_t operator()(const TopicFilter &filter) const noexcept {
            return std::hash<std::string>{}(filter._value);
        }
    };

    [[nodiscard]] const_iterator begin() const {
        return TopicLevelIterator(_value).begin();
    }

    [[nodiscard]] const_iterator end() const {
        return TopicLevelIterator(_value).end();
    }

    [[nodiscard]] const std::string &get() const noexcept {
        return _value;
    }

private:
    std::string _value;

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
