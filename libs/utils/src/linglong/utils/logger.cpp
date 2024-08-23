/*
 * SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "linglong/utils/logger.h"

#include <sys/syslog.h>

#include "linglong/utils/common.h"

namespace linglong::utils {

std::string errnoString() {
  return format("errno(%d): %s", errno, strerror(errno));
}

std::string RetErrString(int ret) {
  return format("ret(%d),errno(%d): %s", ret, errno, strerror(errno));
}

std::string GetPidnsPid() {
  char buf[30];
  memset(buf, 0, sizeof(buf));
  if (readlink("/proc/self/ns/pid", buf, sizeof(buf) - 1) == -1) {
    return "";
  };
  std::string str = buf;
  return str.substr(5, str.length() - 6) + ":" +
         std::to_string(getpid());  // 6 = strlen("pid:[]")
}

static Logger::Level getLogLevelFromStr(const std::string& str) {
  if (str == "Debug") {
    return Logger::Debug;
  }
  if (str == "Info") {
    return Logger::Info;
  }
  if (str == "Warning") {
    return Logger::Warning;
  }
  if (str == "Error") {
    return Logger::Error;
  }
  if (str == "Fatal") {
    return Logger::Fatal;
  }
  return Logger::Level(Logger::Warning);
}

static Logger::Level initLogLevel() {
  openlog("ll-box", LOG_PID, LOG_USER);
  auto *env = getenv("LINGLONG_LOG_LEVEL");
  return getLogLevelFromStr(env ? env : "Error");
}

Logger::Level Logger::LOGLEVEL = initLogLevel();

}  // namespace linglong::utils
