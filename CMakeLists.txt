# SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.11.4)

project(
  box
  VERSION 1.4.0
  DESCRIPTION "box is the container application toolkit of linyaps"
  HOMEPAGE_URL https://github.com/OpenAtom-Linyaps/box
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED)
set(BOX_BIN_NAME "ll-box")

option(STATIC_BOX "Build ll-box staticlly" OFF)
option(ENABLE_CPM "Use CPM" ON)

if(${STATIC_BOX})
  set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
  message(STATUS "build ll-box statically")
  set(BOX_BIN_NAME "ll-box-static")
  add_link_options(-static -static-libgcc -static-libstdc++)
endif()


if(CMAKE_VERSION VERSION_LESS "3.14")
  set(ENABLE_CPM NO)
  message(
    STATUS "cmake version ${CMAKE_VERSION} not compatible with CPM.cmake.")
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(PFL)

if(ENABLE_CPM)
  include(CPM)
  cpmfindpackage(
    NAME
    tl-expected
    VERSION
    1.0.0 # NOTE: Upstream cmake version doesn't match git tag, we need
          # https://github.com/TartanLlama/expected/pull/62 in v1.1.0, but cmake
          # version of v1.1.0 is 1.0.0.
    GITHUB_REPOSITORY
    TartanLlama/expected
    GIT_TAG
    v1.1.0
    GIT_SHALLOW
    ON
    OPTIONS
    "EXPECTED_BUILD_TESTS OFF"
    EXCLUDE_FROM_ALL
    ON)

  cpmfindpackage(
    NAME
    nlohmann_json
    VERSION
    3.5.0
    URL
    "https://github.com/nlohmann/json/archive/refs/tags/v3.5.0.tar.gz"
    EXCLUDE_FROM_ALL
    ON
    OPTIONS
    "JSON_BuildTests OFF")
endif()

find_package(PkgConfig REQUIRED)

pkg_search_module(SECCOMP REQUIRED IMPORTED_TARGET libseccomp)

# for ocppi
find_package(tl-expected 1.0.0 REQUIRED)

find_package(nlohmann_json 3.5.0 QUIET)
if(NOT nlohmann_json_FOUND)
  list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake.fix)
  find_package(nlohmann_json 3.5.0 REQUIRED)
endif()

find_package(ocppi 0.3.2 REQUIRED)

function(get_real_target_name output target)
  get_target_property("${output}" "${target}" ALIASED_TARGET)
  if("${output}" STREQUAL "")
    set("${output}" "${target}")
  endif()
  set("${output}"
      ${${output}}
      PARENT_SCOPE)
endfunction()

pfl_init(AUTO)

include(GNUInstallDirs)

pfl_add_libraries(LIBS container utils APPS ll-box)
