// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include "pink/include/redis_parser.h"

#include <assert.h>     /* assert */

#include "slash/include/slash_string.h"
#include "slash/include/xdebug.h"

namespace pink {

#if 1
  #define IsHexDigit isxdigit
#else
static bool IsHexDigit(char ch) {
  return (ch>='0' && ch<='9') || (ch>='a' && ch<='f') || (ch>='A' && ch<='F');
}
#endif

static int HexDigitToInt32(char ch) {
  if (ch <= '9' && ch >= '0') {
    return ch-'0';
  } else if (ch <= 'F' && ch >= 'A') {
    return ch-'A';
  } else if (ch <= 'f' && ch >= 'a') {
    return ch-'a';
  } else {
    return 0;
  }
}

static int split2args(const char* req_buf, RedisCmdArgsType& argv) {
  const char *p = req_buf;
  dstring arg;

  while (1) {
    // skip blanks
    while (*p && isspace(*p)) p++;
    if (*p) {
      // get a token
      int inq = 0;  // set to 1 if we are in "quotes"
      int insq = 0;  // set to 1 if we are in 'single quotes'
      int done = 0;

      arg.clear();
      while (!done) {
        if (inq) {
          if (*p == '\\' && *(p+1) == 'x' &&
              IsHexDigit(*(p+2)) &&
              IsHexDigit(*(p+3))) {
            unsigned char byte = HexDigitToInt32(*(p+2))*16 + HexDigitToInt32(*(p+3));
            arg.append(1, byte);
            p += 3;
          } else if (*p == '\\' && *(p + 1)) {
            char c;

            p++;
            switch (*p) {
              case 'n': c = '\n'; break;
              case 'r': c = '\r'; break;
              case 't': c = '\t'; break;
              case 'b': c = '\b'; break;
              case 'a': c = '\a'; break;
              default: c = *p; break;
            }
            arg.append(1, c);
          } else if (*p == '"') {
            /* closing quote must be followed by a space or
            * nothing at all. */
            if (*(p+1) && !isspace(*(p+1))) {
              argv.clear();
              return -1;
            }
            done = 1;
          } else if (!*p) {
            // unterminated quotes
            argv.clear();
            return -1;
          } else {
            arg.append(1, *p);
          }
        } else if (insq) {
          if (*p == '\\' && *(p+1) == '\'') {
            p++;
            arg.append(1, '\'');
          } else if (*p == '\'') {
            /* closing quote must be followed by a space or
            * nothing at all. */
            if (*(p+1) && !isspace(*(p+1))) {
              argv.clear();
              return -1;
            }
            done = 1;
          } else if (!*p) {
            // unterminated quotes
            argv.clear();
            return -1;
          } else {
            arg.append(1, *p);
          }
        } else {
          switch (*p) {
            case ' ':
            case '\n':
            case '\r':
            case '\t':
            case '\0':
              done = 1;
            break;
            case '"':
              inq = 1;
            break;
            case '\'':
              insq = 1;
            break;
            default:
              // current = sdscatlen(current,p,1);
              arg.append(1, *p);
              break;
          }
        }
        if (*p) p++;
      }
      argv.push_back(arg);
    } else {
      return 0;
    }
  }
}


inline int RedisParser::FindNextSeparators() {
#if 1
  assert(cur_pos_ <= length_);
  if (auto f = memchr(input_buf_ + cur_pos_, '\n', length_ - cur_pos_)) {
    return (const char*)f - input_buf_;
  }
#else
  if (cur_pos_ > length_ - 1) {
    return -1;
  }
  int pos = cur_pos_;
  while (pos <= length_ - 1) {
    if (input_buf_[pos] == '\n') {
      return pos;
    }
    pos++;
  }
#endif
  return -1;
}

int RedisParser::GetNextNum(int pos, long* value) {
  assert(pos > cur_pos_);
  //     cur_pos_       pos
  //      |    ----------|
  //      |    |
  //      *3\r\n
  // [cur_pos_ + 1, pos - cur_pos_ - 2]
  if (slash::string2l(input_buf_ + cur_pos_ + 1,
                            pos - cur_pos_ - 2,
                            value)) {
    return 0; // Success
  }
  return -1; // Failed
}

void RedisParser::SetParserStatus(RedisParserStatus status,
    RedisParserError error) {
  if (status == kRedisParserHalf) {
    CacheHalfArgv();
  }
  status_code_ = status;
  error_code_ = error;
}

void RedisParser::CacheHalfArgv() {
  half_argv_.assign(input_buf_ + cur_pos_, length_ - cur_pos_);
  cur_pos_ = length_;
}

RedisParserStatus RedisParser::RedisParserInit(
    RedisParserType type, const RedisParserSettings& settings) {
  if (status_code_ != kRedisParserNone) {
    SetParserStatus(kRedisParserError, kRedisParserInitError);
    return status_code_;
  }
  if (type != REDIS_PARSER_REQUEST && type != REDIS_PARSER_RESPONSE) {
    SetParserStatus(kRedisParserError, kRedisParserInitError);
    return status_code_;
  }
  redis_parser_type_ = type;
  parser_settings_ = settings;
  SetParserStatus(kRedisParserInitDone);
  return status_code_;
}

RedisParserStatus RedisParser::ProcessInlineBuffer() {
  int pos, ret;
  pos = FindNextSeparators();
  if (pos == -1) {
    // change rbuf_len_ to length_
    if (length_ > REDIS_INLINE_MAXLEN) {
      SetParserStatus(kRedisParserError, kRedisParserFullError);
      return status_code_;
    } else {
      SetParserStatus(kRedisParserHalf);
      return status_code_;
    }
  }
#if 1
  const char* req_buf = input_buf_ + cur_pos_;
#else
  // args \r\n
  std::string req_buf(input_buf_ + cur_pos_, pos + 1 - cur_pos_);
#endif

  argv_.clear();
  ret = split2args(req_buf, argv_);
  cur_pos_ = pos + 1;

  if (ret == -1) {
    SetParserStatus(kRedisParserError, kRedisParserProtoError);
    return status_code_;
  }
  SetParserStatus(kRedisParserDone);
  return status_code_;
}

RedisParserStatus RedisParser::ProcessMultibulkBuffer() {
  int pos = 0;
  if (multibulk_len_ == 0) {
    /* The client should have been reset */
    pos = FindNextSeparators();
    if (pos != -1) {
      if (GetNextNum(pos, &multibulk_len_) != 0) {
        // Protocol error: invalid multibulk length
        SetParserStatus(kRedisParserError, kRedisParserProtoError);
        return status_code_;
      }
      cur_pos_ = pos + 1;
      argv_.clear();
      if (cur_pos_ > length_ - 1) {
        SetParserStatus(kRedisParserHalf);
        return status_code_;
      }
    } else {
      SetParserStatus(kRedisParserHalf);
      return status_code_;  // HALF
    }
  }
  while (multibulk_len_) {
    if (bulk_len_ == -1) {
      pos = FindNextSeparators();
      if (pos != -1) {
        if (input_buf_[cur_pos_] != '$') {
          SetParserStatus(kRedisParserError, kRedisParserProtoError);
          return status_code_;  // PARSE_ERROR
        }

        if (GetNextNum(pos, &bulk_len_) != 0) {
            // Protocol error: invalid bulk length
          SetParserStatus(kRedisParserError, kRedisParserProtoError);
          return status_code_;
        }
        cur_pos_ = pos + 1;
      }
      if (pos == -1 || cur_pos_ > length_ - 1) {
        SetParserStatus(kRedisParserHalf);
        return status_code_;
      }
    }
    if ((length_ - 1) - cur_pos_ + 1 < bulk_len_ + 2) {
      // Data not enough
      break;
    } else {
      argv_.emplace_back(input_buf_ + cur_pos_, bulk_len_);
      cur_pos_ = cur_pos_ + bulk_len_ + 2;
      bulk_len_ = -1;
      multibulk_len_--;
    }
  }

  if (multibulk_len_ == 0) {
    SetParserStatus(kRedisParserDone);
    return status_code_;  // OK
  } else {
    SetParserStatus(kRedisParserHalf);
    return status_code_;  // HALF
  }
}

void RedisParser::PrintCurrentStatus() {
  log_info("status_code %d error_code %d", status_code_, error_code_);
  log_info("multibulk_len_ %ld bulk_len %ld redis_type %d redis_parser_type %d", multibulk_len_, bulk_len_, redis_type_, redis_parser_type_);
  //for (auto& i : argv_) {
  //  UNUSED(i);
  //  log_info("parsed arguments: %s", i.c_str());
  //}
  log_info("cur_pos : %d", cur_pos_);
  log_info("input_buf_ is clean ? %d", input_buf_ == NULL);
  if (input_buf_ != NULL) {
    log_info(" input_buf %s", input_buf_);
  }
  log_info("half_argv_ : %.*s", (int)half_argv_.size(), half_argv_.data());
  log_info("input_buf len %d", length_);
}

RedisParserStatus RedisParser::ProcessInputBuffer(
    const char* input_buf, int length, int* parsed_len) {
  if (status_code_ == kRedisParserInitDone ||
      status_code_ == kRedisParserHalf ||
      status_code_ == kRedisParserDone) {
    size_t half_len = half_argv_.size();
    size_t full_len = half_len + length;
    input_str_.ensure_capacity(full_len + 1);
    input_str_.risk_set_size(full_len);
    memcpy(input_str_.data(), half_argv_.data(), half_len);
    memcpy(input_str_.data() + half_len, input_buf, length);
    input_str_.data()[full_len] = '\0'; // trailing '\0', act as std::string
    input_buf_ = input_str_.data();
    length_ = full_len;
    if (redis_parser_type_ == REDIS_PARSER_REQUEST) {
      ProcessRequestBuffer();
    } else if (redis_parser_type_ == REDIS_PARSER_RESPONSE) {
      ProcessResponseBuffer();
    } else {
      SetParserStatus(kRedisParserError, kRedisParserInitError);
      return status_code_;
    }
    // cur_pos_ starts from 0, val of cur_pos_ is the parsed_len
    *parsed_len = cur_pos_;
    ResetRedisParser();
    //PrintCurrentStatus();
    return status_code_;
  }
  SetParserStatus(kRedisParserError, kRedisParserInitError);
  return status_code_;
}

// TODO AZ
RedisParserStatus RedisParser::ProcessResponseBuffer() {
  SetParserStatus(kRedisParserDone);
  return status_code_;
}

RedisParserStatus RedisParser::ProcessRequestBuffer() {
  RedisParserStatus ret;
  while (cur_pos_ <= length_ - 1) {
    if (!redis_type_) {
      if (input_buf_[cur_pos_] == '*') {
        redis_type_ = REDIS_REQ_MULTIBULK;
      } else {
        redis_type_ = REDIS_REQ_INLINE;
      }
    }

    if (redis_type_ == REDIS_REQ_INLINE) {
      ret = ProcessInlineBuffer();
      if (ret != kRedisParserDone) {
        return ret;
      }
    } else if (redis_type_ == REDIS_REQ_MULTIBULK) {
      ret = ProcessMultibulkBuffer();
      if (ret != kRedisParserDone) { // FULL_ERROR || HALF || PARSE_ERROR
        return ret;
      }
    } else {
      // Unknown requeset type;
      return kRedisParserError;
    }
    if (!argv_.empty()) {
      argvs_.push_back(std::move(argv_));
      auto& last = argvs_.back();
      if (parser_settings_.DealMessage) {
        if (parser_settings_.DealMessage(this, last) != 0) {
          SetParserStatus(kRedisParserError, kRedisParserDealError);
          return status_code_;
        }
      }
    }
    assert(argv_.empty());
    // argv_.clear();
    // Reset
    ResetCommandStatus();
  }
  if (parser_settings_.Complete) {
    for (auto& argv : argvs_) {
      std::string& cmdname = argv[0];
      for (char& c : cmdname) c = tolower((unsigned char)c);
    }
    if (parser_settings_.Complete(this, std::move(argvs_)) != 0) {
      SetParserStatus(kRedisParserError, kRedisParserCompleteError);
      return status_code_;
    }
  }
  argvs_.clear();
  SetParserStatus(kRedisParserDone);
  return status_code_; // OK
}

void RedisParser::ResetCommandStatus() {
  redis_type_ = 0;
  multibulk_len_ = 0;
  bulk_len_ = -1;
  half_argv_.erase_all();
}

void RedisParser::ResetRedisParser() {
  cur_pos_ = 0;
  input_buf_ = NULL;
  input_str_.erase_all();
  length_ = 0;
}

}  // namespace pink
