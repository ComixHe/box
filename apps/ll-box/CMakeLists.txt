# SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

pfl_add_executable(
  SOURCES
  ./src/main.cpp
  OUTPUT_NAME
  ${BOX_BIN_NAME}
  LINK_LIBRARIES
  PUBLIC
  PkgConfig::SECCOMP
  box::container
  box::utils)
