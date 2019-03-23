#include "oneflow/core/operator/naive_model_update_op.h"

namespace oneflow {

const PbMessage& NaiveModelUpdateOp::GetCustomizedConf() const {
  if (Global<JobDesc>::Get()->IsTrain()) {
    return op_conf().normal_mdupdt_conf();
  } else if (Global<JobDesc>::Get()->other_conf().predict_conf().has_tmp_split_fw_bw_train_conf()) {
    return op_conf().naive_model_update_conf();
  } else {
    UNIMPLEMENTED();
  }
}

REGISTER_CLASS(NormalModelUpdateOpUserConf::kNaiveConf, NormalModelUpdtOp, NaiveModelUpdateOp);

REGISTER_OP(OperatorConf::kNaiveModelUpdateConf, NaiveModelUpdateOp);

}  // namespace oneflow
