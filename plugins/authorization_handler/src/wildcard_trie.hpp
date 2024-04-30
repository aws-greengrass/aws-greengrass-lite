#pragma once

#include "authorization_handler.hpp"

#include <memory>
#include <sstream>
#include <string>
#include <utility>

enum class ResourceLookupPolicy { STANDARD, MQTT_STYLE };

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

class WildcardTrie {
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
    void add(std::string subject) {
        if(!subject.compare(GLOBAL_WILDCARD)) {
            if(auto it = _children.find(GLOBAL_WILDCARD); it != _children.end()) {
                it->second->_matchAll = true;
                it->second->_isTerminal = true;
                it->second->_isMQTTWildcard = true;
            }
            return;
        }
        if(!subject.compare(MQTT_MULTILEVEL_WILDCARD)) {
            if(auto it = _children.find(MQTT_MULTILEVEL_WILDCARD); it != _children.end()) {
                it->second->_matchAll = true;
                it->second->_isTerminal = true;
                it->second->_isMQTTWildcard = true;
            }
            return;
        }
        if(!subject.compare(MQTT_SINGLELEVEL_WILDCARD)) {
            if(auto it = _children.find(MQTT_SINGLELEVEL_WILDCARD); it != _children.end()) {
                it->second->_isTerminal = true;
                it->second->_isMQTTWildcard = true;
            }
            return;
        }
        if(subject.rfind(MQTT_SINGLELEVEL_SEPARATOR, 0) == 0) {
            if(auto it = _children.find(MQTT_SINGLELEVEL_WILDCARD); it != _children.end()) {
                it->second->_isMQTTWildcard = true;
                it->second->add(subject.substr(1), true);
            }
            return;
        }

        add(subject, true);
    };
    bool matches(std::string str, ResourceLookupPolicy lookupPolicy) {
        return lookupPolicy == ResourceLookupPolicy::MQTT_STYLE ? matchesMQTT(str)
                                                                : matchesStandard(str);
    };
    bool matchesMQTT(std::string str) {
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
            if((value->_isWildcard && !key.compare(GLOBAL_WILDCARD))
               || (value->_isMQTTWildcard
                   && (!key.compare(MQTT_SINGLELEVEL_WILDCARD)
                       || !key.compare(MQTT_MULTILEVEL_WILDCARD)))) {
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
                if(!str.compare(terminalKey)) {
                    return true;
                }
                if(endsWith(str, terminalKey)) {
                    key = terminalKey;
                }
            }

            int keyLength = key.size();
            int strLength = static_cast<int>(str.size());
            // If I'm a wildcard, then I need to maybe chomp many characters to match my
            // children
            if(_isWildcard) {
                int foundChildIndex = str.find(key);
                while(foundChildIndex >= 0 && foundChildIndex < strLength) {
                    matchingChildren.insert(
                        {str.substr(foundChildIndex + keyLength), std::move(value)});
                    foundChildIndex = str.find(key, foundChildIndex + 1);
                }
            }
            // If I'm a MQTT wildcard (specifically +, as # is already covered),
            // then I need to maybe chomp many characters to match my children
            if(_isMQTTWildcard) {
                int foundChildIndex = str.find(key);
                // Matched characters inside + should not contain a "/"
                while(foundChildIndex >= 0 && foundChildIndex < strLength
                      && (str.substr(0, foundChildIndex).find(MQTT_LEVEL_SEPARATOR)
                          == std::string::npos)) {
                    matchingChildren.insert(
                        {str.substr(foundChildIndex + keyLength), std::move(value)});
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
    bool matchesStandard(std::string str) {
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
            if(value._isWildcard && !key.compare(GLOBAL_WILDCARD)) {
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
                int foundChildIndex = str.find(key);
                int keyLength = key.size();
                while(foundChildIndex >= 0) {
                    matchingChildren.insert(
                        {str.substr(foundChildIndex + keyLength), std::move(value)});
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
    bool _isTerminal;
    bool _isTerminalLevel;
    bool _isWildcard;
    bool _isMQTTWildcard;
    bool _matchAll;
    std::unordered_map<std::string, std::shared_ptr<WildcardTrie>> _children;
    std::shared_ptr<WildcardTrie> add(std::string subject, bool isTerminal) {
        if(subject.empty()) {
            _isTerminal = isTerminal;
            return std::make_shared<WildcardTrie>(*this);
        }
        int subjectLength = subject.size();
        auto current = std::make_shared<WildcardTrie>(*this);
        ;
        std::stringstream ss;
        for(int i = 0; i < subjectLength; i++) {
            char currentChar = subject[i];
            if(currentChar == wildcardChar) {
                current = current->add(ss.str(), false);
                if(auto it = current->_children.find(GLOBAL_WILDCARD);
                   it != current->_children.end()) {
                    current = std::move(it->second);
                    current->_isWildcard = true;
                    if(i == subjectLength - 1) {
                        current->_isTerminal = isTerminal;
                        return current;
                    }
                    return current->add(subject.substr(i + 1), true);
                }
                return nullptr;
            }
            if(currentChar == multiLevelWildcardChar) {
                auto terminalLevel = current->add(ss.str(), false);
                if(auto it = terminalLevel->_children.find(MQTT_MULTILEVEL_WILDCARD);
                   it != terminalLevel->_children.end()) {
                    current = std::move(it->second);
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
                return nullptr;
            }
            if(currentChar == singleLevelWildcardChar) {
                current = current->add(ss.str(), false);
                if(auto it = current->_children.find(MQTT_SINGLELEVEL_WILDCARD);
                   it != current->_children.end()) {
                    current = std::move(it->second);
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
                return nullptr;
            }
            if(currentChar == escapeChar) {
                char actualChar = getActualChar(subject.substr(i));
                if(actualChar != nullChar) {
                    ss << actualChar;
                    i = i + 3;
                    continue;
                }
            }
            ss << currentChar;
        }
        // Handle non-wildcard value
        if(auto it = current->_children.find(ss.str()); it != current->_children.end()) {
            current = std::move(it->second);
            current->_isTerminal = isTerminal;
            return current;
        }
        return nullptr;
    };
    bool endsWith(const std::string &str, const std::string &suffix) {
        return str.size() >= suffix.size()
               && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
};
