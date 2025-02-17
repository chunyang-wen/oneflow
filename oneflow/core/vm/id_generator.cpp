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
#include "oneflow/core/common/multi_client.h"
#include "oneflow/core/control/global_process_ctx.h"
#include "oneflow/core/vm/id_generator.h"
#include "oneflow/core/vm/id_util.h"

namespace oneflow {
namespace vm {

Maybe<int64_t> LogicalIdGenerator::NewSymbolId() {
  if (JUST(IsMultiClient())) {
    // NOTE(chengcheng): in Multi-Client LogicalIdGenerator will degenerate directly to
    //   PhysicalIdGenerator, because each rank will generate id ONLY from itself, NOT the master.
    return IdUtil::NewPhysicalSymbolId(GlobalProcessCtx::Rank());
  }
  CHECK_OR_RETURN(GlobalProcessCtx::IsThisProcessMaster());
  return IdUtil::NewLogicalSymbolId();
}

Maybe<int64_t> LogicalIdGenerator::NewObjectId() {
  if (JUST(IsMultiClient())) {
    // NOTE(chengcheng): in Multi-Client LogicalIdGenerator will degenerate directly to
    //   PhysicalIdGenerator, because each rank will generate id ONLY from itself, NOT the master.
    return IdUtil::NewPhysicalObjectId(GlobalProcessCtx::Rank());
  }

  CHECK_OR_RETURN(GlobalProcessCtx::IsThisProcessMaster());
  return IdUtil::NewLogicalObjectId();
}

Maybe<int64_t> PhysicalIdGenerator::NewSymbolId() {
  return IdUtil::NewPhysicalSymbolId(GlobalProcessCtx::Rank());
}

Maybe<int64_t> PhysicalIdGenerator::NewObjectId() {
  return IdUtil::NewPhysicalObjectId(GlobalProcessCtx::Rank());
}

}  // namespace vm
}  // namespace oneflow
