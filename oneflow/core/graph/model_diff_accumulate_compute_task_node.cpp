#include "oneflow/core/graph/model_diff_accumulate_compute_task_node.h"

namespace oneflow {

void MdDiffAccCompTaskNode::FixPackedBlobDescOfProducedRegst() {
  std::shared_ptr<RegstDesc> md_diff_acc_regst = GetProducedRegst("acc");
  CHECK(md_diff_acc_regst->IsLocked());
  Shape& shape = md_diff_acc_regst->MutBlobDesc(GenPackedLbi())->mut_shape();
  CHECK_EQ(1, shape.NumAxes());
  shape = Shape({RoundUp(shape.elem_cnt(), parallel_ctx()->parallel_num())});
}

}  // namespace oneflow
