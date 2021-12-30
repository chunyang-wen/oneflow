/*
Copyright 2020 The OneFlow Authors. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include "oneflow/core/embedding/embedding_manager.h"
#include "oneflow/core/embedding/block_based_key_value_store.h"

namespace oneflow {

namespace embedding {}  // namespace embedding

embedding::Cache* EmbeddingMgr::GetCache(const std::string& name, int64_t parallel_id) {
  std::pair<std::string, int64_t> map_key = std::make_pair(name, parallel_id);
  std::unique_lock<std::mutex> lock(mutex_);
  auto it = cache_map_.find(map_key);
  if (it != cache_map_.end()) { return it->second.get(); }
  embedding::CudaLruCacheOptions options{};
  const uint32_t line_size = ParseIntegerFromEnv("EMBEDDING_SIZE", 128);
  options.line_size = line_size;
  options.memory_budget_mb = ParseIntegerFromEnv("CACHE_MEMORY_BUDGET_MB", 0);
  CHECK_GT(options.memory_budget_mb, 0);
  options.max_query_length = 65536 * 26;
  options.key_type = DataType::kInt64;
  options.value_type = DataType::kFloat;
  std::unique_ptr<embedding::Cache> cache = embedding::NewCudaLruCache(options);
  auto pair = cache_map_.emplace(map_key, std::move(cache));
  CHECK(pair.second);
  return pair.first->second.get();
}

embedding::KeyValueStore* EmbeddingMgr::GetKeyValueStore(const std::string& name,
                                                         int64_t parallel_id) {
  std::pair<std::string, int64_t> map_key = std::make_pair(name, parallel_id);
  std::unique_lock<std::mutex> lock(mutex_);
  auto it = key_value_store_map_.find(map_key);
  if (it != key_value_store_map_.end()) { return it->second.get(); }

  std::unique_ptr<embedding::KeyValueStore> store;
  std::string kv_store = GetStringFromEnv("KEY_VALUE_STORE", "");
  if (kv_store == "cuda_in_memory") {
    embedding::CudaInMemoryKeyValueStoreOptions options{};
    options.num_shards = 4;
    options.value_length = ParseIntegerFromEnv("EMBEDDING_SIZE", 128);
    options.num_keys = ParseIntegerFromEnv("NUM_KEYS", 0);
    CHECK_GT(options.num_keys, 0);
    options.num_device_keys = ParseIntegerFromEnv("NUM_DEVICE_KEYS", 0);
    options.key_type = DataType::kInt64;
    options.value_type = DataType::kFloat;
    options.encoding_type = embedding::CudaInMemoryKeyValueStoreOptions::EncodingType::kOrdinal;
    store = NewCudaInMemoryKeyValueStore(options);
  } else if (kv_store == "block_based") {
    std::string path = GetStringFromEnv("BLOCK_BASED_PATH", "");
    embedding::BlockBasedKeyValueStoreOptions options{};
    options.path = path + std::to_string(parallel_id);
    options.value_length = ParseIntegerFromEnv("EMBEDDING_SIZE", 128);
    options.key_type = DataType::kInt64;
    options.value_type = DataType::kFloat;
    options.max_query_length = 65536 * 26;
    options.block_size = ParseIntegerFromEnv("BLOCK_BASED_BLOCK_SIZE", 512);
    store = NewBlockBasedKeyValueStore(options);
  } else {
    UNIMPLEMENTED();
  }
  auto pair = key_value_store_map_.emplace(map_key, std::move(store));
  CHECK(pair.second);
  return pair.first->second.get();
}

}  // namespace oneflow
