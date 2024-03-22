#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>

namespace util {
    template<typename T>
    class SerializableBase;
    template<typename T>
    class ArchiveBase;
    template<typename T>
    class ArchiveAdapter;
    template<typename T>
    class AbstractArchiver;
    template<typename T>
    class AbstractDearchiver;
} // namespace util

namespace ggapi {

    //
    // Common definitions that are needed ahead of other definitions
    //

    struct ArchiveTraits;
    class Buffer;
    class CallbackManager;
    class ChannelCloseCallback;
    class ChannelListenCallback;
    class Container;
    class Future;
    class FutureCallback;
    class FutureSet;
    class LifecycleCallback;
    class List;
    class ModuleScope;
    class ObjHandle;
    class Promise;
    class Struct;
    class Subscription;
    class Symbol;
    class TopicCallback;

    using TopicCallbackLambda = std::function<ObjHandle(Symbol, Container)>;
    using LifecycleCallbackLambda = std::function<bool(ModuleScope, Symbol, Struct)>;

    using Serializable = util::SerializableBase<ArchiveTraits>;
    using ArchiveAdapter = util::ArchiveAdapter<ArchiveTraits>;
    using Archive = util::ArchiveBase<ArchiveTraits>;
    using AbstractArchiver = util::AbstractArchiver<ArchiveTraits>;
    using AbstractDearchiver = util::AbstractDearchiver<ArchiveTraits>;

    template<typename Func, typename... Args>
    void callApiThrowError(Func &&f, Args &&...args);
    template<typename Func, typename... Args>
    bool callBoolApiThrowError(Func &&f, Args &&...args);
    template<typename Handle, typename Func, typename... Args>
    Handle callHandleApiThrowError(Func &&f, Args &&...args);

    // All uses of these functions need to be replaced
    template<typename T>
    T trapErrorReturn(const std::function<T()> &fn) noexcept;
    template<typename T>
    T callApiReturn(const std::function<T()> &fn);
    template<typename T>
    T callApiReturnHandle(const std::function<uint32_t()> &fn);
    void callApi(const std::function<void()> &fn);

    /**
     * Helper function for filling a string buffer from a Nucleus String function.
     *
     * In new pattern, caller may pre-guess size of string. If string is too small, API will specify
     * a new desired size, and fill fill up to the provided size.
     * @return filled string
     */
    template<
        class Traits = std::char_traits<char>,
        class Alloc = std::allocator<char>,
        class StringFillFn>
    std::basic_string<char, Traits, Alloc> stringFillHelper(
        size_t strLen, StringFillFn stringFillFn) {
        if(strLen == 0) {
            return {};
        }
        std::basic_string<char, Traits, Alloc> str(strLen, '\0');
        size_t fillLen;
        size_t reqLen;
        for(;;) {
            fillLen = 0;
            reqLen = 0;
            if constexpr(std::is_invocable_v<StringFillFn, char *, size_t, size_t *, size_t *>) {
                // New API pattern
                stringFillFn(str.data(), str.size(), &fillLen, &reqLen);
            } else {
                static_assert(std::is_invocable_v<StringFillFn, char *, size_t>);
                static_assert(std::is_convertible_v<
                              std::invoke_result_t<StringFillFn, char *, size_t>,
                              size_t>);
                // TODO: Deprecate Old API pattern
                reqLen = fillLen = stringFillFn(str.data(), str.size());
            }
            if(reqLen <= str.size()) {
                break;
            }
        }
        str.resize(fillLen);
        return str;
    }

} // namespace ggapi
