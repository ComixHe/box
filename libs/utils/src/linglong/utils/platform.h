/*
 * SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef LINGLONG_BOX_SRC_UTIL_PLATFORM_H_
#define LINGLONG_BOX_SRC_UTIL_PLATFORM_H_

#include <sys/mman.h>

#include <optional>
#include "linglong/utils/common.h"


constexpr auto kStackSize = (1024 * 1024);

namespace linglong::utils {

int PlatformClone(int (*callback)(void *), int flags, void *arg);
int Exec(const str_vec &args,
         std::optional<std::vector<std::string>> env_list);
int WaitAllUntil(int pid);

}  // namespace linglong::util

#endif /* LINGLONG_BOX_SRC_UTIL_PLATFORM_H_ */
