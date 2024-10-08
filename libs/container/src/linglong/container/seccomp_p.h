/*
 * SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef LINGLONG_BOX_SRC_CONTAINER_SECCOMP_H_
#define LINGLONG_BOX_SRC_CONTAINER_SECCOMP_H_

#include "linglong/utils/oci_runtime.h"

namespace linglong::container {
int ConfigSeccomp(const std::optional<linglong::utils::Seccomp> &seccomp);
}

#endif /* LINGLONG_BOX_SRC_CONTAINER_SECCOMP_H_ */
