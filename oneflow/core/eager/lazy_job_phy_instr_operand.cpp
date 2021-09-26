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
#include "oneflow/core/eager/lazy_job_phy_instr_operand.h"
#include "oneflow/core/framework/device.h"

namespace oneflow {
namespace vm {

namespace {

Maybe<LocalDepObject*> RawGetEagerNcclLocalDepObject(const std::string& type) {
  const auto& device = JUST(Device::New(type));
  const auto& local_dep_object = device->mut_transport_local_dep_object();
  CHECK_OR_RETURN(local_dep_object.has_value());
  return JUST(local_dep_object);
}

static constexpr auto* GetEagerNcclLocalDepObject =
    DECORATE(&RawGetEagerNcclLocalDepObject, ThreadLocalCopiable);

}  // namespace


void LaunchLazyJobPhyInstrOperand::ForEachMutMirroredObject(
    const std::function<void(vm::MirroredObject* infer, vm::MirroredObject* compute)>& DoEach)
    const {
  DoEach(nullptr, inputs_critical_section_->mut_local_dep_object()->mut_mirrored_object());
  DoEach(nullptr, outputs_critical_section_->mut_local_dep_object()->mut_mirrored_object());

  for (const auto& eager_blob_object : *param_blob_objects_) {
    DoEach(nullptr,
           CHECK_JUST(eager_blob_object->compute_local_dep_object())->mut_mirrored_object());
  }

#ifdef WITH_CUDA
  auto* sync_launched_nccl = CHECK_JUST(GetEagerNcclLocalDepObject("sync_launched_nccl"));
  auto* async_launched_nccl = CHECK_JUST(GetEagerNcclLocalDepObject("async_launched_nccl"));
  CHECK_EQ(sync_launched_nccl, async_launched_nccl);
  DoEach(nullptr, async_launched_nccl->mut_mirrored_object());
#endif  // WITH_CUDA
}

void LaunchLazyJobPhyInstrOperand::ForEachConstMirroredObject(
    const std::function<void(vm::MirroredObject* infer, vm::MirroredObject* compute)>& DoEach)
    const {
  for (auto& local_dep_object : input_local_dep_objects_) {
    DoEach(nullptr, local_dep_object->mut_mirrored_object());
  }
}

void LaunchLazyJobPhyInstrOperand::ForEachMut2MirroredObject(
    const std::function<void(vm::MirroredObject* infer, vm::MirroredObject* compute)>& DoEach)
    const {
  for (auto& local_dep_object : output_local_dep_objects_) {
    DoEach(nullptr, local_dep_object->mut_mirrored_object());
  }
}

}  // namespace vm
}  // namespace oneflow
