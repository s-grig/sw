// Copyright (C) 2017-2018 Egor Pugin <egor.pugin@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include <sw/manager/package_path.h>
#include <sw/manager/version.h>

#include <primitives/emitter.h>
#include <primitives/filesystem.h>

namespace sw
{

struct SwContext;

enum class GeneratorType
{
    UnspecifiedGenerator,

    Batch,
    CMake,
    CompilationDatabase,
    Make,
    NMake,
    Ninja,
    QMake,
    Shell,
    VisualStudio,
    VisualStudioNMake,
    VisualStudioUtility,
    VisualStudioNMakeAndUtility,
};

struct Generator
{
    GeneratorType type = GeneratorType::UnspecifiedGenerator;
    //path dir;
    //path file;

    virtual ~Generator() = default;

    virtual void generate(const SwContext &) = 0;
    //void generate(const path &file, const Build &b);
    //virtual void createSolutions(Build &b) {}
    //virtual void initSolutions(Build &b) {}

    static std::unique_ptr<Generator> create(const String &s);
};

struct VSGenerator : Generator
{
	Version version;
    String cwd;
    path dir;
    const path projects_dir = "projects";
    const InsecurePath deps_subdir = "Dependencies";
    const InsecurePath overridden_deps_subdir = "Overridden Packages";
    const String predefined_targets_dir = ". SW Predefined Targets"s;
    const String all_build_name = "ALL_BUILD"s;
    const String build_dependencies_name = "BUILD_DEPENDENCIES"s;

    VSGenerator();

    void generate(const SwContext &b) override;
    //void createSolutions(Build &b) override;
    //void initSolutions(Build &b) override;
};

struct NinjaGenerator : Generator
{
    void generate(const SwContext &) override;
};

struct MakeGenerator : Generator
{
    void generate(const SwContext &b) override;
};

struct ShellGenerator : Generator
{
    bool batch = false;

    void generate(const SwContext &b) override;
};

struct CompilationDatabaseGenerator : Generator
{
    void generate(const SwContext &b) override;
};

String toString(GeneratorType Type);
String toPathString(GeneratorType Type);
GeneratorType fromString(const String &ss);

}
