// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2017-2020 Egor Pugin <egor.pugin@gmail.com>

#pragma once

#include <nlohmann/json_fwd.hpp>
#include <primitives/filesystem.h>

#include <memory>
#include <optional>
#include <variant>

namespace sw
{

struct Directories;

using TargetSettingKey = String;
using TargetSettingValue = String;
struct TargetSetting;
struct TargetSettings;

struct SW_CORE_API TargetSettings
{
    enum StringType : int
    {
        KeyValue    = 0,

        Json,
        // yml

        Simple      = KeyValue,
    };

    TargetSetting &operator[](const TargetSettingKey &);
    const TargetSetting &operator[](const TargetSettingKey &) const;

    void mergeMissing(const TargetSettings &);
    void mergeAndAssign(const TargetSettings &);
    void erase(const TargetSettingKey &);

    // other merges
    void mergeFromString(const String &s, int type = Json);
    void mergeFromJson(const nlohmann::json &);

    String getHash() const;
    String toString(int type = Json) const;

    bool operator==(const TargetSettings &) const;
    bool operator<(const TargetSettings &) const;
    bool isSubsetOf(const TargetSettings &) const;

    auto begin() { return settings.begin(); }
    auto end() { return settings.end(); }
    auto begin() const { return settings.begin(); }
    auto end() const { return settings.end(); }

    bool empty() const;

private:
    std::map<TargetSettingKey, TargetSetting> settings;

    //String toStringKeyValue() const;
    nlohmann::json toJson() const;
    size_t getHash1() const;

    friend struct TargetSetting;

#ifdef BOOST_SERIALIZATION_ACCESS_HPP
    friend class boost::serialization::access;
    template <class Ar>
    void serialize(Ar &ar, unsigned)
    {
        ar & settings;
    }
#endif
};

struct SW_CORE_API TargetSetting
{
    struct nulltag_t
    {
        bool operator==(const nulltag_t &) const { return true; }
        bool operator<(const nulltag_t &) const { return false; }
    };

    using Value = TargetSettingValue;
    using Map = TargetSettings;
    using ArrayValue = TargetSetting;
    using Array = std::vector<ArrayValue>;
    using NullType = nulltag_t;

    TargetSetting() = default;
    TargetSetting(const TargetSetting &);
    TargetSetting &operator=(const TargetSetting &);

    TargetSetting &operator[](const TargetSettingKey &k);
    const TargetSetting &operator[](const TargetSettingKey &k) const;

    template <class U>
    TargetSetting(const U &u)
    {
        if constexpr (std::is_same_v<U, path>)
            setAbsolutePathValue(u);
        else
            operator=(u);
    }

    template <class U>
    TargetSetting &operator=(const U &u)
    {
        if constexpr (std::is_same_v<U, std::nullptr_t>)
        {
            setNull();
            return *this;
        }
        reset();
        value = u;
        return *this;
    }

    bool operator==(const TargetSetting &) const;
    //bool operator!=(const TargetSetting &) const;
    bool operator<(const TargetSetting &) const;

    template <class U>
    bool operator==(const U &u) const
    {
        auto v = std::get_if<Value>(&value);
        if (!v)
            return false;
        return *v == u;
    }

    template <class U>
    bool operator!=(const U &u) const
    {
        return !operator==(u);
    }

    explicit operator bool() const;
    //bool hasValue() const;
    bool isEmpty() const;
    bool isNull() const;
    void setNull();

    const String &getValue() const;
    const Array &getArray() const;
    Map &getMap();
    const Map &getMap() const;

    // path helpers
    path getPathValue(const Directories &) const;
    path getPathValue(const path &root) const;
    void setPathValue(const Directories &, const path &value);
    void setPathValue(const path &root, const path &value);
    path getAbsolutePathValue() const;
    void setAbsolutePathValue(const path &value);

    void push_back(const ArrayValue &);
    void reset();

    void use();
    void setUseCount(int);

    void setRequired(bool = true);
    bool isRequired() const;

    void useInHash(bool);
    bool useInHash() const { return used_in_hash; }

    void ignoreInComparison(bool);
    bool ignoreInComparison() const { return ignore_in_comparison; }

    void serializable(bool);
    bool serializable() const { return serializable_; }

    void mergeAndAssign(const TargetSetting &);
    void mergeMissing(const TargetSetting &);
    void mergeFromJson(const nlohmann::json &);

    bool isValue() const;
    bool isArray() const;
    bool isObject() const;

private:
    int use_count = 1;
    bool required = false;
    bool used_in_hash = true;
    bool ignore_in_comparison = false;
    bool serializable_ = true;
    // when adding new member, add it to copy_fields()!
    std::variant<std::monostate, Value, Array, Map, NullType> value;

    nlohmann::json toJson() const;
    size_t getHash1() const;
    void copy_fields(const TargetSetting &);

    friend struct TargetSettings;

#ifdef BOOST_SERIALIZATION_ACCESS_HPP
    friend class boost::serialization::access;

    template <class Ar>
    void load(Ar &ar, unsigned)
    {
        ar & used_in_hash;
        ar & ignore_in_comparison;
        //
        size_t idx;
        ar & idx;
        switch (idx)
        {
        case 0:
            break;
        case 1:
        {
            Value v;
            ar & v;
            value = v;
        }
            break;
        case 2:
        {
            Array v1;
            ar & v1;
            value = v1;
        }
            break;
        case 3:
        {
            Map v;
            ar & v;
            value = v;
        }
            break;
        }
    }
    template <class Ar>
    void save(Ar &ar, unsigned) const
    {
        if (!serializable_)
            return;
        ar & used_in_hash;
        ar & ignore_in_comparison;
        //
        ar & value.index();
        switch (value.index())
        {
        case 0:
            break;
        case 1:
            ar & std::get<Value>(value);
            break;
        case 2:
            ar & std::get<Array>(value);
            break;
        case 3:
            ar & std::get<Map>(value);
            break;
        }
    }
    BOOST_SERIALIZATION_SPLIT_MEMBER()
#endif
};

SW_CORE_API
TargetSettings toTargetSettings(const struct OS &);

// serialization

SW_CORE_API
TargetSettings loadSettings(const path &archive_fn, int type = 0);

SW_CORE_API
void saveSettings(const path &archive_fn, const TargetSettings &, int type = 0);

} // namespace sw
