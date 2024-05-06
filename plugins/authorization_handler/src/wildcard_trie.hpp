#pragma once

#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include <string_util.hpp>

enum class ResourceLookupPolicy { STANDARD, MQTT_STYLE, UNKNOWN };

/**
 * A Wildcard trie node which contains properties to identify the Node and a map of all it's
 * children.
 * - isTerminal: If the node is a terminal node while adding a resource. It might not
 * necessarily be a leaf node as we are adding multiple resources having same prefix but
 * terminating on different points.
 * - isTerminalLevel: If the node is the last level before a valid use "#" wildcard (eg:
 * "abc/123/#", 123/ would be the terminalLevel).
 * - isWildcard: If current Node is a valid glob wildcard (*)
 * - isMQTTWildcard: If current Node is a valid MQTT wildcard (#, +)
 * - matchAll: if current node should match everything. Could be MQTTWildcard or a wildcard and
 * will always be a terminal Node.
 */

class WildcardTrie : public std::enable_shared_from_this<WildcardTrie> {
public:
    static constexpr auto GLOBAL_WILDCARD = "*";
    static constexpr auto MQTT_MULTILEVEL_WILDCARD = "#";
    static constexpr auto MQTT_SINGLELEVEL_WILDCARD = "+";
    static constexpr auto MQTT_LEVEL_SEPARATOR = "/";
    static constexpr auto MQTT_SINGLELEVEL_SEPARATOR = "+/";
    static constexpr auto nullChar = '\0';
    static constexpr auto escapeChar = '$';
    static constexpr auto singleCharWildcard = '?';
    static constexpr auto wildcardChar = '*';
    static constexpr auto multiLevelWildcardChar = '#';
    static constexpr auto singleLevelWildcardChar = '+';
    static constexpr auto levelSeparatorChar = '/';

    WildcardTrie() = default;

    static char getActualChar(std::string str) {
        if(str.size() < 4) {
            return nullChar;
        }
        // Match the escape format ${c}
        if(str[0] == escapeChar && str[1] == '{' && str[3] == '}') {
            return str[2];
        }
        return nullChar;
    };

    /**
     * Add allowed resources for a particular operation.
     * - A new node is created for every occurrence of a wildcard (*, #, +).
     * - Only nodes with valid usage of wildcards are marked with isWildcard or isMQTTWildcard.
     * - Any other characters are grouped together to form a node.
     * - Just a '*' or '#' creates a Node setting matchAll to true and would match all resources
     *
     * @param subject resource pattern
     */
    void add(const std::string &subject) {
        if(subject.empty()) {
            return;
        }
        if(subject == GLOBAL_WILDCARD) {
            _children[GLOBAL_WILDCARD] = std::make_shared<WildcardTrie>();
            auto initial = _children[GLOBAL_WILDCARD];
            initial->_matchAll = true;
            initial->_isTerminal = true;
            initial->_isWildcard = true;
            return;
        }
        if(subject == MQTT_MULTILEVEL_WILDCARD) {
            _children[MQTT_MULTILEVEL_WILDCARD] = std::make_shared<WildcardTrie>();
            auto initial = _children[MQTT_MULTILEVEL_WILDCARD];
            initial->_matchAll = true;
            initial->_isTerminal = true;
            initial->_isMQTTWildcard = true;
            return;
        }
        if(subject == MQTT_SINGLELEVEL_WILDCARD) {
            _children[MQTT_SINGLELEVEL_WILDCARD] = std::make_shared<WildcardTrie>();
            auto initial = _children[MQTT_SINGLELEVEL_WILDCARD];
            initial->_isTerminal = true;
            initial->_isMQTTWildcard = true;
            return;
        }
        if(subject.rfind(MQTT_SINGLELEVEL_SEPARATOR, 0) == 0) {
            _children[MQTT_SINGLELEVEL_WILDCARD] = std::make_shared<WildcardTrie>();
            auto initial = _children[MQTT_SINGLELEVEL_WILDCARD];
            initial->_isMQTTWildcard = true;
            initial->add(subject.substr(1), true);
            return;
        }

        add(subject, true);
    };
    bool matches(const std::string &str, ResourceLookupPolicy lookupPolicy) {
        return lookupPolicy == ResourceLookupPolicy::MQTT_STYLE ? matchesMQTT(str)
                                                                : matchesStandard(str);
    };
    bool matchesMQTT(const std::string &str) { // NOLINT(*-no-recursion)
        if((_isWildcard && _isTerminal) || (_isTerminal && str.empty())) {
            return true;
        }
        if(_isMQTTWildcard) {
            if(_matchAll
               || (_isTerminal && (str.find(MQTT_LEVEL_SEPARATOR) == std::string::npos))) {
                return true;
            }
        }

        bool hasMatch = false;
        std::unordered_map<std::string, std::shared_ptr<WildcardTrie>> matchingChildren;
        for(auto &childIt : _children) {
            // Succeed fast
            if(hasMatch) {
                return true;
            }
            auto key = childIt.first;
            auto value = std::move(childIt.second);

            // Process *, # and + wildcards (only process MQTT wildcards that have valid usages)
            if((value->_isWildcard && key == GLOBAL_WILDCARD)
               || (value->_isMQTTWildcard
                   && (key == MQTT_SINGLELEVEL_WILDCARD || key == MQTT_MULTILEVEL_WILDCARD))) {
                hasMatch = value->matchesMQTT(str);
                continue;
            }
            if(str.rfind(key, 0) == 0) {
                hasMatch = value->matchesMQTT(str.substr(key.size()));
                // Succeed fast
                if(hasMatch) {
                    return true;
                }
            }
            // Check if it's terminalLevel to allow matching of string without "/" in the end
            //      "abc/#" should match "abc".
            //      "abc/*xy/#" should match "abc/12xy"
            std::string terminalKey = key.substr(0, key.size() - 1);
            if(value->_isTerminalLevel) {
                if(str == terminalKey) {
                    return true;
                }
                if(util::endsWith(str, terminalKey)) {
                    key = terminalKey;
                }
            }

            size_t keyLength = key.length();
            size_t strLength = str.length();
            // If I'm a wildcard, then I need to maybe chomp many characters to match my
            // children
            if(_isWildcard) {
                size_t foundChildIndex = str.find(key);
                while(foundChildIndex != std::string::npos && foundChildIndex < strLength) {
                    matchingChildren.insert({str.substr(foundChildIndex + keyLength), value});
                    foundChildIndex = str.find(key, foundChildIndex + 1);
                }
            }
            // If I'm a MQTT wildcard (specifically +, as # is already covered),
            // then I need to maybe chomp many characters to match my children
            if(_isMQTTWildcard) {
                size_t foundChildIndex = str.find(key);
                // Matched characters inside + should not contain a "/"
                while(foundChildIndex != std::string::npos && foundChildIndex < strLength
                      && (str.substr(0, foundChildIndex).find(MQTT_LEVEL_SEPARATOR)
                          == std::string::npos)) {
                    matchingChildren.insert({str.substr(foundChildIndex + keyLength), value});
                    foundChildIndex = str.find(key, foundChildIndex + 1);
                }
            }

            // Succeed fast
            if(hasMatch) {
                return true;
            }
            if((_isWildcard || _isMQTTWildcard) && !matchingChildren.empty()) {
                bool anyMatch = false;
                for(auto &e : matchingChildren) {
                    if(e.second->matchesMQTT(e.first)) {
                        anyMatch = true;
                        break;
                    }
                }
                if(anyMatch) {
                    return true;
                }
            }
            return false;
        }
        return false;
    };
    bool matchesStandard(const std::string &str) { // NOLINT(*-no-recursion)
        if((_isWildcard && _isTerminal) || (_isTerminal && str.empty())) {
            return true;
        }

        bool hasMatch = false;
        std::unordered_map<std::string, WildcardTrie> matchingChildren;
        for(auto &childIt : _children) {
            // Succeed fast
            if(hasMatch) {
                return true;
            }
            auto key = childIt.first;
            auto value = *childIt.second;

            // Process * wildcards
            if(value._isWildcard && key == GLOBAL_WILDCARD) {
                hasMatch = value.matchesStandard(str);
                continue;
            }

            // Match normal characters
            if(str.rfind(key, 0) == 0) {
                hasMatch = value.matchesStandard(str.substr(key.size()));
                // Succeed fast
                if(hasMatch) {
                    return true;
                }
            }

            // If I'm a wildcard, then I need to maybe chomp many characters to match my
            // children
            if(_isWildcard) {
                size_t foundChildIndex = str.find(key);
                size_t keyLength = key.length();
                while(foundChildIndex != std::string::npos) {
                    matchingChildren.insert({str.substr(foundChildIndex + keyLength), value});
                    foundChildIndex = str.find(key, foundChildIndex + 1);
                }
            }
        }
        // Succeed fast
        if(hasMatch) {
            return true;
        }
        if(_isWildcard && !matchingChildren.empty()) {
            bool anyMatch = false;
            for(auto e : matchingChildren) {
                if(e.second.matchesStandard(e.first)) {
                    anyMatch = true;
                    break;
                }
            }
            if(anyMatch) {
                return true;
            }
        }
        return false;
    };

private:
    bool _isTerminal = false;
    bool _isTerminalLevel = false;
    bool _isWildcard = false;
    bool _isMQTTWildcard = false;
    bool _matchAll = false;
    std::unordered_map<std::string, std::shared_ptr<WildcardTrie>> _children;
    std::shared_ptr<WildcardTrie> add( // NOLINT(*-no-recursion)
        std::string subject,
        bool isTerminal) {
        if(subject.empty()) {
            this->_isTerminal |= isTerminal;
            return shared_from_this();
        }
        size_t subjectLength = subject.length();
        auto current = shared_from_this();
        std::string ss;
        for(size_t i = 0; i < subjectLength; i++) {
            char currentChar = subject[i];
            if(currentChar == wildcardChar) {
                current = current->add(ss, false);
                if(!current->_children.count(GLOBAL_WILDCARD)) {
                    current->_children[GLOBAL_WILDCARD] = std::make_shared<WildcardTrie>();
                }
                current = current->_children[GLOBAL_WILDCARD];
                current->_isWildcard = true;
                if(i == subjectLength - 1) {
                    current->_isTerminal = isTerminal;
                    return current;
                }
                return current->add(subject.substr(i + 1), true);
            }
            if(currentChar == multiLevelWildcardChar) {
                auto terminalLevel = current->add(ss, false);
                if(!current->_children.count(MQTT_MULTILEVEL_WILDCARD)) {
                    current->_children[MQTT_MULTILEVEL_WILDCARD] = std::make_shared<WildcardTrie>();
                }
                current = terminalLevel->_children[MQTT_MULTILEVEL_WILDCARD];
                if(i == subjectLength - 1) {
                    current->_isTerminal = true;
                    if(i > 0 && subject[i - 1] == levelSeparatorChar) {
                        current->_isMQTTWildcard = true;
                        current->_matchAll = true;
                        terminalLevel->_isTerminalLevel = true;
                    }
                    return current;
                }
                return current->add(subject.substr(i + 1), true);
            }
            if(currentChar == singleLevelWildcardChar) {
                current = current->add(ss, false);
                if(!current->_children.count(MQTT_SINGLELEVEL_WILDCARD)) {
                    current->_children[MQTT_SINGLELEVEL_WILDCARD] =
                        std::make_shared<WildcardTrie>();
                }
                current = current->_children[MQTT_SINGLELEVEL_WILDCARD];
                if(i == subjectLength - 1) {
                    current->_isTerminal = true;
                    if(i > 0 && subject[i - 1] == levelSeparatorChar) {
                        current->_isMQTTWildcard = true;
                    }
                    return current;
                }
                if(i > 0 && subject[i - 1] == levelSeparatorChar
                   && subject[i + 1] == levelSeparatorChar) {
                    current->_isMQTTWildcard = true;
                }
                return current->add(subject.substr(i + 1), true);
            }
            if(currentChar == escapeChar) {
                char actualChar = getActualChar(subject.substr(i));
                if(actualChar != nullChar) {
                    ss.push_back(actualChar);
                    i = i + 3;
                    continue;
                }
            }
            ss.push_back(currentChar);
        }
        // Handle non-wildcard value
        if(!current->_children.count(ss)) {
            current->_children[ss] = std::make_shared<WildcardTrie>();
        }
        current = current->_children[ss];
        current->_isTerminal |= isTerminal;
        return current;
    };
};
