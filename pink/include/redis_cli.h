// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
//
#ifndef PINK_INCLUDE_REDIS_CLI_H_
#define PINK_INCLUDE_REDIS_CLI_H_

#include <vector>
#include <string>
#include "redis_parser.h"

namespace pink {

// We can serialize redis command by 2 ways:
// 1. by variable argmuments;
//    eg.  RedisCli::Serialize(cmd, "set %s %d", "key", 5);
//        cmd will be set as the result string;
// 2. by a string vector;
//    eg.  RedisCli::Serialize(argv, cmd);
//        also cmd will be set as the result string.
extern int SerializeRedisCommand(std::string *cmd, const char *format, ...);
extern int SerializeRedisCommand(const RedisCmdArgsType& argv, std::string *cmd);

}   // namespace pink

#endif  // PINK_INCLUDE_REDIS_CLI_H_
