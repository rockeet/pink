// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#ifndef PINK_INCLUDE_REDIS_PARSER_H_
#define PINK_INCLUDE_REDIS_PARSER_H_

#include "pink/include/pink_define.h"

#include <vector>

#include <limits.h>
#include <stdint.h>
#include <terark/valvec.hpp>

#if 0
  #include <folly/FBString.h>
  using dstring = folly::fbstring;
#else
  using dstring = std::string;
#endif

#define REDIS_PARSER_REQUEST 1
#define REDIS_PARSER_RESPONSE 2

namespace pink {

class RedisParser;

struct RedisCmdArgsType : std::vector<dstring> {
  mutable size_t cmd_idx = UINT_MAX;
};
typedef int (*RedisParserDataCb) (RedisParser*, const RedisCmdArgsType&);
typedef int (*RedisParserMultiDataCb) (RedisParser*, std::vector<RedisCmdArgsType>&&);
typedef int (*RedisParserCb) (RedisParser*);
typedef int RedisParserType;

enum RedisParserStatus : uint8_t {
  kRedisParserNone = 0,
  kRedisParserInitDone = 1,
  kRedisParserHalf = 2,
  kRedisParserDone = 3,
  kRedisParserError = 4,
};

enum RedisParserError : uint8_t {
  kRedisParserOk = 0,
  kRedisParserInitError = 1,
  kRedisParserFullError = 2, // input overwhelm internal buffer
  kRedisParserProtoError = 3,
  kRedisParserDealError = 4,
  kRedisParserCompleteError = 5,
};

struct RedisParserSettings {
  RedisParserDataCb DealMessage = nullptr;
  RedisParserMultiDataCb Complete = nullptr;
};

class RedisParser {
 public:
  RedisParserStatus RedisParserInit(RedisParserType type, const RedisParserSettings& settings);
  RedisParserStatus ProcessInputBuffer(const char* input_buf, size_t length);
  long get_bulk_len() {
    return bulk_len_;
  }
  RedisParserError get_error_code() {
    return error_code_;
  }
  void *data; /* A pointer to get hook to the "connection" or "socket" object */
 private:
  // for DEBUG
  void PrintCurrentStatus();

  void CacheHalfArgv();
  int FindNextSeparators();
  int GetNextNum(int pos, long* value);
  RedisParserStatus ProcessInlineBuffer();
  RedisParserStatus ProcessMultibulkBuffer();
  RedisParserStatus ProcessRequestBuffer();
  RedisParserStatus ProcessResponseBuffer();
  void SetParserStatus(RedisParserStatus status, RedisParserError error = kRedisParserOk);
  void ResetRedisParser();
  void ResetCommandStatus();

  RedisParserSettings parser_settings_;
  RedisParserStatus status_code_ = kRedisParserNone;
  RedisParserError error_code_ = kRedisParserOk;

  uint8_t redis_type_ = 0; // REDIS_REQ_INLINE or REDIS_REQ_MULTIBULK
  uint8_t redis_parser_type_ = REDIS_PARSER_REQUEST; // or REDIS_PARSER_RESPONSE

  long multibulk_len_ = 0;
  long bulk_len_ = -1;
  terark::valvec<char> half_argv_;

  RedisCmdArgsType argv_;
  std::vector<RedisCmdArgsType> argvs_;

  int cur_pos_ = 0;
  int length_ = 0;
  const char* input_buf_ = nullptr;
  terark::valvec<char> input_str_;
};

}  // namespace pink
#endif  // PINK_INCLUDE_REDIS_PARSER_H_

