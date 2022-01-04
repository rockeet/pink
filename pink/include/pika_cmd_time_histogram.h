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

using terark::fstring;

enum CmdProcessStep {
  Parse,
  Schedule,
  Process,
  Response,
  All,
  StepMax,
};

struct CmdProcessTime {
  size_t cmd_idx;
  uint64_t start_time;
  uint64_t end_time;
};
struct CmdTimeInfo {
  uint64_t read_start_time;
  uint64_t parse_end_time;
  uint64_t schedule_end_time;
  uint64_t response_end_time;
  std::vector<CmdProcessTime> cmd_process_times;
};

class PikaCmdRunTimeHistogram {
public:
  PikaCmdRunTimeHistogram(const PikaCmdRunTimeHistogram&) = delete;
  PikaCmdRunTimeHistogram& operator=(const PikaCmdRunTimeHistogram&) = delete;
  PikaCmdRunTimeHistogram();
  ~PikaCmdRunTimeHistogram();
  void AddTimeMetric(CmdTimeInfo& info);
  std::string GetTimeMetric();
//size_t  (*m_get_idx)(fstring) = nullptr;
  fstring (*m_get_name)(size_t) = nullptr;
  static constexpr size_t HistogramNum = 151;
private:
  void AddTimeMetric(size_t cmd_idx, uint64_t value, CmdProcessStep step);
  rocksdb::HistogramStat* HistogramTable[HistogramNum][StepMax];
  const terark::fstring step_str[StepMax] = {"parse","schedule","process","response","all"};
};

} // end time_histogram
