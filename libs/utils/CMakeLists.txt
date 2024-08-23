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
  src/linglong/utils/common.cpp
  src/linglong/utils/common.h
  src/linglong/utils/debug/debug.cpp
  src/linglong/utils/debug/debug.h
  src/linglong/utils/json.h
  src/linglong/utils/logger.cpp
  src/linglong/utils/logger.h
  src/linglong/utils/macro.h
  src/linglong/utils/oci_runtime.h
  src/linglong/utils/platform.cpp
  src/linglong/utils/platform.h
  src/linglong/utils/util.h
  COMPILE_FEATURES
  PUBLIC
  cxx_std_17
  LINK_LIBRARIES
  PUBLIC
  nlohmann_json::nlohmann_json
  )
