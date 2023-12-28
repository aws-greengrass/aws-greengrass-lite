#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace ggapi {

    //
    // Common definitions that are needed ahead of other definitions
    //

    class Buffer;
    class CallbackManager;
    class ChannelCloseCallback;
    class ChannelListenCallback;
    class Container;
    class GgApiError;
    class LifecycleCallback;
    class List;
    class ModuleScope;
    class ObjHandle;
    class Scope;
    class Struct;
    class Subscription;
    class Symbol;
    class Task;
    class TaskCallback;
    class TopicCallback;

    using TopicCallbackLambda = std::function<Struct(Task, Symbol, Struct)>;
    using LifecycleCallbackLambda = std::function<bool(ModuleScope, Symbol, Struct)>;
    using TaskCallbackLambda = std::function<void(Struct)>;

    template<typename T>
    T trapErrorReturn(const std::function<T()> &fn) noexcept;
    template<typename T>
    T callApiReturn(const std::function<T()> &fn);
    template<typename T>
    T callApiReturnHandle(const std::function<uint32_t()> &fn);
    void callApi(const std::function<void()> &fn);

    // Helper functions for consistent string copy pattern
    template<
        class Traits = std::char_traits<char>,
        class Alloc = std::allocator<char>,
        class StringFillFn>
    std::basic_string<char, Traits, Alloc> stringFillHelper(
        size_t strLen, StringFillFn stringFillFn) {
        static_assert(std::is_invocable_v<StringFillFn, char *, size_t>);
        static_assert(
            std::is_convertible_v<std::invoke_result_t<StringFillFn, char *, size_t>, size_t>);
        if(strLen == 0) {
            return {};
        }
        std::basic_string<char, Traits, Alloc> str(strLen, '\0');
        size_t finalLen = stringFillFn(str.data(), str.size());
        str.resize(finalLen);
        return str;
    }

} // namespace ggapi
