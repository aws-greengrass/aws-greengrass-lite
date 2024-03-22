#pragma once
#include "data/value_type.hpp"
#include <archive.hpp>
#include <filesystem>

namespace data {
    class StructElement;
    class StructModelBase;
    class ContainerModelBase;
    struct ArchiveTraits;

    using Serializable = util::SerializableBase<ArchiveTraits>;
    using ArchiveAdapter = util::ArchiveAdapter<ArchiveTraits>;
    using AbstractArchiver = util::AbstractArchiver<ArchiveTraits>;
    using AbstractDearchiver = util::AbstractDearchiver<ArchiveTraits>;
    using Archive = util::ArchiveBase<ArchiveTraits>;

    struct ArchiveTraits {
        using SerializableType = Serializable;
        using ValueType = data::ValueType;
        using ReadType = data::StructElement;
        using KeyType = data::Symbol;
        using SymbolType = data::Symbol;
        using AutoKeyType = data::Symbolish;

        static ValueType toValue(const ReadType &rv);
        static SymbolType toSymbol(const ReadType &rv);
        static std::string toString(const ReadType &rv);
        static uint64_t toInt64(const ReadType &rv);
        static double toDouble(const ReadType &rv);
        static bool toBool(const ReadType &rv);
        static bool hasValue(const ReadType &rv);
        static bool isList(const ReadType &rv);
        static std::shared_ptr<ArchiveAdapter> toKey(
            const ReadType &rv, const KeyType &key, bool ignoreCase);
        static std::shared_ptr<ArchiveAdapter> toList(const ReadType &rv);
        static std::vector<KeyType> toKeys(const ReadType &rv);
        template<typename T>
        static T *initSharedPtr(std::shared_ptr<T> &ptr) {
            return nullptr;
        }
    };

    namespace archive {
        void readFromStruct(const std::shared_ptr<ContainerModelBase> &data, Serializable &target);
        void readFromFile(const std::filesystem::path &file, Serializable &target);
        void readFromYamlFile(const std::filesystem::path &file, Serializable &target);
        void readFromJsonFile(const std::filesystem::path &file, Serializable &target);
        void writeToStruct(const std::shared_ptr<StructModelBase> &data, Serializable &target);
        void writeToFile(const std::filesystem::path &file, Serializable &source);
        void writeToYamlFile(const std::filesystem::path &file, Serializable &source);
        void writeToJsonFile(const std::filesystem::path &file, Serializable &source);
    } // namespace archive

} // namespace data
