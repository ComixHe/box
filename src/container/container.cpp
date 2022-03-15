/*
 * Copyright (c) 2020-2021. Uniontech Software Ltd. All rights reserved.
 *
 * Author:     Iceyer <me@iceyer.net>
 *
 * Maintainer: Iceyer <me@iceyer.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "container.h"

#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <grp.h>
#include <sched.h>
#include <unistd.h>

#include <cerrno>
#include <utility>

#include "util/logger.h"
#include "util/filesystem.h"
#include "util/semaphore.h"
#include "util/debug/debug.h"
#include "util/platform.h"

#include "container/seccomp.h"
#include "container/container_option.h"
#include "container/mount/host_mount.h"
#include "container/mount/filesystem_driver.h"

namespace linglong {

static const std::string ll_dbus_proxy_bin = "/usr/bin/ll-dbus-proxy";
// start dbus proxy
static int StartDbusProxy(const Runtime &r)
{
    if (!(r.annotations.has_value() && r.annotations->dbus_proxy_info.has_value()
          && r.annotations->dbus_proxy_info->enable)) {
        logInf() << "dbus proxy disabled";
        return -1;
    }

    const auto &info = r.annotations->dbus_proxy_info;

    std::string socket_path = info->proxy_path;

    pid_t proxy_pid = fork();
    if (proxy_pid < 0) {
        logErr() << "fork to start dbus proxy failed:", util::errnoString();
        return -1;
    }

    if (0 == proxy_pid) {
        // FIXME: parent may dead before this return.
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        std::string bus_type = info->bus_type;
        std::string app_id = info->app_id;
        std::vector<std::string> name_fliter = info->name;
        std::vector<std::string> path_fliter = info->path;
        std::vector<std::string> interface_fliter = info->interface;
        std::string name_fliter_string = linglong::util::str_vec_join(name_fliter, ',');
        std::string path_fliter_string = linglong::util::str_vec_join(path_fliter, ',');
        std::string interface_fliter_string = linglong::util::str_vec_join(interface_fliter, ',');
        char const *const args[] = {ll_dbus_proxy_bin.c_str(),
                                    app_id.c_str(),
                                    bus_type.c_str(),
                                    socket_path.c_str(),
                                    name_fliter_string.c_str(),
                                    path_fliter_string.c_str(),
                                    interface_fliter_string.c_str(),
                                    NULL};
        int ret = execvp(args[0], (char **)args);
        logErr() << "start dbus proxy failed, ret=" << ret;
        exit(ret);
    } else {
        // FIXME: this call make 10ms lag at least
        if (util::fs::path(socket_path).wait_until_exsit() != 0) {
            logErr() << util::format("timeout! socketPath [\"%s\"] not exsit", socket_path.c_str());
            return -1;
        }
    }
    return 0;
}

int DropToNormalUser(int uid, int gid)
{
    setuid(uid);
    seteuid(uid);
    setgid(gid);
    setegid(gid);
    return 0;
}

static int ConfigUserNamespace(const linglong::Linux &linux, int initPid)
{
    std::string pid = "self";
    if (initPid > 0) {
        pid = util::format("%d", initPid);
    }

    logDbg() << "write uid_map and pid_map" << initPid;

    // write uid map
    std::ofstream uidMapFile(util::format("/proc/%s/uid_map", pid.c_str()));
    for (auto const &idMap : linux.uidMappings) {
        uidMapFile << util::format("%lu %lu %lu\n", idMap.containerID, idMap.hostID, idMap.size);
    }
    uidMapFile.close();

    // write gid map
    auto setgroupsPath = util::format("/proc/%s/setgroups", pid.c_str());
    std::ofstream setgroupsFile(setgroupsPath);
    setgroupsFile << "deny";
    setgroupsFile.close();

    std::ofstream gidMapFile(util::format("/proc/%s/gid_map", pid.c_str()));
    for (auto const &idMap : linux.gidMappings) {
        gidMapFile << util::format("%lu %lu %lu\n", idMap.containerID, idMap.hostID, idMap.size);
    }
    gidMapFile.close();

    logDbg() << "finish write uid_map and pid_map";
    return 0;
}

// FIXME(iceyer): not work now
static int ConfigCgroupV2(const std::string &cgroupsPath, const linglong::Resources &res, int initPid)
{
    if (cgroupsPath.empty()) {
        logWan() << "skip with empty cgroupsPath";
        return 0;
    }

    auto writeConfig = [](const std::map<std::string, std::string> &cfgs) {
        for (auto const &cfg : cfgs) {
            logWan() << "ConfigCgroupV2" << cfg.first << cfg.second;
            std::ofstream cfgFile(cfg.first);
            cfgFile << cfg.second << std::endl;
            cfgFile.close();
        }
    };

    auto cgroupsRoot = util::fs::path(cgroupsPath);
    util::fs::create_directories(cgroupsRoot, 0755);

    int ret = mount("cgroup2", cgroupsRoot.string().c_str(), "cgroup2", 0u, nullptr);
    if (ret != 0) {
        logErr() << "mount cgroup failed" << util::RetErrString(ret);
        return -1;
    }

    // TODO: should sub path with pid?
    auto subCgroupRoot = cgroupsRoot / "ll-box";
    if (!util::fs::create_directories(util::fs::path(subCgroupRoot), 0755)) {
        logErr() << "create_directories subCgroupRoot failed" << util::errnoString();
        return -1;
    }

    auto subCgroupPath = [subCgroupRoot](const std::string &compents) -> std::string {
        return (subCgroupRoot / compents).string();
    };

    // config memory
    const auto memMax = res.memory.limit;

    if (memMax > 0) {
        const auto memSwapMax = res.memory.swap - memMax;
        const auto memLow = res.memory.reservation;
        writeConfig({
            {subCgroupPath("memory.max"), util::format("%d", memMax)},
            {subCgroupPath("memory.swap.max"), util::format("%d", memSwapMax)},
            {subCgroupPath("memory.low"), util::format("%d", memLow)},
        });
    }

    // config cpu
    const auto cpuPeriod = res.cpu.period;
    const auto cpuMax = res.cpu.quota;
    // convert from [2-262144] to [1-10000], 262144 is 2^18
    const auto cpuWeight = (1 + ((res.cpu.shares - 2) * 9999) / 262142);

    writeConfig({
        {subCgroupPath("cpu.max"), util::format("%d %d", cpuMax, cpuPeriod)},
        {subCgroupPath("cpu.weight"), util::format("%d", cpuWeight)},
    });

    // config pid
    writeConfig({
        {subCgroupPath("cgroup.procs"), util::format("%d", initPid)},
    });

    logDbg() << "move" << initPid << "to new cgroups";

    return 0;
}

struct ContainerPrivate {
public:
    ContainerPrivate(Runtime r, Container * /*parent*/)
        : host_root_(r.root.path)
        , r(std::move(r))
        , native_mounter_(new HostMount)
        , overlayfs_mounter_(new HostMount)
        , fuseproxy_mounter_(new HostMount)
    {
    }

    int sem_id = -1;

    std::string host_root_;

    Runtime r;

    //    bool use_delay_new_user_ns = false;
    bool use_new_cgroup_ns = false;
    bool clone_new_pid_ = false;

    Option opt;

    uid_t host_uid_ = -1;
    gid_t host_gid_ = -1;

    std::unique_ptr<HostMount> native_mounter_;
    std::unique_ptr<HostMount> overlayfs_mounter_;
    std::unique_ptr<HostMount> fuseproxy_mounter_;

    HostMount *container_mounter_ = nullptr;

public:
    static int DropPermissions()
    {
        __gid_t newgid[1] = {getgid()};
        auto newuid = getuid();
        auto olduid = geteuid();

        if (0 == olduid) {
            setgroups(1, newgid);
        }
        seteuid(newuid);
        return 0;
    }

    int PrepareLinks() const
    {
        chdir("/");

        if (opt.link_lfs) {
            symlink("/usr/bin", "/bin");
            symlink("/usr/lib", "/lib");
            symlink("/usr/lib32", "/lib32");
            symlink("/usr/lib64", "/lib64");
            symlink("/usr/libx32", "/libx32");
        }

        // default link
        symlink("/proc/kcore", "/dev/core");
        symlink("/proc/self/fd", "/dev/fd");
        symlink("/proc/self/fd/2", "/dev/stderr");
        symlink("/proc/self/fd/0", "/dev/stdin");
        symlink("/proc/self/fd/1", "/dev/stdout");

        return 0;
    }

    int PrepareDefaultDevices() const
    {
        struct Device {
            std::string path;
            mode_t mode;
            dev_t dev;
        };

        std::vector<Device> list = {
            {"/dev/null", S_IFCHR | 0666, makedev(1, 3)},    {"/dev/zero", S_IFCHR | 0666, makedev(1, 5)},
            {"/dev/full", S_IFCHR | 0666, makedev(1, 7)},    {"/dev/random", S_IFCHR | 0666, makedev(1, 8)},
            {"/dev/urandom", S_IFCHR | 0666, makedev(1, 9)}, {"/dev/tty", S_IFCHR | 0666, makedev(5, 0)},
        };

        // TODO(iceyer): not work now
        if (!opt.rootless) {
            for (auto const &dev : list) {
                auto path = host_root_ + dev.path;
                int ret = mknod(path.c_str(), dev.mode, dev.dev);
                if (0 != ret) {
                    logErr() << "mknod" << path << dev.mode << dev.dev << "failed with" << util::RetErrString(ret);
                }
                chmod(path.c_str(), dev.mode | 0xFFFF);
                chown(path.c_str(), 0, 0);
            }
        } else {
            for (auto const &dev : list) {
                Mount m;
                m.destination = dev.path;
                m.type = "bind";
                m.data = std::vector<std::string> {};
                m.flags = MS_BIND;
                m.fsType = Mount::Bind;
                m.source = dev.path;

                this->container_mounter_->MountNode(m);
            }
        }

        // FIXME(iceyer): /dev/console is set up if terminal is enabled in the config by bind mounting the
        // pseudoterminal slave to /dev/console.
        symlink("/dev/ptmx", "/dev/pts/ptmx");

        return 0;
    }

    int PivotRoot() const
    {
        int ret = -1;
        chdir(host_root_.c_str());

        if (opt.rootless && r.annotations->overlayfs.has_value()) {
            int flag = MS_MOVE;
            ret = mount(".", "/", nullptr, flag, nullptr);
            if (0 != ret) {
                logErr() << "mount / failed" << util::RetErrString(ret);
                return -1;
            }

            ret = chroot(".");
            if (0 != ret) {
                logErr() << "chroot . failed" << util::RetErrString(ret);
                return -1;
            }
        } else {
            int flag = MS_BIND | MS_REC;
            ret = mount(".", ".", "bind", flag, nullptr);
            if (0 != ret) {
                logErr() << "mount / failed" << util::RetErrString(ret);
                return -1;
            }

            auto ll_host_filename = "ll-host";

            auto ll_host_path = host_root_ + "/" + ll_host_filename;

            mkdir(ll_host_path.c_str(), 0755);

            ret = syscall(SYS_pivot_root, host_root_.c_str(), ll_host_path.c_str());
            if (0 != ret) {
                logErr() << "SYS_pivot_root failed" << host_root_ << util::errnoString() << errno << ret;
                return -1;
            }

            chdir("/");
            ret = chroot(".");
            if (0 != ret) {
                logErr() << "chroot failed" << host_root_ << util::errnoString() << errno;
                return -1;
            }

            chdir("/");
            umount2(ll_host_filename, MNT_DETACH);
        }

        return 0;
    }

    int PrepareRootfs()
    {
        auto PrepareOverlayfsRootfs = [&](const AnnotationsOverlayfs &overlayfs) -> int {
            native_mounter_->Setup(new NativeFilesystemDriver(overlayfs.lower_parent));

            util::str_vec lower_dirs = {};
            int prefix_index = 0;
            for (auto m : overlayfs.mounts) {
                auto prefix = util::fs::path(util::format("/%d", prefix_index));
                m.destination = (prefix / m.destination).string();
                if (0 == native_mounter_->MountNode(m)) {
                    lower_dirs.push_back((util::fs::path(overlayfs.lower_parent) / prefix).string());
                }
                ++prefix_index;
            }

            overlayfs_mounter_->Setup(
                new OverlayfsFuseFilesystemDriver(lower_dirs, overlayfs.upper, overlayfs.workdir, host_root_));

            container_mounter_ = overlayfs_mounter_.get();
            return -1;
        };

        auto PrepareFuseProxyRootfs = [&](const AnnotationsOverlayfs &overlayfs) -> int {
            native_mounter_->Setup(new NativeFilesystemDriver(overlayfs.lower_parent));

            util::str_vec mounts = {};
            for (auto const &m : overlayfs.mounts) {
                auto mount_item = util::format("%s:%s\n", m.source.c_str(), m.destination.c_str());
                mounts.push_back(mount_item);
            }

            fuseproxy_mounter_->Setup(new FuseProxyFilesystemDriver(mounts, host_root_));

            container_mounter_ = fuseproxy_mounter_.get();
            return -1;
        };

        auto PrepareNativeRootfs = [&](const AnnotationsNativeRootfs &native) -> int {
            native_mounter_->Setup(new NativeFilesystemDriver(r.root.path));

            for (auto m : native.mounts) {
                native_mounter_->MountNode(m);
            }

            container_mounter_ = native_mounter_.get();
            return -1;
        };

        if (r.annotations.has_value() && r.annotations->overlayfs.has_value()) {
            clone_new_pid_ = true;
            auto env = getenv("LL_BOX_FS_BACKEND");
            if (env && std::string(env) == "fuse-proxy") {
                return PrepareFuseProxyRootfs(r.annotations->overlayfs.value());
            } else {
                return PrepareOverlayfsRootfs(r.annotations->overlayfs.value());
            }
        } else {
            return PrepareNativeRootfs(r.annotations->native.value());
        }

        return -1;
    }

    int MountContainerPath()
    {
        if (r.mounts.has_value()) {
            for (auto const &m : r.mounts.value()) {
                container_mounter_->MountNode(m);
            }
        };

        return 0;
    }
};

int HookExec(const Hook &hook)
{
    int execPid = fork();
    if (execPid < 0) {
        logErr() << "fork failed" << util::RetErrString(execPid);
        return -1;
    }

    if (execPid == 0) {
        util::str_vec argStrVec;
        argStrVec.push_back(hook.path);

        std::copy(hook.args->begin(), hook.args->end(), std::back_inserter(argStrVec));

        util::Exec(argStrVec, hook.env.value());

        exit(0);
    }

    return waitpid(execPid, nullptr, 0);
}

int NonePrivilegeProc(void *arg)
{
    auto &c = *reinterpret_cast<ContainerPrivate *>(arg);

    if (c.clone_new_pid_) {
        auto ret = mount("proc", "/proc", "proc", 0, nullptr);
        if (0 != ret) {
            logErr() << "mount proc failed" << util::RetErrString(ret);
            return -1;
        }
    }

    if (c.opt.rootless) {
        // TODO(iceyer): use option

        auto unshare_flags = CLONE_NEWUSER;
        if (unshare_flags) {
            auto ret = unshare(unshare_flags);
            if (0 != ret) {
                logErr() << "unshare failed" << unshare_flags << util::RetErrString(ret);
            }
        }

        logDbg() << "wait switch to normal user" << c.sem_id;
        Semaphore noPrivilegeSem(c.sem_id);
        noPrivilegeSem.vrijgeven();
        noPrivilegeSem.passeren();
        logDbg() << "wait switch to normal user end";
    }

    if (c.r.hooks.has_value() && c.r.hooks->prestart.has_value()) {
        for (auto const &preStart : *c.r.hooks->prestart) {
            HookExec(preStart);
        }
    }

    if (!c.opt.rootless) {
        seteuid(0);
        // todo: check return value
        ConfigSeccomp(c.r.linux.seccomp);
        ContainerPrivate::DropPermissions();
    }

    // FIXME: parent may dead before this return.
    prctl(PR_SET_PDEATHSIG, SIGKILL);

    logDbg() << "c.r.process.args:" << c.r.process.args;
    chdir(c.r.process.cwd.c_str());

    int pid = fork();
    if (pid < 0) {
        logErr() << "fork failed" << util::RetErrString(pid);
        return -1;
    }

    if (0 == pid) {
        for (auto env : c.r.process.env) {
            auto kv = util::str_spilt(env, "=");
            if (kv.size() > 1 && kv.at(0) == "PATH") {
                setenv("PATH", kv.at(1).c_str(), 1);
            }
        }
        auto ret = util::Exec(c.r.process.args, c.r.process.env);
        if (0 != ret) {
            logErr() << "execve failed" << util::RetErrString(ret);
        }
        exit(ret);
    }

    util::WaitAll();
    return 0;
}

int EntryProc(void *arg)
{
    auto &c = *reinterpret_cast<ContainerPrivate *>(arg);

    Semaphore s(c.sem_id);

    if (c.opt.rootless) {
        s.vrijgeven();
        s.passeren();
    }

    // FIXME: change HOSTNAME will broken XAUTH
    auto new_hostname = c.r.hostname;
    //    if (sethostname(new_hostname.c_str(), strlen(new_hostname.c_str())) == -1) {
    //        logErr() << "sethostname failed" << util::errnoString();
    //        return -1;
    //    }

    uint32_t flags = MS_REC | MS_SLAVE;
    int ret = mount(nullptr, "/", nullptr, flags, nullptr);
    if (0 != ret) {
        logErr() << "mount / failed" << util::RetErrString(ret);
        return -1;
    }

    std::string container_root = c.r.annotations->container_root_path;
    flags = MS_NODEV | MS_NOSUID;
    ret = mount("tmpfs", container_root.c_str(), "tmpfs", flags, nullptr);
    if (0 != ret) {
        logErr() << util::format("mount container root (%s) failed:", container_root.c_str())
                 << util::RetErrString(ret);
        return -1;
    }

    // NOTE(iceyer): it's not standard oci action
    c.PrepareRootfs();

    c.MountContainerPath();

    if (c.use_new_cgroup_ns) {
        ConfigCgroupV2(c.r.linux.cgroupsPath, c.r.linux.resources, getpid());
    }

    c.PrepareDefaultDevices();

    c.PivotRoot();

    c.PrepareLinks();

    if (!c.opt.rootless) {
        auto unshare_flags = 0;
        // TODO(iceyer): no need user namespace in setuid
        //        if (c.use_delay_new_user_ns) {
        //            unshare_flags |= CLONE_NEWUSER;
        //        }
        if (c.use_new_cgroup_ns) {
            unshare_flags |= CLONE_NEWCGROUP;
        }
        if (unshare_flags) {
            ret = unshare(unshare_flags);
            if (0 != ret) {
                logErr() << "unshare failed" << unshare_flags << util::RetErrString(ret);
            }
        }
        //        if (c.use_delay_new_user_ns) {
        //            s.vrijgeven();
        //            s.passeren();
        //        }
    }

    c.sem_id = getpid();
    Semaphore none_privilege_sem(c.sem_id);
    none_privilege_sem.init();

    int none_privilege_proc_flag = SIGCHLD | CLONE_NEWNS;
    if (c.clone_new_pid_) {
        none_privilege_proc_flag |= CLONE_NEWPID;
    }

    int noPrivilegePid = util::PlatformClone(NonePrivilegeProc, none_privilege_proc_flag, arg);
    if (noPrivilegePid < 0) {
        logErr() << "clone failed" << util::RetErrString(noPrivilegePid);
        return -1;
    }

    if (c.opt.rootless) {
        none_privilege_sem.passeren();

        Linux linux;
        IDMap id_map;

        id_map.containerID = c.host_uid_;
        id_map.hostID = 0;
        id_map.size = 1;
        linux.uidMappings.push_back(id_map);

        id_map.containerID = c.host_gid_;
        id_map.hostID = 0;
        id_map.size = 1;
        linux.gidMappings.push_back(id_map);

        logDbg() << "switch to normal user" << noPrivilegePid;
        ConfigUserNamespace(linux, noPrivilegePid);

        none_privilege_sem.vrijgeven();
        logDbg() << "switch to normal end" << noPrivilegePid;
    }

    ContainerPrivate::DropPermissions();

    // FIXME: parent may dead before this return.
    prctl(PR_SET_PDEATHSIG, SIGKILL);

    // FIXME(interactive bash): if need keep interactive shell

    if (c.clone_new_pid_) {
        util::WaitAllUntil(noPrivilegePid);
    } else {
        // NOTE: if third-level box do not new pid ns, the init process of app is second-level box, so we need to wait
        // all here
        util::WaitAll();
    };
    return 0;
}

Container::Container(const Runtime &r)
    : dd_ptr(new ContainerPrivate(r, this))
{
}

int Container::Start(const Option &opt)
{
    auto &c = *reinterpret_cast<ContainerPrivate *>(dd_ptr.get());
    c.opt = opt;

    if (opt.rootless) {
        c.host_uid_ = geteuid();
        c.host_gid_ = getegid();
    }

    int flags = SIGCHLD | CLONE_NEWNS;

    for (auto const &n : dd_ptr->r.linux.namespaces) {
        switch (n.type) {
        case CLONE_NEWIPC:
        case CLONE_NEWUTS:
        case CLONE_NEWNS:
        case CLONE_NEWPID:
        case CLONE_NEWNET:
            flags |= n.type;
            break;
        case CLONE_NEWUSER:
            //            dd_ptr->use_delay_new_user_ns = true;
            break;
        case CLONE_NEWCGROUP:
            dd_ptr->use_new_cgroup_ns = true;
            break;
        default:
            return -1;
        }
    }

    if (opt.rootless) {
        flags |= CLONE_NEWUSER;
    }

    c.sem_id = getpid();
    Semaphore sem(c.sem_id);
    sem.init();

    int entry_pid = util::PlatformClone(EntryProc, flags, (void *)dd_ptr.get());
    if (entry_pid < 0) {
        logErr() << "clone failed" << util::RetErrString(entry_pid);
        return -1;
    }

    // FIXME: maybe we need c.opt.child_need_wait?

    logDbg() << "wait child Start" << entry_pid;
    sem.passeren();
    if (/*c.use_delay_new_user_ns ||*/ opt.rootless) {
        ConfigUserNamespace(c.r.linux, entry_pid);
    }
    StartDbusProxy(c.r);
    sem.vrijgeven();
    logDbg() << "wait child end";

    ContainerPrivate::DropPermissions();

    // FIXME: parent may dead before this return.
    prctl(PR_SET_PDEATHSIG, SIGKILL);

    // FIXME(interactive bash): if need keep interactive shell
    util::WaitAllUntil(entry_pid);

    return 0;
}

Container::~Container() = default;

} // namespace linglong
