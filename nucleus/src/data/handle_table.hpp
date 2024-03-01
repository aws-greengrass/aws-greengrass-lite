#pragma once
#include "safe_handle.hpp"
#include "tracked_object.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <util.hpp>

namespace data {

    namespace handleImpl {
        inline static const uint32_t INVALID_INDEX = std::numeric_limits<uint32_t>::max();
        inline static const uint32_t HANDLE_GEN_OFFSET = 24; // shift factor to get to gen bits
        inline static const uint32_t HANDLE_GEN_INC = 1 << HANDLE_GEN_OFFSET;
        inline static const uint32_t HANDLE_INDEX_MASK = (HANDLE_GEN_INC - 1);
        inline static const uint32_t MAX_HANDLE_CAPACITY = HANDLE_INDEX_MASK; // max capacity
        inline static const uint32_t INITIAL_HANDLE_CAPACITY = 0x100; // first capacity
        inline static const uint32_t INCREMENT_MAX = 0x20000; // don't double past this point
        inline static const uint32_t MIN_FREE = 25; // percent
        inline static const uint32_t PERCENT = 100; // percent

        struct LinkEntry {
            uint32_t next{INVALID_INDEX}; // Next index in linked list
            uint32_t prev{INVALID_INDEX}; // Prev index in linked list
        };

        struct EntryBase : public LinkEntry {
            uint32_t check{INVALID_INDEX}; // Used to do a handle check
        };

        /**
         * IndexList provides a 24-bit table of handles (anything that derives from EntryBase). The
         * table will grow under pressure but does not shrink. Handles consist of an 8-bit "color"
         * followed by 24-bit index. Each time an entry is re-used in the table, the color counter
         * is incremented. When adding handles to the table, the algorithm tries to keep 25% of
         * handles unused (which means table is always at least 25% bigger than it needs to be) to
         * minimize handle re-use. Percentage can be adjusted by constants above. When handles are
         * re-used, they are re-used in FIFO order (but each re-use applies a different color, so
         * the effective re-use of exactly the same handle ID is much longer). See the unit tests
         * for exercising and testing this template as it's important for the rest of the handle
         * logic to be correct.
         *
         * Another aspect of each entry is the linked list. Given the table is compact array, then
         * pointers can change, so the linked list uses indexes. This actually ends up simpler than
         * introducing a new layer of indirection through an additional pointer, and allows the
         * linked list to be used both for free-tracking, and also for tracking all nodes associated
         * with a root.
         */
        template<typename T>
        class IndexList {
            LinkEntry _free;
            uint32_t _freeCount = 0;
            std::vector<T> _entries;

        public:
            /**
             * Map index to pointer with strong Index checking. Note, indexes are relative to start
             * of table, and not the same as a handle ID. Caller is responsible for mutex. Const
             * version.
             * @param index Index with color for validation
             * @return pointer to entry in table, or nullptr if invalid.
             */
            [[nodiscard]] const T *lookup(uint32_t index) const noexcept {
                static_assert(std::is_base_of_v<EntryBase, T>);
                uint32_t realIndex = index & HANDLE_INDEX_MASK;
                if(realIndex >= _entries.size()) {
                    return nullptr;
                }
                const T &entry = _entries.at(realIndex);
                if(entry.check != index) {
                    return nullptr;
                }
                return &entry;
            }

            /**
             * Map index to pointer with strong Index checking. Note, indexes are relative to start
             * of table, and not the same as a handle ID. Caller is responsible for mutex. Non-const
             * version. (TODO: how to combine with const version?)
             * @param index Index with color for validation
             * @return pointer to entry in table, or nullptr if invalid.
             */
            [[nodiscard]] T *lookup(uint32_t index) noexcept {
                static_assert(std::is_base_of_v<EntryBase, T>);
                uint32_t realIndex = index & HANDLE_INDEX_MASK;
                if(realIndex >= _entries.size()) {
                    return nullptr;
                }
                T &entry = _entries.at(realIndex);
                if(entry.check != index) {
                    return nullptr;
                }
                return &entry;
            }

            /**
             * Boolean check if index is valid or not
             * @param index to check
             * @return true if valid
             */
            [[nodiscard]] bool check(uint32_t index) const {
                return lookup(index) != nullptr;
            }

            /**
             * Lazy value retrieval from table ignoring color bits. Relies on underlying vector
             * for validation. Use when index is expected to be valid.
             *
             * @param index Index (Lower 24-bits used)
             * @return pointer to entry
             */
            [[nodiscard]] T *at(uint32_t index) noexcept {
                return &_entries.at(index & HANDLE_INDEX_MASK);
            }

            /**
             * Estimates percentage of table that is free, required to be O(1).
             * @param cap Known capacity of vector (assumed to be previously obtained)
             * @return integer percentage free
             */
            [[nodiscard]] uint32_t getFreePercent(uint32_t cap) const noexcept {
                if(_freeCount == 0) {
                    return 0;
                }
                if(cap == 0) {
                    return 0;
                }
                return _freeCount * PERCENT / cap;
            }

            /**
             * Returns either 0 (don't grow) or number of elements to grow table by,
             * using current size and percentage free as inputs. Algorithm tries to balance between
             * not reusing elements frequently and avoiding increasing table size.
             *
             * @param cap Known capacity of vector (assumed to be previously obtained)
             * @return number of elements to increase.
             */
            [[nodiscard]] uint32_t getIncrementSize(uint32_t cap) const noexcept {
                uint32_t sz = _entries.size();
                if(sz < cap) {
                    return 0; // always use remaining capacity before resizing
                }
                if(cap >= MAX_HANDLE_CAPACITY) {
                    return 0;
                }
                if constexpr(MIN_FREE == 0) {
                    // Minimal mode - don't allocate unless required
                    if(_freeCount > 0) {
                        return 0;
                    }
                } else {
                    // Pressure based mode - don't allocate if enough free
                    auto pc = getFreePercent(cap);
                    if(pc >= MIN_FREE) {
                        return 0;
                    }
                }
                if(MAX_HANDLE_CAPACITY - cap <= INCREMENT_MAX) {
                    return MAX_HANDLE_CAPACITY - cap;
                }
                if(cap >= INCREMENT_MAX) {
                    return MAX_HANDLE_CAPACITY;
                }
                if(cap == 0) {
                    return INITIAL_HANDLE_CAPACITY;
                }
                return cap;
            }

            /**
             * Consider a circular linked list where one node of the list is the control node,
             * represented by the index INVALID_INDEX, and all other nodes are in the vector
             * table. This simplifies the algorithms that work on linked lists.
             *
             * @param ctrl Control node (reference)
             * @param index Index of node to retrieve
             * @return Node by index
             */
            LinkEntry *linkAt(LinkEntry &ctrl, uint32_t index) noexcept {
                static_assert(std::is_base_of_v<LinkEntry, T>);
                if(index == INVALID_INDEX) {
                    return &ctrl;
                } else {
                    return at(index);
                }
            }

            /**
             * Circular linked list semantics to unlink node at given index.
             *
             * @param ctrl Control node (prev = last, next = first)
             * @param index Index to unlink
             */
            void unlink(LinkEntry &ctrl, uint32_t index) noexcept {
                auto pAt = linkAt(ctrl, index);
                auto pPrev = linkAt(ctrl, pAt->prev);
                auto pNext = linkAt(ctrl, pAt->next);
                pPrev->next = pAt->next;
                pNext->prev = pAt->prev;
                index = index & HANDLE_INDEX_MASK;
                pAt->next = pAt->prev = index;
            }

            /**
             * Circular linked list semantics to add node to head/first
             *
             * @param ctrl Control node (prev = last, next = first)
             * @param index Index to insert to first
             */
            void insertFirst(LinkEntry &ctrl, uint32_t index) noexcept {
                auto pNew = linkAt(ctrl, index);
                auto pPrevFirst = linkAt(ctrl, ctrl.next);
                pNew->prev = INVALID_INDEX;
                pNew->next = ctrl.next;
                index = index & HANDLE_INDEX_MASK;
                ctrl.next = index;
                pPrevFirst->prev = index;
            }

            /**
             * Circular linked list semantics to add node to tail/last
             *
             * @param ctrl Control node (prev = last, next = first)
             * @param index Index to add to last
             */
            void insertLast(LinkEntry &ctrl, uint32_t index) noexcept {
                auto pNew = linkAt(ctrl, index);
                auto pPrevLast = linkAt(ctrl, ctrl.prev);
                pNew->next = INVALID_INDEX; // to control
                pNew->prev = ctrl.prev;
                index = index & HANDLE_INDEX_MASK;
                ctrl.prev = index;
                pPrevLast->next = index;
            }

            /**
             * Retrieve the index of first node (avoids needing to remember that next = first)
             * Note, the next=first follows the standard circular linked list semantic
             *
             * @param ctrl Control node
             * @return index of first element
             */
            uint32_t firstIndex(LinkEntry &ctrl) noexcept {
                return ctrl.next;
            }

            /**
             * Retrieve the index of last node (avoids needing to remember that prev = last)
             * Note, the prev=last follows the standard circular linked list semantic
             *
             * @param ctrl Control node
             * @return index of last element
             */
            uint32_t lastIndex(LinkEntry &ctrl) noexcept {
                return ctrl.prev;
            }

            /**
             * Allocate a new handle index - return structure. Actual handle index can be retrieved
             * by re-using the 'check' value of the returned structure. Table may be resized on
             * demand which may throw exception if resize fails.
             *
             * @return reference to structure
             */
            T &alloc() {
                static_assert(std::is_base_of_v<EntryBase, T>);
                uint32_t cap = _entries.capacity();
                uint32_t inc = getIncrementSize(cap);
                if(inc > 0) {
                    _entries.reserve(cap + inc);
                    _freeCount += inc;
                }
                if(_freeCount == 0) {
                    throw std::runtime_error("No more handle slots available");
                }
                T *pNode;
                uint32_t realIndex;
                if(_entries.size() < _entries.capacity()) {
                    // First time use of a given index
                    realIndex = _entries.size();
                    pNode = &_entries.emplace_back();
                    pNode->check = 0;
                } else {
                    // Reuse of a given index
                    auto idx = firstIndex(_free);
                    pNode = at(idx);
                    if(pNode == nullptr) {
                        throw std::logic_error("free count and free list mismatch");
                    }
                    unlink(_free, idx);
                    realIndex = idx & HANDLE_INDEX_MASK;
                }
                pNode->check = (pNode->check & ~HANDLE_INDEX_MASK) + HANDLE_GEN_INC + realIndex;
                pNode->prev = pNode->next = realIndex;
                --_freeCount;
                return *pNode;
            }

            /**
             * Release node at given index.
             *
             * @return true if node released, false if node not released
             */
            bool free(uint32_t idx) noexcept {
                auto ref = lookup(idx);
                idx = idx & HANDLE_INDEX_MASK;
                if(!ref) {
                    return false; // Did not free
                }
                ref->check |= HANDLE_INDEX_MASK; // invalidate check
                assert(ref->prev == (idx & HANDLE_INDEX_MASK));
                assert(ref->next == (idx & HANDLE_INDEX_MASK));
                insertLast(_free, idx);
                ++_freeCount;
                return true;
            }
        };

        /**
         * Book keeping for the roots. Each module has a root, but more roots can be used as needed.
         */
        struct RootEntry : public EntryBase {
            LinkEntry handles; // Linked list of handles per root
        };

        /**
         * Entry of each handle in the table. Each handle represents a single Nucleus reference of
         * the target data structure. A plugin may have multiple references to the handle balancing
         * ref-counting between plugin and Nucleus.
         */
        struct HandleEntry : public EntryBase {
            uint32_t rootIndex = 0; // back-link to roots
            std::shared_ptr<TrackedObject> obj; // Actual object being tracked
        };

    } // namespace handleImpl

    /**
     * Handle tracking.
     */
    class HandleTable {
    private:
        mutable std::shared_mutex _mutex;
        mutable scope::FixedPtr<HandleTable> _table{scope::FixedPtr<HandleTable>::of(this)};
        handleImpl::IndexList<handleImpl::RootEntry> _roots;
        handleImpl::IndexList<handleImpl::HandleEntry> _handles;

        [[nodiscard]] ObjHandle applyUnchecked(ObjHandle::Partial h) const {
            return {_table, h};
        }

        [[nodiscard]] RootHandle applyUncheckedRoot(RootHandle::Partial h) const {
            return {_table, h};
        }

        static uint32_t indexOf(PartialHandle h) {
            return data::IdObfuscator::deobfuscate(h.asInt());
        }

        static PartialHandle handleOf(uint32_t idx) {
            return PartialHandle{data::IdObfuscator::obfuscate(idx)};
        }

    public:
        HandleTable() = default;
        HandleTable(const HandleTable &) = delete;
        HandleTable(HandleTable &&) noexcept = delete;
        ~HandleTable() = default;
        HandleTable &operator=(const HandleTable &) = delete;
        HandleTable &operator=(HandleTable &&) noexcept = delete;
        ObjHandle apply(ObjHandle::Partial h) const;
        std::shared_ptr<TrackedObject> tryGet(const ObjHandle &handle) const;
        std::shared_ptr<TrackedObject> get(const ObjHandle &handle) const;

        ObjHandle create(const std::shared_ptr<TrackedObject> &obj, const RootHandle &root);
        bool release(const ObjHandle &handle) noexcept;

        RootHandle createRoot();
        bool releaseRoot(RootHandle &handle) noexcept;

        bool isObjHandleValid(ObjHandle::Partial handle) const;

        void check(ObjHandle::Partial handle) const;

        /**
         * Extract partial handle from a root handle.
         * @param handle Handle of Root (singleton)
         * @return partial handle.
         */
        RootHandle::Partial partial(const RootHandle &handle) const {
            if(handle) {
                assert(this == &handle.table());
                return handle.partial();
            } else {
                return {};
            }
        }

        /**
         * Extract partial handle from an object handle.
         * @param handle Handle of object
         * @return partial handle
         */
        ObjHandle::Partial partial(const ObjHandle &handle) const {
            if(handle) {
                assert(this == &handle.table());
                return handle.partial();
            } else {
                return {};
            }
        }
    };
} // namespace data
