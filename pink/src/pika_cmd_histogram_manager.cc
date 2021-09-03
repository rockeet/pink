// Copyright (c) 2018-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include <unistd.h>
#include <sys/syscall.h>
#include <glog/logging.h>

#include "pink/include/pika_cmd_histogram_manager.h"

//extern const rocksdb::HistogramBucketMapper bucketMapper;
static const rocksdb::HistogramBucketMapper bucketMapper;

void PikaCmdHistogramManager::Add_Histogram(const std::string &name) {
  for (int i =0; i < StepMax; i++) {
    HistogramTable[i][name] = new rocksdb::HistogramStat();
    HistogramTable[i][name]->Clear();
  }
};

void PikaCmdHistogramManager::Add_Histogram_Metric(const std::string &name, long value, process_step step) {
  assert(step<StepMax);
  auto iter = HistogramTable[step].find(name);
  if (iter == HistogramTable[step].end()) {
    LOG(ERROR) << "command:" << name << " step:" << (unsigned int)step << " don't have Histogram";
    return;
  }
  iter->second->Add(value);
}

std::string PikaCmdHistogramManager::get_metric() {
  std::ostringstream oss;
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
        oss<<"pika_cost_time_bucket{"<<"name=\""<<name<<"\" step=\""<<step_str[step]<<"\" le=\""<<std::to_string(bucketMapper.BucketLimit(i))<<"\"} "<<last<<"\n";
      }
      oss<<"pika_cost_time_bucket{"<<"name=\""<<name<<"\" step=\""<<step_str[step]<<"\" le=\"+Inf\"} "<<last<<"\n";
      oss<<"pika_cost_time_count{"<<"name=\""<<name<<"\" step=\""<<step_str[step]<<"\"} "<<last<<"\n";
      oss<<"pika_cost_time_sum{"<<"name=\""<<name<<"\" step=\""<<step_str[step]<<"\"} "<<iter.second->sum_<<"\n";
      oss<<"pika_cost_time_max_bucket{"<<"name=\""<<name<<"\" step=\""<<step_str[step]<<"\"} "<<std::to_string(bucketMapper.BucketLimit(limit))<<"\n";
    }
  }

  return oss.str();
}