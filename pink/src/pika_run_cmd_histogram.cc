// Copyright (c) 2021-present, Topling, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include <unistd.h>
#include <sys/syscall.h>
#include <glog/logging.h>

#include "terark/util/profiling.hpp"
#include "terark/num_to_str.hpp"
#include "slash/include/slash_string.h"
#include "pink/include/pika_cmd_time_histogram.h"

static const rocksdb::HistogramBucketMapper bucketMapper;

namespace time_histogram {

PikaCmdRunTimeHistogram::PikaCmdRunTimeHistogram() {
  for (size_t cmd_idx = 0; cmd_idx < HistogramNum; cmd_idx++) {
    for (int i = 0; i < StepMax; i++) {
      HistogramTable[cmd_idx][i] = new rocksdb::HistogramStat();
      HistogramTable[cmd_idx][i]->Clear();
    }
  }
}
PikaCmdRunTimeHistogram::~PikaCmdRunTimeHistogram() {
  for (size_t cmd_idx = 0; cmd_idx < HistogramNum; cmd_idx++) {
    for (int i = 0; i < StepMax; i++) {
      delete HistogramTable[cmd_idx][i];
    }
  }
}
void PikaCmdRunTimeHistogram::AddTimeMetric(size_t cmd_idx, uint64_t value, CmdProcessStep step) {
  assert(step<StepMax);
  TERARK_VERIFY_LT(cmd_idx, HistogramNum);
  HistogramTable[cmd_idx][step]->Add(value);
}

static terark::profiling pf;

void PikaCmdRunTimeHistogram::AddTimeMetric(CmdTimeInfo& info) {
  for (auto& cmdinfo: info.cmd_process_times) {
    AddTimeMetric(cmdinfo.cmd_idx, pf.us(info.read_start_time, info.parse_end_time), Parse);
    AddTimeMetric(cmdinfo.cmd_idx, pf.us(info.parse_end_time, info.schedule_end_time), Schedule);
    AddTimeMetric(cmdinfo.cmd_idx, pf.us(cmdinfo.start_time, cmdinfo.end_time), Process);
    AddTimeMetric(cmdinfo.cmd_idx, pf.us(cmdinfo.end_time, info.response_end_time), Response);
    AddTimeMetric(cmdinfo.cmd_idx, pf.us(info.read_start_time, info.response_end_time), All);
  }
  info.cmd_process_times.clear();
}

std::string PikaCmdRunTimeHistogram::GetTimeMetric() {
  terark::string_appender<> oss;
  for(int step = 0; step < StepMax; step++) {
    for (size_t i = 0; i < HistogramNum; ++i) {
      auto* histogram = HistogramTable[i][step];
      auto& buckets = histogram->buckets_;
      fstring name = m_get_name(i);
      uint64_t last = 0;
      size_t limit = 0;
      for (size_t i = bucketMapper.BucketCount(); i; ) {
        i--;
        if (buckets[i].cnt > 0) { limit = i; break; }
      }
      for (size_t i = 0; i <= limit; i++) {
        last += buckets[i].cnt;
        oss<<"pika_cost_time_bucket{"<<"name=\""<<name<<"\" step=\""<<step_str[step]<<"\" le=\""<<bucketMapper.BucketLimit(i)<<"\"} "<<last<<"\n";
      }
      oss<<"pika_cost_time_bucket{"<<"name=\""<<name<<"\" step=\""<<step_str[step]<<"\" le=\"+Inf\"} "<<last<<"\n";
      oss<<"pika_cost_time_count{"<<"name=\""<<name<<"\" step=\""<<step_str[step]<<"\"} "<<last<<"\n";
      oss<<"pika_cost_time_sum{"<<"name=\""<<name<<"\" step=\""<<step_str[step]<<"\"} "<<histogram->sum_<<"\n";
      oss<<"pika_cost_time_max_bucket{"<<"name=\""<<name<<"\" step=\""<<step_str[step]<<"\"} "<<bucketMapper.BucketLimit(limit)<<"\n";
    }
  }

  return std::move(oss);
}

} //end time_histogram

