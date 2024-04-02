#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>

namespace util {

    /**
     * Container to support c-api pointer validation. At the cost of a map lookup, adds some
     * robustness by ensuring that either the pointer is valid - pointing to a data structure of the
     * given type, or an exception results.
     *
     * By encapsulating, this can be conditionally replaced with more optimal unsafe version.
     */
    template<typename T, typename P = std::unique_ptr<T>>
    class CheckedPointers {
        // TODO: Add compile optimization option if needed
    public:
        using DataType = T; // Type of data contained
        using PointerType = P; // Storage pointer / smart-pointer

    private:
        std::map<uintptr_t, PointerType> _refs;

        std::pair<void *, uintptr_t> add(PointerType entry) {
            auto refPtr = static_cast<void *>(entry.get());
            // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
            auto idx = reinterpret_cast<uintptr_t>(refPtr);
            _refs.emplace(idx, std::move(entry));
            return {refPtr, idx};
        }

    public:
        /**
         * Erase real pointer at location given by handle. Caller responsible for mutex.
         *
         * @param handle - handle to data (when void* expected)
         */
        void erase(void const *handle) {
            // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
            erase(reinterpret_cast<uintptr_t>(handle));
        }

        /**
         * Erase real pointer at location given by handle. Caller responsible for mutex.
         *
         * @param handle - handle to data (when an integer is expected)
         */
        void erase(uintptr_t handle) {
            _refs.erase(handle);
        }

        /**
         * Retrieve real pointer at location given by handle. Caller responsible for mutex.
         *
         * @param handle - handle to data (when void* expected)
         * @return workable pointer
         */
        [[nodiscard]] const PointerType &at(void const *handle) const {
            // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
            return at(reinterpret_cast<uintptr_t>(handle));
        }

        /**
         * Retrieve real pointer at location given by handle. Caller responsible for mutex.
         *
         * @param handle - handle to data (when integer is expected)
         * @return workable pointer
         */
        [[nodiscard]] const PointerType &at(uintptr_t handle) const {
            return _refs.at(handle);
        }

        /**
         * Add/move pointer to table, return an integer handle. Caller responsible for mutex.
         * @param entry Pointer to move
         * @return integer handle (caller must only treat as a round-trip integer)
         */
        [[nodiscard]] uintptr_t addAsInt(PointerType entry) {
            return add(std::move(entry)).second;
        }

        /**
         * Add/move pointer to table, return an opaque void* handle. Caller responsible for mutex.
         * @param entry Pointer to move
         * @return void* handle (legal pointer, but caller must not cast or assume anything about
         * the data it points to)
         */
        [[nodiscard]] void *addAsPtr(PointerType entry) {
            return add(std::move(entry)).first;
        }

        /**
         * Copy all pointers into vector - not applicable for unique_ptr. Caller responsible for
         * mutex.
         * @param target vector to collect array of pointers
         */
        void insertInto(std::vector<PointerType> &target) {
            target.reserve(_refs.size());
            for(const auto &e : _refs) {
                target.emplace_back(e.second);
            }
        }
    };

    /**
     * Safer version of CheckedPointers, with locking and assumes shared pointers.
     */

    template<typename T>
    class CheckedSharedPointers {
    public:
        using DataType = T; // Type of data contained
        using PointerType = std::shared_ptr<T>;

    private:
        CheckedPointers<DataType, PointerType> _table;
        mutable std::mutex _mutex;

    public:
        /**
         * Erase real pointer at location given by handle.
         *
         * @param handle - handle to data (when void* expected)
         */
        void erase(void const *handle) {
            std::unique_lock guard{_mutex};
            _table.erase(handle);
        }

        /**
         * Erase real pointer at location given by handle.
         *
         * @param handle - handle to data (when an integer is expected)
         */
        void erase(uintptr_t handle) {
            std::unique_lock guard{_mutex};
            _table.erase(handle);
        }

        /**
         * Retrieve real pointer at location given by handle. Caller responsible for mutex.
         *
         * @param handle - handle to data (when void* expected)
         * @return workable pointer
         */
        [[nodiscard]] PointerType at(void const *handle) const {
            std::unique_lock guard{_mutex};
            return _table.at(handle);
        }

        /**
         * Retrieve real pointer at location given by handle. Caller responsible for mutex.
         *
         * @param handle - handle to data (when integer is expected)
         * @return workable pointer
         */
        [[nodiscard]] PointerType at(uintptr_t handle) const {
            std::unique_lock guard{_mutex};
            return _table.at(handle);
        }

        /**
         * Typically used to delegate to member function per handle.
         */
        template<typename FuncCall, typename... Args>
        std::invoke_result_t<FuncCall, DataType *, Args...> invoke(
            void const *handle, const FuncCall &func, Args &&...args) {

            static_assert(std::is_invocable_v<FuncCall, DataType *, Args...>);
            const auto ptr = at(handle);
            return std::invoke(func, ptr.get(), std::forward<Args>(args)...);
        }

        /**
         * Add shared pointer to table, return an integer handle.
         * @param entry Shared Pointer to insert
         * @return integer handle (caller must only treat as a round-trip integer)
         */
        uintptr_t addAsInt(PointerType entry) {
            std::unique_lock guard{_mutex};
            return _table.addAsInt(entry);
        }

        /**
         * Add shared pointer to table, return an opaque void* handle.
         * @param entry Shared Pointer to insert
         * @return void* handle (legal pointer, but caller must not cast or assume anything about
         * the data it points to)
         */
        void *addAsPtr(PointerType entry) {
            std::unique_lock guard{_mutex};
            return _table.addAsPtr(entry);
        }

        /**
         * Iterate over all pointers in table, and invoke a function (e.g. to clean up)
         * @param func Function to invoke
         * @param args arguments to pass to function
         */
        template<typename FuncCall, typename... Args>
        void invokeAll(const FuncCall &func, Args &&...args) {
            static_assert(std::is_invocable_v<FuncCall, DataType *, Args...>);
            std::vector<PointerType> copy;
            {
                std::unique_lock guard{_mutex};
                _table.insertInto(copy);
            }
            // Perform actual call with no mutex (hence copies)
            for(const auto &ptr : copy) {
                std::invoke(func, ptr.get(), std::forward<Args>(args)...);
            }
        }
    };

} // namespace util
