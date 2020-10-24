// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2017-2020 Egor Pugin <egor.pugin@gmail.com>

#pragma once

#include "../program.h"

#include <sw/builder/command.h>
#include <sw/core/sw_context.h>

#define DETECT_ARGS ::sw::Build &b
#define DETECT_ARGS_PASS b
#define DETECT_ARGS_PASS_TO_LAMBDA &b
#define DETECT_ARGS_PASS_FIRST_CALL(ctx) (::sw::SwContext&)(ctx)
#define DETECT_ARGS_PASS_FIRST_CALL_SIMPLE DETECT_ARGS_PASS_FIRST_CALL(getContext())

namespace sw
{

struct PredefinedProgramTarget : PredefinedTarget, PredefinedProgram
{
    using PredefinedTarget::PredefinedTarget;
};

struct SimpleProgram : Program
{
    using Program::Program;

    std::unique_ptr<Program> clone() const override { return std::make_unique<SimpleProgram>(*this); }
    std::shared_ptr<builder::Command> getCommand() const override
    {
        if (!cmd)
        {
            cmd = std::make_shared<builder::Command>();
            cmd->setProgram(file);
        }
        return cmd;
    }

private:
    mutable std::shared_ptr<builder::Command> cmd;
};

struct SW_DRIVER_CPP_API ProgramDetector
{
    ProgramDetector();

    // combined function for users
    void detectProgramsAndLibraries(DETECT_ARGS);

    String getMsvcPrefix(const path &program) const;

    static PredefinedProgramTarget &addProgram(DETECT_ARGS, const PackageId &id, const TargetSettings &ts, const std::shared_ptr<Program> &p);

    using DetectablePackageEntryPoint = std::function<void(Build &)>;
    using DetectablePackageEntryPoints = std::unordered_map<UnresolvedPackage, DetectablePackageEntryPoint>;
    static DetectablePackageEntryPoints getDetectablePackages();

    template <class T>
    static T &addTarget(DETECT_ARGS, const PackageId &id, const TargetSettings &ts);

private:
    struct VSInstance
    {
        path root;
        Version version;
    };
    using VSInstances = VersionMap<VSInstance>;

    VSInstances vsinstances1;
    std::map<path, String> msvc_prefixes;

    static VSInstances gatherVSInstances();
    VSInstances &getVSInstances();
    static void log_msg_detect_target(const String &m);
    String getMsvcPrefix(builder::detail::ResolvableCommand c);
    auto &getMsvcIncludePrefixes() { return msvc_prefixes; }
    const auto &getMsvcIncludePrefixes() const { return msvc_prefixes; }

    //void detectMsvc(DETECT_ARGS);
    void detectMsvc15Plus(DETECT_ARGS);
    void detectMsvc14AndOlder(DETECT_ARGS);
    void detectWindowsSdk(DETECT_ARGS);
    void detectMsvcCommon(const path &compiler, const Version &vs_version,
        ArchType target_arch, const path &host_root, const TargetSettings &ts, const path &idir,
        const path &root, const path &target,
        DETECT_ARGS);

    void detectWindowsClang(DETECT_ARGS);
    void detectIntelCompilers(DETECT_ARGS);
    void detectWindowsCompilers(DETECT_ARGS);
    void detectNonWindowsCompilers(DETECT_ARGS);

#define DETECT(x) void detect##x##Compilers(DETECT_ARGS);
#include "detect.inl"
#undef DETECT
};

ProgramDetector &getProgramDetector();

void addSettingsAndSetPrograms(const SwCoreContext &, TargetSettings &);
void addSettingsAndSetHostPrograms(const SwCoreContext &, TargetSettings &);

}
