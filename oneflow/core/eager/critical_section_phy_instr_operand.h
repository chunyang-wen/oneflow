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
#ifndef ONEFLOW_CORE_EAGER_CRITICAL_SECTION_PHY_INSTR_OPERAND_H_
#define ONEFLOW_CORE_EAGER_CRITICAL_SECTION_PHY_INSTR_OPERAND_H_

#include "oneflow/core/vm/instruction_operand.msg.h"
#include "oneflow/core/eager/eager_blob_object.h"
#include "oneflow/core/framework/nn_graph_if.h"

namespace oneflow {

namespace one {

using EagerBlobObjectListPtr =
    std::shared_ptr<const std::vector<std::shared_ptr<vm::EagerBlobObject>>>;

}

namespace vm {

class CriticalSectionBeginPhyInstrOperand : public PhyInstrOperand {
 public:
  CriticalSectionBeginPhyInstrOperand(const CriticalSectionBeginPhyInstrOperand&) = delete;
  CriticalSectionBeginPhyInstrOperand(CriticalSectionBeginPhyInstrOperand&&) = delete;
  CriticalSectionBeginPhyInstrOperand& operator=(const CriticalSectionBeginPhyInstrOperand&) = delete;
  CriticalSectionBeginPhyInstrOperand& operator=(CriticalSectionBeginPhyInstrOperand&&) = delete;
  virtual ~CriticalSectionBeginPhyInstrOperand() = default;

  explicit CriticalSectionBeginPhyInstrOperand(const one::EagerBlobObjectListPtr& eager_blob_objects, ObjectMsgPtr<LocalDepObject> local_dep_object)
      : eager_blob_objects_(eager_blob_objects), local_dep_object_(local_dep_object) {}

  void ForEachMirroredObject(
      const std::function<void(vm::MirroredObject* infer, vm::MirroredObject* compute)>&) const;

  void ForEachMutMirroredObject(
      const std::function<void(vm::MirroredObject* infer, vm::MirroredObject* compute)>&)
      const override;

  LocalDepObject* mut_local_dep_object() const { return local_dep_object_.Mutable(); }

 protected:
  one::EagerBlobObjectListPtr eager_blob_objects_;
  mutable ObjectMsgPtr<LocalDepObject> local_dep_object_;
};

class InputCriticalSectionBeginPhyInstrOperand final : public CriticalSectionBeginPhyInstrOperand {
 public:
  InputCriticalSectionBeginPhyInstrOperand(const one::EagerBlobObjectListPtr& eager_blob_objects)
      : CriticalSectionBeginPhyInstrOperand(eager_blob_objects) {}

  ~InputCriticalSectionBeginPhyInstrOperand() override = default;

  // for inputs
  void ForEachConstMirroredObject(
      const std::function<void(vm::MirroredObject* infer, vm::MirroredObject* compute)>& DoEach)
      const override {
    ForEachMirroredObject(DoEach);
  }

  // for outputs
  void ForEachMut2MirroredObject(
      const std::function<void(vm::MirroredObject* infer, vm::MirroredObject* compute)>&)
      const override {}
};

class OutputCriticalSectionBeginPhyInstrOperand final : public CriticalSectionBeginPhyInstrOperand {
 public:
  OutputCriticalSectionBeginPhyInstrOperand(const one::EagerBlobObjectListPtr& eager_blob_objects)
      : CriticalSectionBeginPhyInstrOperand(eager_blob_objects) {}

  ~OutputCriticalSectionBeginPhyInstrOperand() override = default;

  // for inputs
  void ForEachConstMirroredObject(
      const std::function<void(vm::MirroredObject* infer, vm::MirroredObject* compute)>&)
      const override {}

  // for outputs
  void ForEachMut2MirroredObject(
      const std::function<void(vm::MirroredObject* infer, vm::MirroredObject* compute)>& DoEach)
      const override {
    ForEachMirroredObject(DoEach);
  }
};

}  // namespace vm
}  // namespace oneflow

#endif  // ONEFLOW_CORE_EAGER_CRITICAL_SECTION_PHY_INSTR_OPERAND_H_
