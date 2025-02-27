// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include "pink/include/redis_conn.h"

#include <stdlib.h>
#include <limits.h>
#include <sys/socket.h>

#include <string>
#include <sstream>

#include "slash/include/xdebug.h"
#include "slash/include/slash_string.h"

#include <terark/io/readv_writev.hpp>
#include <glog/logging.h>

namespace pink {

RedisConn::RedisConn(const int fd,
                     const std::string& ip_port,
                     Thread* thread,
                     PinkEpoll* pink_epoll,
                     const HandleType& handle_type,
                     const int rbuf_max_len)
    : PinkConn(fd, ip_port, thread, pink_epoll),
      handle_type_(handle_type),
      rbuf_(nullptr),
      rbuf_len_(0),
      rbuf_max_len_(rbuf_max_len),
      msg_peak_(0),
      command_len_(0),
#ifdef REDIS_DONT_USE_writev
      wbuf_pos_(0),
#endif
      last_read_pos_(-1),
      bulk_len_(-1) {
  RedisParserSettings settings;
  settings.DealMessage = ParserDealMessageCb;
  settings.Complete = ParserCompleteCb;
  redis_parser_.RedisParserInit(REDIS_PARSER_REQUEST, settings);
  redis_parser_.data = this;
}

RedisConn::~RedisConn() {
  free(rbuf_);
}

ReadStatus RedisConn::ParseRedisParserStatus(RedisParserStatus status) {
  if (status == kRedisParserInitDone) {
    return kOk;
  } else if (status == kRedisParserHalf) {
    return kReadHalf;
  } else if (status == kRedisParserDone) {
    return kReadAll;
  } else if (status == kRedisParserError) {
    RedisParserError error_code = redis_parser_.get_error_code();
    switch (error_code) {
      case kRedisParserOk :
        return kReadError;
      case kRedisParserInitError :
        return kReadError;
      case kRedisParserFullError :
        return kFullError;
      case kRedisParserProtoError :
        return kParseError;
      case kRedisParserDealError :
        return kDealError;
      default :
        return kReadError;
    }
  } else {
    return kReadError;
  }
}

ReadStatus RedisConn::GetRequest() {
  int next_read_pos = last_read_pos_ + 1;
  int remain = rbuf_len_ - next_read_pos;  // Remain buffer size
  int new_size = 0;
  if (remain == 0) {
    new_size = rbuf_len_ + REDIS_IOBUF_LEN;
    remain += REDIS_IOBUF_LEN;
  } else if (remain < bulk_len_) {
    new_size = next_read_pos + bulk_len_;
    remain = bulk_len_;
  }
  if (new_size > rbuf_len_) {
    if (new_size > rbuf_max_len_) {
      return kFullError;
    }
    using std::max;
    using std::min;
    // inc new_size by REDIS_IOBUF_LEN may cause frequent realloc.
    // inc by 1.5x reduce realloc freq with bounded complexity
    new_size = min(rbuf_max_len_, max(rbuf_len_*3/2, new_size));
    rbuf_ = static_cast<char*>(realloc(rbuf_, new_size));
    if (rbuf_ == nullptr) {
      return kFullError;
    }
    rbuf_len_ = new_size;
  }
  ssize_t nread = read(fd(), rbuf_ + next_read_pos, remain);
  if (nread < 0) {
    if (errno == EAGAIN) {
      nread = 0;
      return kReadHalf; // HALF
    } else {
      // error happened, close client
      return kReadError;
    }
  } else if (nread == 0) {
    // client closed, close client
    return kReadClose;
  }
  // assert(nread > 0);
  last_read_pos_ += nread;
  msg_peak_ = last_read_pos_;
  command_len_ += nread;
  if (command_len_ >= rbuf_max_len_) {
    LOG(INFO) <<"close conn command_len " << command_len_ << ", rbuf_max_len " << rbuf_max_len_;
    return kFullError;
  }
  RedisParserStatus ret = redis_parser_.ProcessInputBuffer(
      rbuf_ + next_read_pos, nread);
  ReadStatus read_status = ParseRedisParserStatus(ret);
  if (read_status == kReadAll || read_status == kReadHalf) {
    if (read_status == kReadAll) {
      command_len_ = 0;
    }
    last_read_pos_ = -1;
    bulk_len_ = redis_parser_.get_bulk_len();
  }
  if (!response_.empty()) {
    set_is_reply(true);
  }
  return read_status; // OK || HALF || FULL_ERROR || PARSE_ERROR
}

WriteStatus RedisConn::SendReply() {
  ssize_t nwritten = 0;
#ifdef REDIS_DONT_USE_writev
  size_t wbuf_len = response_.size();
  while (wbuf_len > 0) {
    nwritten = send(fd(), response_.data() + wbuf_pos_, wbuf_len - wbuf_pos_, 0);
    if (nwritten <= 0) {
      break;
    }
    wbuf_pos_ += nwritten;
    if (wbuf_pos_ == wbuf_len) {
      // Have sended all response data
     #if 0
      if (wbuf_len > DEFAULT_WBUF_SIZE) {
        std::string buf;
        buf.reserve(DEFAULT_WBUF_SIZE);
        response_.swap(buf);
      }
     #endif
      response_.clear();

      wbuf_len = 0;
      wbuf_pos_ = 0;
      return kWriteAll;
    }
  }
#else
  int num = response_.size();
  if (0 == num) {
    fprintf(stderr, "ERROR: RedisConn::SendReply: response_ is empty @1\n");
    return kWriteAll;
  }
  iovec* iov;
  if (iov_idx_ < 0) {
    iov_.resize_no_init(num);
    iov = iov_.data();
    int i = 0;
    for (int j = 0; j < num; ++j) {
      if (response_[j].empty()) {
        fprintf(stderr, "ERROR: RedisConn::SendReply: response_[%d of %d] is empty\n", j, num);
      } else {
        iov[i].iov_base = response_[j].data();
        iov[i].iov_len  = response_[j].size();
        i++;
      }
    }
    if (0 == i) {
      fprintf(stderr, "ERROR: RedisConn::SendReply: response_ is empty @2\n");
      return kWriteAll;
    }
    num = i; // non-empty num
    iov_.risk_set_size(i);
    iov_idx_ = 0;
  }
  else {
    num = (int)iov_.size(); // non-empty num
    iov = iov_.data();
  }
  do {
    nwritten = terark::easy_writev(fd(), iov, num, &iov_idx_);
    if (0 == iov[num-1].iov_len) { // all data was successful sent
      //TERARK_VERIFY_GT(nwritten, 0);
      response_.clear();
      iov_.erase_all();
      iov_idx_ = -1;
      return kWriteAll;
    }
  } while (nwritten > 0);
#endif
  if (nwritten == -1) {
    if (errno == EAGAIN) {
      num_retry_ = 0;
      return kWriteHalf;
    } else if (errno == EDEADLK) {
      if (num_retry_ < 5) {
        num_retry_++;
        return kWriteHalf;
      } else {
        fprintf(stderr, "ERROR: RedisConn::SendReply: exceeds retry 5\n");
        num_retry_ = 0;
        return kWriteError;
      }
    } else {
      num_retry_ = 0;
      // Here we should close the connection
      return kWriteError;
    }
  }
  return kWriteHalf;
}

int RedisConn::WriteResp(std::string&& resp) {
#ifdef REDIS_DONT_USE_writev
  if (response_.empty())
    response_ = std::move(resp);
  else
    response_.append(resp);
#else
  response_.push_back(std::move(resp));
#endif
  set_is_reply(true);
  return 0;
}

void RedisConn::TryResizeBuffer() {
  struct timeval now;
  gettimeofday(&now, nullptr);
  int idletime = now.tv_sec - last_interaction().tv_sec;
  if (rbuf_len_ > REDIS_MBULK_BIG_ARG &&
      ((rbuf_len_ / (msg_peak_ + 1)) > 2 || idletime > 2)) {
    int new_size =
      ((last_read_pos_ + REDIS_IOBUF_LEN) / REDIS_IOBUF_LEN) * REDIS_IOBUF_LEN;
    if (new_size < rbuf_len_) {
      rbuf_ = static_cast<char*>(realloc(rbuf_, new_size));
      rbuf_len_ = new_size;
      log_info("Resize buffer to %d, last_read_pos_: %d\n",
               rbuf_len_, last_read_pos_);
    }
    msg_peak_ = 0;
  }
}

void RedisConn::SetHandleType(const HandleType& handle_type) {
  handle_type_ = handle_type;
}

HandleType RedisConn::GetHandleType() {
  return handle_type_;
}

void RedisConn::ProcessRedisCmds(std::vector<RedisCmdArgsType>&& argvs, bool async) {
}

void RedisConn::NotifyEpoll(bool success) {
  PinkItem ti(fd(), ip_port(), success ? kNotiEpolloutAndEpollin : kNotiClose);
  ti.conn = this->shared_from_this();
  pink_epoll()->Register(std::move(ti), true);
}

int RedisConn::ParserDealMessageCb(RedisParser* parser, const RedisCmdArgsType& argv) {
  RedisConn* conn = reinterpret_cast<RedisConn*>(parser->data);
  if (conn->GetHandleType() == HandleType::kSynchronous) {
    return conn->DealMessage(argv);
  } else {
    return 0;
  }
}

int RedisConn::ParserCompleteCb(RedisParser* parser, std::vector<RedisCmdArgsType>&& argvs) {
  RedisConn* conn = reinterpret_cast<RedisConn*>(parser->data);
  bool async = conn->GetHandleType() == HandleType::kAsynchronous;
  conn->ProcessRedisCmds(std::move(argvs), async);
  return 0;
}

}  // namespace pink
