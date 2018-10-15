#include "oneflow/core/actor/input_wise_compute_actor.h"

namespace oneflow {

void InputWiseCompActor::Init(const TaskProto& task_proto) {
  CHECK_EQ(1, exec_kernel_vec().size());
  const auto& input_bns =
      task_proto.exec_sequence().exec_node().Get(0).kernel_conf().op_attribute().input_bns();
  HashMap<std::string, int64_t> ibn2in_bn_id;
  for (int64_t i = 0; i < input_bns.size(); ++i) {
    CHECK(ibn2in_bn_id.emplace(input_bns.Get(i), i).second);
  }
  for (const auto& pair : exec_kernel_vec().at(0).bn_in_op2regst_desc_id) {
    auto it = ibn2in_bn_id.find(pair.first);
    if (it != ibn2in_bn_id.end()) {
      CHECK(regst_desc_id2in_bn_id_.emplace(pair.second, it->second).second);
    }
  }

  for (const auto& pair : task_proto.consumed_regst_desc_id()) {
    for (int64_t regst_desc_id : pair.second.regst_desc_id()) {
      consumed_rs_.InsertRegstDescId(regst_desc_id);
      CHECK(regst_desc_id2is_processed_.emplace(regst_desc_id, false).second);
    }
  }
  consumed_rs_.InitedDone();
  cur_processed_regst_desc_id_ = -1;
  processed_regst_desc_id_cnt_ = 0;
  OF_SET_MSG_HANDLER(&InputWiseCompActor::HandlerNormal);
}

int64_t InputWiseCompActor::ActNumForEachOutput(int64_t regst_desc_id) const {
  return regst_desc_id2in_bn_id_.size();
}

void InputWiseCompActor::NormalProcessCustomizedReadableRegstMsg(const ActorMsg& msg) {
  CHECK_EQ(0, consumed_rs_.TryPushBackRegst(msg.regst()));
}

bool InputWiseCompActor::IsCustomizedReadReady() {
  CHECK_EQ(-1, cur_processed_regst_desc_id_);
  consumed_rs_.ForChosenRegstDeq([this](int64_t) { return cur_processed_regst_desc_id_ == -1; },
                                 [this](const std::deque<Regst*>& reg_deq) {
                                   if (reg_deq.empty()) { return; }
                                   int64_t regst_desc_id = reg_deq.front()->regst_desc_id();
                                   if (regst_desc_id2is_processed_.at(regst_desc_id) == false) {
                                     cur_processed_regst_desc_id_ = regst_desc_id;
                                   }
                                 });
  return cur_processed_regst_desc_id_ != -1;
}

void InputWiseCompActor::ForEachCurCustomizedReadableRegst(
    std::function<void(const Regst*)> handler) const {
  handler(consumed_rs_.Front(cur_processed_regst_desc_id_));
}

void InputWiseCompActor::Act() {
  Regst* cur_regst = consumed_rs_.Front(cur_processed_regst_desc_id_);
  CHECK(cur_regst);

  KernelCtx kernel_ctx = GenDefaultKernelCtx();
  SetKernelCtxOther(&(kernel_ctx.other));
  AsyncLaunchKernel(kernel_ctx, [&](int64_t regst_desc_id) -> Regst* {
    if (cur_processed_regst_desc_id_ == regst_desc_id) {
      return cur_regst;
    } else {
      return nullptr;
    }
  });
  processed_regst_desc_id_cnt_ += 1;
  regst_desc_id2is_processed_.at(cur_processed_regst_desc_id_) = true;
}

void InputWiseCompActor::VirtualAsyncSendNaiveProducedRegstMsgToConsumer() {
  if (processed_regst_desc_id_cnt_ == regst_desc_id2is_processed_.size()) {
    HandleProducedNaiveDataRegstToConsumer([this](Regst* regst) {
      regst->set_piece_id(consumed_rs_.Front(cur_processed_regst_desc_id_)->piece_id());
      return true;
    });
    for (auto& pair : regst_desc_id2is_processed_) {
      CHECK(pair.second);
      pair.second = false;
    }
    processed_regst_desc_id_cnt_ = 0;
  }
}

void InputWiseCompActor::AsyncSendCustomizedConsumedRegstMsgToProducer() {
  Regst* cur_regst = consumed_rs_.Front(cur_processed_regst_desc_id_);
  CHECK(cur_regst);
  AsyncSendRegstMsgToProducer(cur_regst);
  CHECK_EQ(0, consumed_rs_.TryPopFrontRegst(cur_processed_regst_desc_id_));
  cur_processed_regst_desc_id_ = -1;
}

void InputWiseCompActor::AsyncReturnAllCustomizedReadableRegst() {
  CHECK_EQ(-1, cur_processed_regst_desc_id_);
  CHECK_EQ(0, processed_regst_desc_id_cnt_);
  CHECK_EQ(0, consumed_rs_.available_regst_desc_cnt());
}

bool InputWiseCompActor::ProducedCtrlRegstValid(int64_t regst_desc_id) const { return true; }

}  // namespace oneflow
