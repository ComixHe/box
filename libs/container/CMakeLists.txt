# SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

pfl_add_library(
  MERGED_HEADER_PLACEMENT
  DISABLE_INSTALL
  LIBRARY_TYPE
  STATIC
  SOURCES
  # find -regex '\.\/*.+\.[ch]\(pp\)?\(.in\)?' -type f -printf '%P\n'| sort
  src/linglong/container/container.cpp
  src/linglong/container/container.h
  src/linglong/container/helper.cpp
  src/linglong/container/helper.h
  src/linglong/container/host_mount.cpp
  src/linglong/container/host_mount.h
  src/linglong/container/seccomp.cpp
  src/linglong/container/seccomp_p.h
  COMPILE_FEATURES
  PUBLIC
  cxx_std_17
  LINK_LIBRARIES
  PRIVATE
  PkgConfig::SECCOMP
  PUBLIC
  box::utils
  ocppi::ocppi
  stdc++fs
  )
