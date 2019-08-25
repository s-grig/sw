// Copyright (C) 2016-2019 Egor Pugin <egor.pugin@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "build.h"

#include "driver.h"
#include "input.h"
#include "sw_context.h"

#include <sw/builder/execution_plan.h>

#include <boost/current_function.hpp>
#include <nlohmann/json.hpp>
#include <primitives/executor.h>
#include <primitives/sw/cl.h>

#include <primitives/log.h>
DECLARE_STATIC_LOGGER(logger, "build");

//cl::opt<bool> dry_run("n", cl::desc("Dry run"));

static cl::opt<bool> build_always("B", cl::desc("Build always"));
static cl::opt<int> skip_errors("k", cl::desc("Skip errors"));
static cl::opt<bool> time_trace("time-trace", cl::desc("Record chrome time trace events"));

//static cl::opt<bool> hide_output("hide-output");
static cl::opt<bool> cl_show_output("show-output");
static cl::opt<bool> cl_write_output_to_file("write-output-to-file");
static cl::opt<bool> print_graph("print-graph", cl::desc("Print file with build graph"));

#define CHECK_STATE(from)                                                                 \
    if (state != from)                                                                    \
    throw SW_RUNTIME_ERROR("Unexpected build state = " + std::to_string(toIndex(state)) + \
                           ", expected = " + std::to_string(toIndex(from)))

#define CHECK_STATE_AND_CHANGE(from, to)     \
    CHECK_STATE(from);                       \
    SCOPE_EXIT                               \
    {                                        \
        if (std::uncaught_exceptions() == 0) \
            state = to;                      \
    };                                       \
    LOG_TRACE(logger, "build id " << this << " performing " << BOOST_CURRENT_FUNCTION)

namespace sw
{

SwBuild::SwBuild(SwContext &swctx, const path &build_dir)
    : swctx(swctx)
    , build_dir(build_dir)
{
}

SwBuild::~SwBuild()
{
}

path SwBuild::getBuildDirectory() const
{
    // use like this for now
    return fs::current_path() / SW_BINARY_DIR;
    //return build_dir;
}

void SwBuild::build()
{
    // this is all in one call
    while (step())
        ;
}

bool SwBuild::step()
{
    switch (state)
    {
    case BuildState::NotStarted:
        // load provided inputs
        load();
        return true;
    case BuildState::InputsLoaded:
        setTargetsToBuild();
        return true;
    case BuildState::TargetsToBuildSet:
        resolvePackages();
        return true;
    case BuildState::PackagesResolved:
        loadPackages();
        return true;
    case BuildState::PackagesLoaded:
        // prepare targets
        prepare();
        return true;
    case BuildState::Prepared:
        // create ex. plan and execute it
        execute();
        return true;
    default:
        break;
    }
    return false;
}

void SwBuild::overrideBuildState(BuildState s) const
{
    LOG_TRACE(logger, "build id " << this << " overriding state from " << toIndex(state) << " to " << toIndex(s));

    state = s;
}

void SwBuild::load()
{
    CHECK_STATE_AND_CHANGE(BuildState::NotStarted, BuildState::InputsLoaded);

    std::set<Input *> iv;
    for (auto &i : inputs)
        iv.insert((Input*)&i.getInput());
    swctx.loadEntryPoints(iv, true);

    // and load packages
    for (auto &i : inputs)
    {
        auto tgts = i.loadTargets(*this);
        for (auto &tgt : tgts)
        {
            known_packages.insert(tgt->getPackage()); // also mark them as known
            if (tgt->getSettings()["dry-run"] == "true")
                continue;
            getTargets()[tgt->getPackage()].push_back(tgt);
        }
    }
}

std::unordered_map<UnresolvedPackage, LocalPackage> SwBuild::install(const UnresolvedPackages &upkgs)
{
    auto m = swctx.install(upkgs);
    for (auto &[_, p] : m)
        addKnownPackage(p);
    return m;
}

const PackageIdSet &SwBuild::getKnownPackages() const
{
    return known_packages;
}

void SwBuild::addKnownPackage(const PackageId &id)
{
    known_packages.insert(id);
}

void SwBuild::resolvePackages()
{
    CHECK_STATE_AND_CHANGE(BuildState::TargetsToBuildSet, BuildState::PackagesResolved);

    // gather
    UnresolvedPackages upkgs;
    for (const auto &[pkg, tgts] : getTargetsToBuild())
    {
        for (const auto &tgt : tgts)
        {
            // for package id inputs we also load themselves
            auto pkg = tgt->getPackage();
            //                                skip checks
            if (pkg.getPath().isAbsolute() && !pkg.getPath().is_loc())
                upkgs.insert(pkg);
            break;
        }
    }
    for (const auto &[pkg, tgts] : getTargets())
    {
        for (const auto &tgt : tgts)
        {
            auto deps = tgt->getDependencies();
            for (auto &d : deps)
            {
                // filter out existing targets as they come from same module
                if (auto id = d->getUnresolvedPackage().toPackageId(); id && getTargets().find(*id) != getTargets().end())
                    continue;
                // filter out predefined targets
                if (swctx.getPredefinedTargets().find(d->getUnresolvedPackage().ppath) != swctx.getPredefinedTargets().end(d->getUnresolvedPackage().ppath))
                    continue;
                upkgs.insert(d->getUnresolvedPackage());
            }
            break; // take first as all deps are equal
        }
    }

    // install
    auto m = install(upkgs);

    // now we know all drivers
    std::set<Input *> iv;
    for (auto &[u, p] : m)
    {
        // use addInput to prevent doubling already existing and loaded inputs
        // like when we loading dependency that is already loaded from the input
        // test: sw build org.sw.demo.gnome.pango.pangocairo-1.44
        iv.insert(&swctx.addInput(p));
    }
    swctx.loadEntryPoints(iv, false);
}

void SwBuild::loadPackages()
{
    CHECK_STATE_AND_CHANGE(BuildState::PackagesResolved, BuildState::PackagesLoaded);

    loadPackages(swctx.getPredefinedTargets());
}

void SwBuild::loadPackages(const TargetMap &predefined)
{
    // first, we create all package ids with EPs in targets
    for (auto &[p, _] : swctx.getTargetData())
        targets[p];

    // load
    int r = 1;
    while (1)
    {
        LOG_TRACE(logger, "build id " << this << " " << BOOST_CURRENT_FUNCTION << " round " << r++);

        std::map<TargetSettings, std::pair<PackageId, TargetContainer *>> load;
        auto &chld = targets; // take a ref, because it won't be changed in this loop
        for (const auto &[pkg, tgts] : chld)
        {
            for (const auto &tgt : tgts)
            {
                auto deps = tgt->getDependencies();
                for (auto &d : deps)
                {
                    if (d->isResolved())
                        continue;

                    auto i = chld.find(d->getUnresolvedPackage());
                    if (i == chld.end())
                        throw SW_RUNTIME_ERROR(tgt->getPackage().toString() + ": No target loaded: " + d->getUnresolvedPackage().toString());

                    auto k = i->second.findSuitable(d->getSettings());
                    if (k != i->second.end())
                    {
                        d->setTarget(**k);
                        continue;
                    }

                    if (predefined.find(d->getUnresolvedPackage().ppath) != predefined.end(d->getUnresolvedPackage().ppath))
                    {
                        throw SW_LOGIC_ERROR(tgt->getPackage().toString() + ": predefined target is not resolved: " + d->getUnresolvedPackage().toString());
                    }

                    load.insert({ d->getSettings(), { i->first, &i->second } });
                }
            }
        }
        if (load.empty())
            break;
        bool loaded = false;
        for (auto &[s, d] : load)
        {
            // empty settings mean we want dependency only to be present
            if (s.empty())
                continue;

            LOG_TRACE(logger, "build id " << this << " " << BOOST_CURRENT_FUNCTION << " loading " << d.first.toString());

            loaded = true;

            auto tgts = swctx.getTargetData(d.first).loadPackages(*this, s, known_packages);
            //swctx.getTargetData(d.first).loadPackages(*this, s, { d.first });
            for (auto &tgt : tgts)
            {
                if (tgt->getSettings()["dry-run"] == "true")
                    continue;
                getTargets()[tgt->getPackage()].push_back(tgt);
            }

            auto k = d.second->findSuitable(s);
            if (k == d.second->end())
            {
                throw SW_RUNTIME_ERROR("cannot load package " + d.first.toString() + " with current settings\n" + s.toString());
            }
        }
        if (!loaded)
            break;
    }
}

bool SwBuild::prepareStep()
{
    std::atomic_bool next_pass = false;

    auto &e = getExecutor();
    Futures<void> fs;
    for (const auto &[pkg, tgts] : getTargets())
    {
        for (const auto &tgt : tgts)
        {
            fs.push_back(e.push([tgt, &next_pass]
            {
                if (tgt->prepare())
                    next_pass = true;
            }));
        }
    }
    waitAndGet(fs);

    return next_pass;
}

void SwBuild::setTargetsToBuild()
{
    CHECK_STATE_AND_CHANGE(BuildState::InputsLoaded, BuildState::TargetsToBuildSet);

    // mark existing targets as targets to build
    // only in case if not present?
    if (targets_to_build.empty())
        targets_to_build = getTargets();
    for (auto &[pkg, d] : swctx.getPredefinedTargets())
        targets_to_build.erase(pkg.getPath());
}

void SwBuild::prepare()
{
    CHECK_STATE_AND_CHANGE(BuildState::PackagesLoaded, BuildState::Prepared);

    while (prepareStep())
        ;
}

void SwBuild::execute() const
{
    auto p = getExecutionPlan();
    execute(p);
}

void SwBuild::execute(ExecutionPlan &p) const
{
    CHECK_STATE_AND_CHANGE(BuildState::Prepared, BuildState::Executed);

    p.build_always |= build_always;
    p.write_output_to_file |= cl_write_output_to_file;
    p.skip_errors = skip_errors.getValue();

    //ScopedTime t;
    p.execute(getExecutor());
    /*auto t2 = t.getTimeFloat();
    if (!silent && t2 > 0.15)
        LOG_INFO(logger, "Build time: " << t2 << " s.");*/

    if (time_trace)
        p.saveChromeTrace(getBuildDirectory() / "misc" / "time_trace.json");
}

Commands SwBuild::getCommands() const
{
    // calling this for all targets in any case to set proper command dependencies
    for (const auto &[pkg, tgts] : getTargets())
    {
        for (auto &tgt : tgts)
        {
            for (auto &c : tgt->getCommands())
                c->maybe_unused = builder::Command::MU_TRUE; // why?
        }
    }

    if (targets_to_build.empty())
        throw SW_RUNTIME_ERROR("no targets were selected for building");

    Commands cmds;
    for (auto &[p, tgts] : targets_to_build)
    {
        // one target may be loaded twice
        // we take only the latest, because it is has correct set of command deps
        std::map<TargetSettings, ITarget*> latest_targets;
        for (auto &tgt : tgts)
            latest_targets[tgt->getSettings()] = tgt.get();

        for (auto &[_, tgt] : latest_targets)
        {
            auto c = tgt->getCommands();
            for (auto &c2 : c)
            {
                c2->maybe_unused &= ~builder::Command::MU_TRUE;
                c2->show_output = cl_show_output || cl_write_output_to_file; // only for selected targets
            }
            cmds.insert(c.begin(), c.end());

            // copy output dlls

            /*auto nt = t->as<NativeCompiledTarget*>();
            if (!nt)
                continue;
            if (*nt->HeaderOnly)
                continue;
            if (nt->getSelectedTool() == nt->Librarian.get())
                continue;

            // copy
            if (nt->isLocal() && //getSettings().Native.CopySharedLibraries &&
                nt->Scope == TargetScope::Build && nt->OutputDir.empty() && !nt->createWindowsRpath())
            {
                for (auto &l : nt->gatherAllRelatedDependencies())
                {
                    auto dt = l->as<NativeCompiledTarget*>();
                    if (!dt)
                        continue;
                    if (dt->isLocal())
                        continue;
                    if (dt->HeaderOnly.value())
                        continue;
                    if (dt->getSettings().Native.LibrariesType != LibraryType::Shared && !dt->isSharedOnly())
                        continue;
                    if (dt->getSelectedTool() == dt->Librarian.get())
                        continue;
                    auto in = dt->getOutputFile();
                    auto o = nt->getOutputDir() / dt->OutputDir;
                    o /= in.filename();
                    if (in == o)
                        continue;

                    SW_MAKE_EXECUTE_BUILTIN_COMMAND(copy_cmd, *nt, "sw_copy_file", nullptr);
                    copy_cmd->arguments.push_back(in.u8string());
                    copy_cmd->arguments.push_back(o.u8string());
                    copy_cmd->addInput(dt->getOutputFile());
                    copy_cmd->addOutput(o);
                    copy_cmd->dependencies.insert(nt->getCommand());
                    copy_cmd->name = "copy: " + normalize_path(o);
                    copy_cmd->maybe_unused = builder::Command::MU_ALWAYS;
                    copy_cmd->command_storage = builder::Command::CS_LOCAL;
                    cmds.insert(copy_cmd);
                }
            }*/
        }
    }

    return cmds;
}

ExecutionPlan SwBuild::getExecutionPlan() const
{
    return getExecutionPlan(getCommands());
}

ExecutionPlan SwBuild::getExecutionPlan(const Commands &cmds) const
{
    auto ep = ExecutionPlan::createExecutionPlan(cmds);
    if (ep)
        return ep;

    // error!

    /*auto d = getServiceDir();

    auto [g, n, sc] = ep.getStrongComponents();

    using Subgraph = boost::subgraph<CommandExecutionPlan::Graph>;

    // fill copy of g
    Subgraph root(g.m_vertices.size());
    for (auto &e : g.m_edges)
        boost::add_edge(e.m_source, e.m_target, root);

    std::vector<Subgraph*> subs(n);
    for (decltype(n) i = 0; i < n; i++)
        subs[i] = &root.create_subgraph();
    for (int i = 0; i < sc.size(); i++)
        boost::add_vertex(i, *subs[sc[i]]);

    auto cyclic_path = d / "cyclic";
    fs::create_directories(cyclic_path);
    for (decltype(n) i = 0; i < n; i++)
    {
        if (subs[i]->m_graph.m_vertices.size() > 1)
            CommandExecutionPlan::printGraph(subs[i]->m_graph, cyclic_path / std::to_string(i));
    }

    ep.printGraph(ep.getGraph(), cyclic_path / "processed", ep.commands, true);
    ep.printGraph(ep.getGraphUnprocessed(), cyclic_path / "unprocessed", ep.unprocessed_commands, true);*/

    //String error = "Cannot create execution plan because of cyclic dependencies: strong components = " + std::to_string(n);
    String error = "Cannot create execution plan because of cyclic dependencies";

    throw SW_RUNTIME_ERROR(error);
}

String SwBuild::getHash() const
{
    String s;
    for (auto &i : inputs)
        s += i.getHash();
    return shorten_hash(blake2b_512(s), 8);
}

void SwBuild::addInput(const InputWithSettings &i)
{
    inputs.push_back(i);
}

path SwBuild::getExecutionPlanPath() const
{
    const auto ext = ".swb"; // sw build
    return getBuildDirectory() / "ep" / getHash() += ext;
}

void SwBuild::saveExecutionPlan() const
{
    saveExecutionPlan(getExecutionPlanPath());
}

void SwBuild::runSavedExecutionPlan() const
{
    CHECK_STATE(BuildState::InputsLoaded);

    runSavedExecutionPlan(getExecutionPlanPath());
}

void SwBuild::saveExecutionPlan(const path &in) const
{
    CHECK_STATE(BuildState::Prepared);

    auto p = getExecutionPlan();
    p.save(in);
}

void SwBuild::runSavedExecutionPlan(const path &in) const
{
    auto p = ExecutionPlan::load(in, getContext());

    // change state
    overrideBuildState(BuildState::Prepared);
    SCOPE_EXIT
    {
        // fallback
        overrideBuildState(BuildState::InputsLoaded);
    };

    execute(p);
}

std::vector<InputWithSettings> SwBuild::getInputs() const
{
    return inputs;
}

}
