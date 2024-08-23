/*
 * SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef LINGLONG_BOX_SRC_CONTAINER_CONTAINER_H_
#define LINGLONG_BOX_SRC_CONTAINER_CONTAINER_H_

#include "linglong/container/host_mount.h"
#include "linglong/utils/oci_runtime.h"

namespace linglong::container {

class Container {
 public:
  Container(std::filesystem::path bundle, std::string id, utils::Runtime runtime);

  ~Container();

  int Start();

 private:
  [[nodiscard]] static int DropPermissions();
  [[nodiscard]] static int PrepareLinks();
  [[nodiscard]] int PrepareDefaultDevices();
  [[nodiscard]] bool forkAndExecProcess(const utils::Process &process,
                                        bool unblock = false);
  [[nodiscard]] int PivotRoot() const;
  int MountContainerPath();
  void waitChildAndExec();

  static int NonePrivilegeProc(void *self);
  static int EntryProc(void *self);

  utils::Runtime runtime;
  std::filesystem::path bundle;
  std::string id;
  std::filesystem::path hostRoot;

  int hostUid{-1};
  int hostGid{-1};
  bool useNewCgroupNs{false};
  std::map<int, std::string> pidMap;

  HostMount containerMounter;
};

}  // namespace linglong::container

#endif /* LINGLONG_BOX_SRC_CONTAINER_CONTAINER_H_ */
