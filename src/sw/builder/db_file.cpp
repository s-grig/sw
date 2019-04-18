// Copyright (C) 2017-2018 Egor Pugin <egor.pugin@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "db_file.h"

#include "sw_context.h"

#include <sw/manager/storage.h>

#include <primitives/emitter.h>
#include <primitives/date_time.h>
#include <primitives/debug.h>
#include <primitives/exceptions.h>
#include <primitives/lock.h>
#include <primitives/symbol.h>

#include <primitives/log.h>
DECLARE_STATIC_LOGGER(logger, "db_file");

#define FILE_DB_FORMAT_VERSION 3
#define COMMAND_DB_FORMAT_VERSION 2

namespace sw
{

static path getCurrentModuleName()
{
    return primitives::getModuleNameForSymbol(primitives::getCurrentModuleSymbol());
}

static String getCurrentModuleNameHash()
{
    return shorten_hash(blake2b_512(getCurrentModuleName().u8string()), 12);
}

static path getDir(const SwContext &swctx, bool local)
{
    if (local)
        return path(SW_BINARY_DIR) / "db";
    return swctx.getLocalStorage().storage_dir_tmp / "db";
}

static path getFilesDbFilename(const SwContext &swctx, const String &config, bool local)
{
    return getDir(swctx, local) / std::to_string(FILE_DB_FORMAT_VERSION) / config / "files.bin";
}

path getFilesLogFileName(const SwContext &swctx, const String &config, bool local)
{
    auto cfg = shorten_hash(blake2b_512(getCurrentModuleNameHash() + "_" + config), 12);
    return getDir(swctx, local) / std::to_string(FILE_DB_FORMAT_VERSION) / config / ("log_" + cfg + ".bin");
}

static path getCommandsDbFilename(const SwContext &swctx, bool local)
{
    return getDir(swctx, local) / std::to_string(COMMAND_DB_FORMAT_VERSION) / "commands.bin";
}

path getCommandsLogFileName(const SwContext &swctx, bool local)
{
    auto cfg = shorten_hash(blake2b_512(getCurrentModuleNameHash()), 12);
    return getDir(swctx, local) / std::to_string(COMMAND_DB_FORMAT_VERSION) / ("log_" + cfg + ".bin");
}

static void load(FileStorage &fs, const path &fn,
    ConcurrentHashMap<path, FileRecord> &files, std::unordered_map<int64_t, std::unordered_set<int64_t>> &deps)
{
    ScopedShareableFileLock lk(fn);

    primitives::BinaryStream b;
    try
    {
        b.load(fn);
    }
    catch (std::exception &)
    {
        if (fs::exists(fn))
            throw;
        return;
    }
    while (!b.eof())
    {
        size_t sz; // record size
        b.read(sz);
        if (!b.has(sz))
            break; // record is in bad shape

        size_t h;
        b.read(h);

        String p;
        b.read(p);

        auto kv = files.insert(h);
        kv.first->file = p;
        kv.first->data = fs.registerFile(p)->data;

        decltype(kv.first->data->last_write_time) lwt;
        b.read(lwt);

        if (kv.first->data->last_write_time < lwt)
        {
            kv.first->data->last_write_time = lwt;
        }

        kv.first->data->refreshed = FileData::RefreshType::Unrefreshed;

        size_t n;
        b.read(n);

        //std::unordered_map<int64_t, std::unordered_set<int64_t>> deps2;
        for (int i = 0; i < n; i++)
        {
            size_t h2;
            b.read(h2);
            deps[h].insert(h2);
            //deps2[h].insert(h2);
        }
        // maybe also create local FileRecord & FileData to prevent curruption in main db?
        //deps.insert(deps2.begin(), deps2.end());
    }
}

FileDb::FileDb(const SwContext &swctx)
    : swctx(swctx)
{
}

void FileDb::load(FileStorage &fs, ConcurrentHashMap<path, FileRecord> &files, bool local) const
{
    std::unordered_map<int64_t, std::unordered_set<int64_t>> deps;

    sw::load(fs, getFilesDbFilename(swctx, fs.config, local), files, deps);
    //try {
        sw::load(fs, getFilesLogFileName(swctx, fs.config, local), files, deps);
    //} catch (...) {}
    error_code ec;
    fs::remove(getFilesLogFileName(swctx, fs.config, local), ec);

    for (auto &[k, v] : deps)
    {
        for (auto &h2 : v)
        {
            if (!h2)
                continue;
            auto k2 = &files[h2];
            if (k2 && !k2->file.empty())
                files[k].implicit_dependencies.insert({ k2->file, k2 });
        }
    }
}

void FileDb::save(FileStorage &fs, ConcurrentHashMap<path, FileRecord> &files, bool local) const
{
    const auto f = getFilesDbFilename(swctx, fs.config, local);

    // first, we load current copy of files
    // disable for now
    if (0)
    {
        ConcurrentHashMap<path, FileRecord> old;
        load(fs, old, local);

        // compare and renew our (actually any) copy
        for (auto i = old.getIterator(); i.isValid(); i.next())
        {
            auto &f = *i.getValue();
            if (f.file.empty())
                continue;
            auto[ptr, inserted] = files.insert(f.file, f);
            if (!inserted && f.data && *ptr < f)
                *ptr = f;
        }

        /*for (auto &[_, f] : old)
        {
            if (f->file.empty())
                continue;
            auto[ptr, inserted] = files.insert(f->file, *f);
            if (!inserted && f->data && *ptr < *f)
                *ptr = *f;
        }*/
    }

    primitives::BinaryStream b(10'000'000); // reserve amount
    std::vector<uint8_t> v;
    for (auto i = files.getIterator(); i.isValid(); i.next())
    {
        auto &f = *i.getValue();
        if (!f.data)
            continue;

        write(v, f);
        b.write(v.size());
        b.write(v.data(), v.size());
    }
    /*for (auto &[_, f] : files)
    {
        if (!f->data)
            continue;

        auto h1 = std::hash<path>()(f->file);
        b.write(h1);
        b.write(normalize_path(f->file));
        b.write(f->data->last_write_time.time_since_epoch().count());
        //b.write(f.data->size);
        b.write(f->implicit_dependencies.size());

        for (auto &[f, d] : f->implicit_dependencies)
            b.write(std::hash<path>()(d->file));
    }*/
    if (b.empty())
        return;
    fs::create_directories(f.parent_path());
    b.save(f);
}

template <class T>
static void write_int(std::vector<uint8_t> &vec, T val)
{
    auto sz = vec.size();
    vec.resize(sz + sizeof(val));
    memcpy(&vec[sz], &val, sizeof(val));
}

static void write_str(std::vector<uint8_t> &vec, const String &val)
{
    auto sz = val.size() + 1;
    //write_int(vec, sz);
    auto vsz = vec.size();
    vec.resize(vsz + sz);
    memcpy(&vec[vsz], &val[0], sz);
}

void FileDb::write(std::vector<uint8_t> &v, const FileRecord &f) const
{
    v.clear();

    write_int(v, std::hash<path>()(f.file));
    write_str(v, normalize_path(f.file));
    write_int(v, f.data->last_write_time.time_since_epoch().count());
    //write_int(v, f.data->size);
    //write_int(v, f.data->flags.to_ullong());

    auto n = f.implicit_dependencies.size();
    write_int(v, n);

    for (auto &[f, d] : f.implicit_dependencies)
        write_int(v, std::hash<path>()(d->file));
}

static void load(const path &fn, ConcurrentCommandStorage &commands)
{
    primitives::BinaryStream b;
    try
    {
        b.load(fn);
    }
    catch (std::exception &)
    {
        if (fs::exists(fn))
            throw;
        return;
    }
    while (!b.eof())
    {
        size_t k;
        b.read(k);
        size_t h;
        b.read(h);
        auto r = commands.insert_ptr(k, h);
        if (!r.second)
            *r.first = h;
    }
}

void FileDb::load(ConcurrentCommandStorage &commands, bool local) const
{
    sw::load(getCommandsDbFilename(swctx, local), commands);
    try {
        sw::load(getCommandsLogFileName(swctx, local), commands);
    } catch (...) {}
    error_code ec;
    fs::remove(getCommandsLogFileName(swctx, local), ec);
}

void FileDb::save(ConcurrentCommandStorage &commands, bool local) const
{
    primitives::BinaryStream b(10'000'000); // reserve amount
    for (auto i = commands.getIterator(); i.isValid(); i.next())
    {
        b.write(i.getKey());
        b.write(*i.getValue());
    }
    /*for (auto &[k, v] : commands)
    {
        b.write(k);
        b.write(*v);
    }*/
    if (b.empty())
        return;
    auto p = getCommandsDbFilename(swctx, local);
    fs::create_directories(p.parent_path());
    b.save(p);
}

}