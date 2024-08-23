/*
 * SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "linglong/utils/debug/debug.h"

#include <dirent.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include "linglong/utils/logger.h"

namespace linglong::utils::debug {

#define DUMP_DBG(func, line) /*NOLINT*/ \
  (linglong::utils::Logger(linglong::utils::Logger::Debug, func, line))

void DumpFileInfo(const std::string &path) {
  DumpFileInfo1(path, __FUNCTION__, __LINE__);
}

void DumpFileInfo1(const std::string &path, const char *func, int line) {
  using stat = struct stat;
  stat st{};  // NOLINT

  auto ret = lstat(path.c_str(), &st);
  if (0 != ret) {
    DUMP_DBG(func, line) << path << RetErrString(ret);
  } else {
    DUMP_DBG(func, line) << path << st.st_uid << st.st_gid
                         << ((st.st_mode & S_IFMT) == S_IFDIR);
  }
}

}  // namespace linglong::utils::debug
