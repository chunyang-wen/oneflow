#include "oneflow/core/operator/momentum_model_update_op.h"

namespace oneflow {

void MomentumModelUpdateOp::MdUpdtVirtualInitFromOpConf() {
  if (Global<JobDesc>::Get()->IsTrain()) {
    EnrollForwardModelBn("momentum");
  } else if (Global<JobDesc>::Get()->other_conf().predict_conf().has_tmp_split_fw_bw_train_conf()) {
    EnrollInputBn("momentum", false)->set_is_mutable(true);
  } else {
    UNIMPLEMENTED();
  }
}

void MomentumModelUpdateOp::MdUpdtVirtualInferBlobDescs(
    std::function<BlobDesc*(const std::string&)> GetBlobDesc4BnInOp,
    const ParallelContext* parallel_ctx) const {
  const BlobDesc* model_blob_desc = GetBlobDesc4BnInOp("model");
  CHECK_EQ(model_blob_desc->data_type(), Global<JobDesc>::Get()->DefaultDataType());
  CHECK_EQ(model_blob_desc->has_data_id_field(), false);
  if (Global<JobDesc>::Get()->IsTrain()) {
    *GetBlobDesc4BnInOp("momentum") = *model_blob_desc;
  } else if (Global<JobDesc>::Get()->other_conf().predict_conf().has_tmp_split_fw_bw_train_conf()) {
    CHECK(*GetBlobDesc4BnInOp("momentum") == *model_blob_desc);
  } else {
    UNIMPLEMENTED();
  }
}

const PbMessage& MomentumModelUpdateOp::GetCustomizedConf() const {
  if (Global<JobDesc>::Get()->IsTrain()) {
    return op_conf().normal_mdupdt_conf();
  } else if (Global<JobDesc>::Get()->other_conf().predict_conf().has_tmp_split_fw_bw_train_conf()) {
    return op_conf().momentum_model_update_conf();
  } else {
    UNIMPLEMENTED();
  }
}

REGISTER_CLASS(NormalModelUpdateOpUserConf::kMomentumConf, NormalModelUpdtOp,
               MomentumModelUpdateOp);

REGISTER_OP(OperatorConf::kMomentumModelUpdateConf, MomentumModelUpdateOp);

}  // namespace oneflow
