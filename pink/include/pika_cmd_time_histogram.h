// Copyright (c) 2021-present, Topling, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#pragma once

#include <unordered_map>
#include <string>
#include "terark/fstring.hpp"
#include "monitoring/histogram.h"

namespace time_histogram {

enum CmdProcessStep {
  Parse,
  Schedule,
  Process,
  Response,
  All,
  StepMax,
};

struct CmdProcessTime {
  CmdProcessTime(std::string const c, uint64_t st, uint64_t et):cmd(c), start_time(st), end_time(et){};
  std::string cmd;
  uint64_t start_time;
  uint64_t end_time;
};
struct CmdTimeInfo {
  uint64_t read_start_time;
  uint64_t parse_end_time;
  uint64_t schdule_end_time;
  uint64_t response_end_time;
  std::vector<CmdProcessTime> cmd_process_times;
};

class PikaCmdRunTimeHistogram {
public:
  PikaCmdRunTimeHistogram() {};
  void AddHistogram(const std::string &name);
  void AddTimeMetric(const std::string &name, uint64_t value, CmdProcessStep step);
  void AddTimeMetric(CmdTimeInfo &info);
  std::string GetTimeMetric();

private:
  std::unordered_map<std::string, rocksdb::HistogramStat*> HistogramTable[StepMax];
  const terark::fstring step_str[StepMax] = {"parse","schedule","process","response","all"};
};

} // end time_histogram
