// Copyright (C) 2016-2019 Egor Pugin <egor.pugin@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

//#include <nlohmann/json.hpp>
#include <primitives/string.h>

#include <memory>
#include <optional>

namespace sw
{

/*struct SettingValue
{
    //String value;
    bool used = false;

    // int inherit?
    // -1 - infinite depth
    // 0 - false
    // >0 - depth
    bool inherit = false;
};*/

using TargetSettingKey = String;
using TargetSettingValue = String;
struct TargetSettings;

struct SW_CORE_API TargetSetting
{
    TargetSetting(const TargetSettingKey &k);
    TargetSetting(const TargetSetting &);
    TargetSetting &operator=(const TargetSetting &);

    TargetSetting &operator[](const TargetSettingKey &k);
    const TargetSetting &operator[](const TargetSettingKey &k) const;

    TargetSetting &operator=(const TargetSettings &u);

    template <class U>
    TargetSetting &operator=(const U &u)
    {
        value = u;
        return *this;
    }

    bool operator==(const TargetSetting &) const;
    bool operator!=(const TargetSetting &) const;

    template <class U>
    bool operator==(const U &u) const
    {
        if (!value.has_value())
            return false;
        return *value == u;
    }

    template <class U>
    bool operator!=(const U &u) const
    {
        return !operator==(u);
    }

    bool operator<(const TargetSetting &) const;
    explicit operator bool() const;
    //bool hasValue() const;
    const String &getValue() const;
    const TargetSettings &getSettings() const;
    void merge(const TargetSetting &);
    void push_back(const TargetSettingValue &);

private:
    TargetSettingKey key;
    std::optional<TargetSettingValue> value;
    std::optional<std::vector<TargetSettingValue>> array;
    mutable std::unique_ptr<TargetSettings> settings;
};

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

    void merge(const TargetSettings &);
    void erase(const TargetSettingKey &);

    String getConfig() const; // getShortConfig()?
    String getHash() const;
    String toString(int type = Simple) const;

    bool operator==(const TargetSettings &) const;
    bool operator<(const TargetSettings &) const;

    auto begin() { return settings.begin(); }
    auto end() { return settings.end(); }
    auto begin() const { return settings.begin(); }
    auto end() const { return settings.end(); }

private:
    mutable std::map<TargetSettingKey, TargetSetting> settings;

    String toStringKeyValue() const;
    String toJsonString() const;
};

SW_CORE_API
TargetSettings toTargetSettings(const struct OS &);

} // namespace sw