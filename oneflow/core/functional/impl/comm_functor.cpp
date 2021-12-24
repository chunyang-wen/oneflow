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
#include "oneflow/core/framework/id_util.h"
#include "oneflow/core/framework/attr_value.h"
#include "oneflow/core/framework/nd_sbp.h"
#include "oneflow/core/framework/op_builder.h"
#include "oneflow/core/framework/op_expr.h"
#include "oneflow/core/framework/op_interpreter/op_interpreter_util.h"
#include "oneflow/core/framework/op_interpreter/eager_mirrored_op_interpreter.h"
#include "oneflow/core/framework/op_generated.h"
#include "oneflow/core/framework/tensor.h"
#include "oneflow/core/framework/tensor_tuple.h"
#include "oneflow/core/functional/functional.h"
#include "oneflow/core/functional/function_library.h"
#include "oneflow/core/functional/impl/common.h"
#include "oneflow/core/functional/impl/unary_functor.h"
#include "oneflow/core/ccl/ccl.h"
#include "oneflow/core/job/rank_group_scope.h"
#include "oneflow/core/rpc/include/global_process_ctx.h"
#include "oneflow/core/common/flat_shape.h"

namespace oneflow {
namespace one {
namespace functional {

namespace impl {

namespace {

bool IsAllBroadcastNdSbp(Symbol<cfg::NdSbp> nd_sbp) {
  for (const auto& sbp_parallel : nd_sbp->sbp_parallel()) {
    if (!sbp_parallel.has_broadcast_parallel()) { return false; }
  }
  return true;
}

bool IsAllPartialSumNdSbp(Symbol<cfg::NdSbp> nd_sbp) {
  for (const auto& sbp_parallel : nd_sbp->sbp_parallel()) {
    if (!sbp_parallel.has_partial_sum_parallel()) { return false; }
  }
  return true;
}

bool IsAllSplitNdSbp(Symbol<cfg::NdSbp> nd_sbp, int64_t axis) {
  for (const auto& sbp_parallel : nd_sbp->sbp_parallel()) {
    if (!(sbp_parallel.has_split_parallel() && sbp_parallel.split_parallel().axis() == axis)) {
      return false;
    }
  }
  return true;
}

bool IsSplitSbp(Symbol<cfg::SbpParallel> sbp_parallel) {
  return sbp_parallel->has_split_parallel();
}

Maybe<one::UserOpExpr> EagerNcclAllReduce(Symbol<ParallelDesc> parallel_desc) {
  return one::OpBuilder("eager_nccl_all_reduce", *JUST(UniqueStr("eager_nccl_all_reduce")))
      .Input("in")
      .Output("out")
      .Build();
}

static constexpr auto* CachedEagerNcclAllReduceOpExpr = DECORATE(&EagerNcclAllReduce, ThreadLocal);

Maybe<one::UserOpExpr> EagerNcclReduceScatter(Symbol<ParallelDesc> parallel_desc,
                                              const std::string& op_type) {
  return one::OpBuilder("eager_nccl_reduce_scatter", *JUST(UniqueStr("eager_nccl_reduce_scatter")))
      .Input("in")
      .Output("out")
      .Build();
}
static constexpr auto* CachedNcclReduceScatterOpExpr =
    DECORATE(&EagerNcclReduceScatter, ThreadLocalCopiable);

Maybe<one::UserOpExpr> EagerNcclAllGather(Symbol<ParallelDesc> parallel_desc) {
  return one::OpBuilder("eager_nccl_all_gather", *JUST(UniqueStr("eager_nccl_all_gather")))
      .Input("in")
      .Output("out")
      .Build();
}

static constexpr auto* CachedEagerNcclAllGatherOpExpr = DECORATE(&EagerNcclAllGather, ThreadLocal);

Maybe<one::UserOpExpr> EagerNcclS2S(Symbol<ParallelDesc> parallel_desc,
                                    Symbol<cfg::SbpParallel> src_sbp,
                                    Symbol<cfg::SbpParallel> dst_sbp) {
  return one::OpBuilder("eager_nccl_s2s", *JUST(UniqueStr("eager_nccl_s2s")))
      .Input("in")
      .Output("out")
      .Build();
}

auto* CachedEagerNcclS2SOpExpr = DECORATE(&EagerNcclS2S, ThreadLocal);

Maybe<one::UserOpExpr> EagerNcclReduce(Symbol<ParallelDesc> parallel_desc, int64_t root) {
  return one::OpBuilder("eager_nccl_reduce", *JUST(UniqueStr("eager_nccl_reduce")))
      .Input("in")
      .Output("out")
      .Build();
}

auto* CachedEagerNcclReduceOpExpr = DECORATE(&EagerNcclReduce, ThreadLocal);

}  // namespace

class BroadcastFunctor {
 public:
  BroadcastFunctor() = default;
  Maybe<Tensor> operator()(const std::shared_ptr<one::Tensor>& x, int64_t src_rank,
                           bool inplace) const {
    const auto& rank_group = JUST(RankGroupScope::CurrentRankGroup());
    std::string device_type_str = JUST(x->device())->type();
    CHECK_OR_RETURN(device_type_str == "cuda" || device_type_str == "cpu");
    DeviceType device_type = device_type_str == "cuda" ? DeviceType::kCUDA : DeviceType::kCPU;
    const auto& parallel_desc = JUST(RankGroup::GetDefaultParallelDesc(device_type, rank_group));
    return one::Broadcast(x, src_rank, parallel_desc, inplace);
  }
};

namespace {

Maybe<one::UserOpExpr> RankGroupAndDeviceType2AllReduceOpExpr(Symbol<RankGroup> rank_group,
                                                              DeviceType device_type) {
  const auto& parallel_desc = JUST(RankGroup::GetDefaultParallelDesc(device_type, rank_group));
  return one::OpBuilder("eager_nccl_all_reduce")
      .Input("in")
      .Output("out")
      // TODO(hjchen2)
      // .Attr<std::string>("parallel_conf", PbMessage2TxtString(parallel_desc->parallel_conf()))
      // .Attr<bool>("async_launch", true)
      .Build();
}

auto* CachedRankGroupAndDeviceType2AllReduceOpExpr =
    DECORATE(&RankGroupAndDeviceType2AllReduceOpExpr, ThreadLocal);

}  // namespace

class LocalAllReduceFunctor {
 public:
  LocalAllReduceFunctor() = default;
  Maybe<Tensor> operator()(const std::shared_ptr<one::Tensor>& x) const {
    const auto& device = JUST(x->device());
    CHECK_EQ_OR_RETURN(device->device_id(), GlobalProcessCtx::LocalRank());
    const auto& rank_group = JUST(RankGroupScope::CurrentRankGroup());
    const std::string& device_type_str = device->type();
    CHECK_OR_RETURN(device_type_str == "cuda" || device_type_str == "cpu");
    DeviceType device_type = device_type_str == "cuda" ? DeviceType::kCUDA : DeviceType::kCPU;
    std::shared_ptr<OpExpr> op_expr =
        JUST(CachedRankGroupAndDeviceType2AllReduceOpExpr(rank_group, device_type));
    if (const auto& static_zeros_tensor = std::dynamic_pointer_cast<StaticZerosTensor>(x)) {
      return OpInterpUtil::Dispatch<Tensor>(*op_expr,
                                            {JUST(static_zeros_tensor->AsMirroredTensor())}, {});
    } else {
      return OpInterpUtil::Dispatch<Tensor>(*op_expr, {x}, {});
    }
  }
};

class ConsistentAllReduceFunctor {
 public:
  ConsistentAllReduceFunctor() = default;
  Maybe<Tensor> operator()(const std::shared_ptr<one::Tensor>& x) const {
    {
      CHECK_OR_RETURN(x->is_consistent());
      CHECK_OR_RETURN(IsAllPartialSumNdSbp(JUST(x->nd_sbp())));
    }
    std::shared_ptr<OpExpr> op_expr =
        JUST(CachedEagerNcclAllReduceOpExpr(JUST(x->parallel_desc())));
    auto ctx = std::make_shared<schema::EagerNcclAllReduceOp>();
    ctx->set_parallel_conf(PbMessage2TxtString(JUST(x->parallel_desc())->parallel_conf()));
    ctx->set_async_launch(false);
    return JUST(OpInterpUtil::Dispatch<Tensor>(*op_expr, {x}, ctx));
  }
};

class ConsistentReduceScatterFunctor {
 public:
  ConsistentReduceScatterFunctor() = default;
  Maybe<Tensor> operator()(const std::shared_ptr<one::Tensor>& x,
                           const std::string& op_type) const {
    {
      CHECK_OR_RETURN(x->is_consistent());
      if (op_type == "max") {
        CHECK_OR_RETURN(IsAllBroadcastNdSbp(JUST(x->nd_sbp())));
        CHECK_EQ_OR_RETURN(JUST(x->parallel_desc())->device_type(), DeviceType::kCUDA);
      } else if (op_type == "sum") {
        CHECK_OR_RETURN(IsAllPartialSumNdSbp(JUST(x->nd_sbp())));
      } else {
        UNIMPLEMENTED_THEN_RETURN();
      }
    }
    std::shared_ptr<OpExpr> op_expr =
        JUST(CachedNcclReduceScatterOpExpr(JUST(x->parallel_desc()), op_type));
    auto ctx =
        std::make_shared<schema::EagerNcclReduceScatterOp>();
    ctx->set_parallel_conf(PbMessage2TxtString(JUST(x->parallel_desc())->parallel_conf()));
    ctx->set_op_type(op_type);
    return JUST(OpInterpUtil::Dispatch<Tensor>(*op_expr, {x}, ctx));
  }
};

class ConsistentAllGatherFunctor {
 public:
  ConsistentAllGatherFunctor() = default;
  Maybe<Tensor> operator()(const std::shared_ptr<one::Tensor>& x) const {
    {
      CHECK_OR_RETURN(x->is_consistent());
      CHECK_OR_RETURN(IsAllSplitNdSbp(JUST(x->nd_sbp()), 0));
    }
    std::shared_ptr<OpExpr> op_expr =
        JUST(CachedEagerNcclAllGatherOpExpr(JUST(x->parallel_desc())));
    auto ctx = std::make_shared<schema::EagerNcclAllGatherOp>();
    ctx->set_parallel_conf(PbMessage2TxtString(JUST(x->parallel_desc())->parallel_conf()));
    return JUST(OpInterpUtil::Dispatch<Tensor>(*op_expr, {x}, ctx));
  }
};

class ConsistentS2SFunctor {
 public:
  ConsistentS2SFunctor() = default;
  Maybe<Tensor> operator()(const std::shared_ptr<one::Tensor>& x,
                           const std::vector<Symbol<cfg::SbpParallel>>& sbp_parallels) const {
    Symbol<cfg::NdSbp> in_nd_sbp = JUST(x->nd_sbp());
    Symbol<cfg::NdSbp> out_nd_sbp = JUST(GetNdSbp(sbp_parallels));
    {
      CHECK_OR_RETURN(x->is_consistent());
      CHECK_EQ_OR_RETURN(in_nd_sbp->sbp_parallel_size(), 1);
      CHECK_OR_RETURN(IsSplitSbp(in_nd_sbp->sbp_parallel(0)));
      CHECK_EQ_OR_RETURN(out_nd_sbp->sbp_parallel_size(), 1);
      CHECK_OR_RETURN(IsSplitSbp(out_nd_sbp->sbp_parallel(0)));
      CHECK_NE_OR_RETURN(in_nd_sbp->sbp_parallel(0).split_parallel().axis(),
                         out_nd_sbp->sbp_parallel(0).split_parallel().axis());
    }
    std::shared_ptr<OpExpr> op_expr = JUST(
        CachedEagerNcclS2SOpExpr(JUST(x->parallel_desc()), SymbolOf(in_nd_sbp->sbp_parallel(0)),
                                 SymbolOf(out_nd_sbp->sbp_parallel(0))));
    auto ctx = std::make_shared<schema::EagerNcclS2sOp>();
    ctx->set_in_split_axis(in_nd_sbp->sbp_parallel(0).split_parallel().axis());
    ctx->set_out_split_axis(out_nd_sbp->sbp_parallel(0).split_parallel().axis());
    ctx->set_parallel_conf(PbMessage2TxtString(JUST(x->parallel_desc())->parallel_conf()));
    return JUST(OpInterpUtil::Dispatch<Tensor>(*op_expr, {x}, ctx));
  }
};

class SendFunctor {
 public:
  SendFunctor() { op_expr_ = CHECK_JUST(one::OpBuilder("send").Input("in").Build()); }
  Maybe<void> operator()(const std::shared_ptr<one::Tensor>& x, int64_t dst, bool send_meta) const {
    auto ctx = std::make_shared<schema::SendOp>();
    ctx->set_dst_process_id(dst);
    if (send_meta) {
      std::shared_ptr<FlatShape> flat_shape = JUST(FlatShape::New(*x->shape()));
      JUST(ccl::Send<DeviceType::kCPU>(flat_shape.get(), sizeof(*flat_shape), DataType::kChar, dst,
                                       nullptr));

      DataType dtype = x->dtype()->data_type();
      JUST(ccl::Send<DeviceType::kCPU>(&dtype, sizeof(dtype), DataType::kChar, dst, nullptr));

      DeviceType device_type = JUST(Device::GetPlacement(*JUST(x->device())))->device_type();
      JUST(ccl::Send<DeviceType::kCPU>(&device_type, sizeof(device_type), DataType::kChar, dst,
                                       nullptr));
    }
    JUST(OpInterpUtil::Dispatch<TensorTuple>(*op_expr_, {x}, ctx));
    return Maybe<void>::Ok();
  }

 private:
  std::shared_ptr<OpExpr> op_expr_;
};

class RecvFunctor {
 public:
  RecvFunctor() { op_expr_ = CHECK_JUST(one::OpBuilder("recv").Output("out").Build()); }
  Maybe<Tensor> operator()(int64_t src, const Optional<Shape>& optional_shape,
                           const Optional<Symbol<DType>>& optional_dtype,
                           const Optional<Symbol<Device>>& optional_device,
                           const Optional<one::Tensor>& out) const {
    auto ctx = std::make_shared<schema::RecvOp>();
    ctx->set_src_process_id(src);
    Shape shape;
    DataType data_type = DataType::kInvalidDataType;
    Symbol<Device> device;
    if (optional_shape.has_value() && optional_dtype.has_value() && optional_device.has_value()) {
      shape = *JUST(optional_shape);
      data_type = JUST(optional_dtype)->data_type();
      device = JUST(optional_device);
    } else if (!optional_shape.has_value() && !optional_dtype.has_value()
               && !optional_device.has_value()) {
      FlatShape flat_shape{};
      JUST(ccl::Recv<DeviceType::kCPU>(&flat_shape, sizeof(flat_shape), DataType::kChar, src,
                                       nullptr));
      shape = *JUST(flat_shape.ToShape());

      JUST(ccl::Recv<DeviceType::kCPU>(&data_type, sizeof(data_type), DataType::kChar, src,
                                       nullptr));

      DeviceType device_type = DeviceType::kInvalidDevice;
      JUST(ccl::Recv<DeviceType::kCPU>(&device_type, sizeof(device_type), DataType::kChar, src,
                                       nullptr));
      device = JUST(Device::New(Device::Type4DeviceTag(*JUST(DeviceTag4DeviceType(device_type)))));
    } else {
      UNIMPLEMENTED_THEN_RETURN() << "All or none of shape, dtype and device should have value.";
    }
    ctx->set_shape(shape);
    ctx->set_dtype(data_type);
    ctx->set_device_type(device->type());
    ctx->set_device_id(device->device_id());
    ctx->device = device;
    if (out.has_value()) {
      std::shared_ptr<one::Tensor> out_tensor = JUST(out);
      Symbol<Device> out_tensor_device = JUST(out_tensor->device());
      CHECK_OR_RETURN(out_tensor_device == device);
      std::shared_ptr<TensorTuple> outputs = std::make_shared<TensorTuple>(1);
      outputs->at(0) = out_tensor;
      JUST(OpInterpUtil::Dispatch(*op_expr_, {}, outputs.get(), ctx));
      return outputs->at(0);
    }
    return OpInterpUtil::Dispatch<Tensor>(*op_expr_, {}, ctx);
  }

 private:
  std::shared_ptr<OpExpr> op_expr_;
};

class LocalReduceFunctor {
 public:
  LocalReduceFunctor() = default;
  Maybe<Tensor> operator()(const std::shared_ptr<one::Tensor>& x, int64_t dst, bool inplace) const {
    const auto& device = JUST(x->device());
    { CHECK_EQ_OR_RETURN(device->device_id(), GlobalProcessCtx::LocalRank()); }
    static thread_local std::unordered_map<Symbol<RankGroup>, Symbol<ParallelDesc>>
        rank_group2parallel_desc;
    const auto& rank_group = JUST(RankGroupScope::CurrentRankGroup());
    auto iter = rank_group2parallel_desc.find(rank_group);
    Symbol<ParallelDesc> parallel_desc;
    if (iter == rank_group2parallel_desc.end()) {
      ParallelConf parallel_conf;
      parallel_conf.set_device_tag(JUST(device->of_type()));
      JUST(rank_group->ForEachRank([&parallel_conf](int64_t rank) -> Maybe<void> {
        parallel_conf.add_device_name("@" + std::to_string(rank) + ":"
                                      + std::to_string(GlobalProcessCtx::LocalRank(rank)));
        return Maybe<void>::Ok();
      }));
      parallel_desc = SymbolOf(ParallelDesc(parallel_conf));
      rank_group2parallel_desc[rank_group] = parallel_desc;
    } else {
      parallel_desc = iter->second;
    }
    std::shared_ptr<OpExpr> op_expr = JUST(CachedEagerNcclReduceOpExpr(parallel_desc, dst));
    auto ctx = std::make_shared<schema::EagerNcclReduceOp>();
    ctx->set_parallel_conf(PbMessage2TxtString(JUST(x->parallel_desc())->parallel_conf()));
    ctx->set_root(dst);
    if (inplace) {
      TensorTuple outputs{x};
      JUST(OpInterpUtil::Dispatch(*op_expr, {x}, &outputs, ctx));
      return x;
    } else {
      return JUST(OpInterpUtil::Dispatch<Tensor>(*op_expr, {x}, ctx));
    }
  }
};

}  // namespace impl

ONEFLOW_FUNCTION_LIBRARY(m) {
  m.add_functor<impl::BroadcastFunctor>("Broadcast");
  m.add_functor<impl::LocalAllReduceFunctor>("LocalAllReduce");
  m.add_functor<impl::ConsistentAllReduceFunctor>("ConsistentAllReduce");
  m.add_functor<impl::ConsistentReduceScatterFunctor>("ConsistentReduceScatter");
  m.add_functor<impl::ConsistentAllGatherFunctor>("ConsistentAllGather");
  m.add_functor<impl::ConsistentS2SFunctor>("ConsistentS2S");
  m.add_functor<impl::SendFunctor>("Send");
  m.add_functor<impl::RecvFunctor>("Recv");
  m.add_functor<impl::LocalReduceFunctor>("LocalReduce");
};

}  // namespace functional
}  // namespace one
}  // namespace oneflow
