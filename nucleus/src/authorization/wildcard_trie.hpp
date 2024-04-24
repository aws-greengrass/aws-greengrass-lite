#pragma once

#include <string>
// TODO: Remove - From Java Description

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

namespace authorization {

    // TODO: Implement the wildcard trie, as described above.

    class WildcardTrie {
    protected:
        static constexpr auto GLOBAL_WILDCARD = "*";
        static constexpr auto MQTT_MULTILEVEL_WILDCARD = "#";
        static constexpr auto MQTT_SINGLELEVEL_WILDCARD = "+";
        static constexpr auto MQTT_LEVEL_SEPARATOR = "/";

    public:
        explicit WildcardTrie(){};
    };
} // namespace authorization
