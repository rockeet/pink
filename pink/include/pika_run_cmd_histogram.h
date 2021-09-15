// Copyright (c) 2021-present, Topling, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#pragma once

#include <unordered_map>
#include <string>
#include "terark/fstring.hpp"
#include "monitoring/histogram.h"

namespace cmd_run_histogram {

enum process_step {
  Parse,
  Schedule,
  Process,
  Response,
  All,
  StepMax,
};

struct cmd_process_time {
  cmd_process_time(std::string const c, uint64_t st, uint64_t et):cmd(c), start_time(st), end_time(et){};
  std::string cmd;
  uint64_t start_time;
  uint64_t end_time;
};
struct statistics_info {
  uint64_t read_start_time;
  uint64_t parse_end_time;
  uint64_t schdule_end_time;
  uint64_t response_end_time;
  std::vector<cmd_process_time> cmd_process_times;
};

class PikaCmdRunHistogram {
public:
  PikaCmdRunHistogram() {};
  void Add_Histogram(const std::string &name);
  void Add_Histogram_Metric(const std::string &name, uint64_t value, process_step step);
  void Add_Histogram_Metric(statistics_info &info);
  std::string get_metric();

private:
  std::unordered_map<std::string, rocksdb::HistogramStat*> HistogramTable[StepMax];
  std::vector<terark::fstring> const step_str{"parse","schedule","process","response","all"};  //adpater process_step
};

} // end cmd_run_histogram
