/*
 * SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "linglong/utils/platform.h"

#include <sched.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>

#include "logger.h"

namespace linglong::utils {

int PlatformClone(int (*callback)(void *), int flags, void *arg) {
  auto *stack = reinterpret_cast<char *>(
      mmap(nullptr, kStackSize, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0));
  if (stack == MAP_FAILED) {
    return EXIT_FAILURE;
  }

  auto *stackTop = stack + kStackSize;
  return ::clone(callback, stackTop, flags, arg);
}

int Exec(const str_vec &args,
         std::optional<std::vector<std::string>> env_list) {
  auto targetArgc = args.size();
  std::vector<const char *> targetArgv;
  for (decltype(targetArgc) i = 0; i < targetArgc; i++) {
    targetArgv.push_back(args[i].c_str());
  }
  targetArgv.push_back(nullptr);

  auto targetEnvc = env_list.has_value() ? env_list->size() : 0;
  std::vector<const char *> targetEnvv;

  for (decltype(targetEnvc) i = 0; i < targetEnvc; i++) {
    targetEnvv.push_back(env_list.value().at(i).c_str());
  }
  targetEnvv.push_back(nullptr);

  logDbg() << "execve" << targetArgv[0] << " in pid:" << getpid();

  int ret = execvpe(targetArgv[0], const_cast<char **>(targetArgv.data()),
                    const_cast<char **>(targetEnvv.data()));

  return ret;
}

// if wstatus says child exit normally, return true else false
static bool parse_wstatus(const int &wstatus, std::string &info) {
  if (WIFEXITED(wstatus)) {
    auto code = WEXITSTATUS(wstatus);
    info = format("exited with code %d", code);
    return code == 0;
  } else if (WIFSIGNALED(wstatus)) {
    info = format("terminated by signal %d", WTERMSIG(wstatus));
    return false;
  } else {
    info = format("is dead with wstatus=%d", wstatus);
    return false;
  }
}

// call waitpid with pid until waitpid return value equals to target or all
// child exited
static int DoWait(const int pid, int target = 0) {
  logDbg() << format("DoWait called with pid=%d, target=%d", pid, target);
  int wstatus{-1};
  while (int child = waitpid(pid, &wstatus, 0)) {
    if (child > 0) {
      std::string info;
      auto normal = parse_wstatus(wstatus, info);
      info = format("child [%d] [%s].", child, info.c_str());
      if (normal) {
        logDbg() << info;
      } else {
        logWan() << info;
      }
      if (child == target || child == pid) {
        // this will never happen when target <= 0
        logDbg() << "wait done";
        return normal ? 0 : -1;
      }
    } else if (child < 0) {
      if (errno == ECHILD) {
        logDbg() << format("no child to wait");
        return -1;
      }

      auto string = errnoString();
      logErr() << format("waitpid failed, %s", string.c_str());
      return -1;
    }
  }
  // when we pass options=0 to waitpid, it will never return 0
  logWan() << "waitpid return 0, this should not happen normally";
  return -1;
}

// wait all child until pid exit
int WaitAllUntil(const int pid) { return DoWait(-1, pid); }

}  // namespace linglong::utils
