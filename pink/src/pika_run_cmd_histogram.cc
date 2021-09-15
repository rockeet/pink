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
#include "pink/include/pika_run_cmd_histogram.h"

static const rocksdb::HistogramBucketMapper bucketMapper;

namespace cmd_run_histogram {

void PikaCmdRunHistogram::Add_Histogram(const std::string &name) {
  for (int i =0; i < StepMax; i++) {
    HistogramTable[i][name] = new rocksdb::HistogramStat();
    HistogramTable[i][name]->Clear();
  }
};

void PikaCmdRunHistogram::Add_Histogram_Metric(const std::string &name, uint64_t value, process_step step) {
  assert(step<StepMax);
  auto iter = HistogramTable[step].find(name);
  if (iter == HistogramTable[step].end()) {
    LOG(ERROR) << "command:" << name << " step:" << (unsigned int)step << " don't have Histogram";
    return;
  }
  iter->second->Add(value);
}

static terark::profiling pf;

void PikaCmdRunHistogram::Add_Histogram_Metric(statistics_info &info) {
  for (auto &cmdinfo:info.cmd_process_times) {
    slash::StringToLower(cmdinfo.cmd);
    Add_Histogram_Metric(cmdinfo.cmd, pf.us(info.read_start_time, info.parse_end_time), Parse);
    Add_Histogram_Metric(cmdinfo.cmd, pf.us(info.parse_end_time, info.schdule_end_time), Schedule);
    Add_Histogram_Metric(cmdinfo.cmd, pf.us(cmdinfo.start_time, cmdinfo.end_time), Process);
    Add_Histogram_Metric(cmdinfo.cmd, pf.us(cmdinfo.end_time, info.response_end_time), Response);
    Add_Histogram_Metric(cmdinfo.cmd, pf.us(info.read_start_time, info.response_end_time), All);
  }
  info.cmd_process_times.clear();
}

std::string PikaCmdRunHistogram::get_metric() {
  terark::string_appender<> oss;
  for(int step = 0; step < StepMax; step++) {
    auto Histogram = HistogramTable[step];
    for (auto const &iter:Histogram) {
      auto &buckets = iter.second->buckets_;
      u_int64_t last = 0;
      auto const &name = iter.first;
      size_t limit = 0;
      for (size_t i = bucketMapper.BucketCount() - 1; i > 0; i--) {
        if (buckets[i].cnt > 0) { limit = i; break; }
      }
      for (size_t i = 0; i <= limit; i++) {
        last += buckets[i].cnt;
        oss<<"pika_cost_time_bucket{"<<"name=\""<<name<<"\" step=\""<<step_str[step]<<"\" le=\""<<bucketMapper.BucketLimit(i)<<"\"} "<<last<<"\n";
      }
      oss<<"pika_cost_time_bucket{"<<"name=\""<<name<<"\" step=\""<<step_str[step]<<"\" le=\"+Inf\"} "<<last<<"\n";
      oss<<"pika_cost_time_count{"<<"name=\""<<name<<"\" step=\""<<step_str[step]<<"\"} "<<last<<"\n";
      oss<<"pika_cost_time_sum{"<<"name=\""<<name<<"\" step=\""<<step_str[step]<<"\"} "<<iter.second->sum_<<"\n";
      oss<<"pika_cost_time_max_bucket{"<<"name=\""<<name<<"\" step=\""<<step_str[step]<<"\"} "<<bucketMapper.BucketLimit(limit)<<"\n";
    }
  }

  return oss.str();
}

} //end cmd_run_histogram

