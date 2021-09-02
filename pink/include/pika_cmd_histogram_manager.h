// Copyright (c) 2021-present, terark, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#pragma once

#include <unordered_map>
#include <string>
#include "monitoring/histogram.h"

enum process_step {
  Parse=0,
  Process=1,
  Response=2,
  StepMax,
};

class PikaCmdHistogramManager {
public:
  PikaCmdHistogramManager() {};
  void Add_Histogram(const std::string &name);
  void Add_Histogram_Metric(const std::string &name, long value, process_step step);
  std::string get_metric();

private:
  std::unordered_map<std::string, rocksdb::HistogramStat*> HistogramTable[StepMax];
};