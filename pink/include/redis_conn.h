// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#ifndef PINK_INCLUDE_REDIS_CONN_H_
#define PINK_INCLUDE_REDIS_CONN_H_

#include <map>
#include <vector>
#include <string>

#include "slash/include/slash_status.h"
#include "pink/include/pink_define.h"
#include "pink/include/pink_conn.h"
#include "pink/include/redis_parser.h"
#include <sys/uio.h>

#include <terark/valvec.hpp>

namespace pink {

enum HandleType {
  kSynchronous,
  kAsynchronous
};

class RedisConn: public PinkConn {
 public:
  RedisConn(const int fd,
            const std::string& ip_port,
            Thread* thread,
            PinkEpoll* pink_epoll = nullptr,
            const HandleType& handle_type = kSynchronous,
            const int rbuf_max_len = REDIS_MAX_MESSAGE);
  virtual ~RedisConn();

  ReadStatus GetRequest() override;
  WriteStatus SendReply() override;
  int WriteResp(std::string&& resp) override;

  void TryResizeBuffer() override;
  void SetHandleType(const HandleType& handle_type);
  HandleType GetHandleType();

  virtual void ProcessRedisCmds(std::vector<RedisCmdArgsType>&& argvs, bool async);
  void NotifyEpoll(bool success);

  virtual int DealMessage(const RedisCmdArgsType& argv) = 0;

 private:
  static int ParserDealMessageCb(RedisParser* parser, const RedisCmdArgsType& argv);
  static int ParserCompleteCb(RedisParser* parser, std::vector<RedisCmdArgsType>&& argvs);
  ReadStatus ParseRedisParserStatus(RedisParserStatus status);

  HandleType handle_type_;

  char* rbuf_;
  int rbuf_len_;
  int rbuf_max_len_;
  int msg_peak_;
  int command_len_;

#ifdef REDIS_DONT_USE_writev
  uint32_t wbuf_pos_;
  std::string response_;
#else
  int iov_idx_ = -1;
  std::vector<std::string> response_;
  terark::valvec<iovec> iov_;
#endif

  // For Redis Protocol parser
  int last_read_pos_;
  RedisParser redis_parser_;
  long bulk_len_;
};

}  // namespace pink
#endif  // PINK_INCLUDE_REDIS_CONN_H_
