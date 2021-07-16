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
#include "oneflow/core/framework/framework.h"

namespace oneflow {

REGISTER_USER_OP("smooth_l1_loss")
    .Input("prediction")
    .Input("label")
    .Output("loss")
    .Attr<float>("beta")
    .SetTensorDescInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      const Shape& prediction_shape = ctx->InputShape("prediction", 0);
      const Shape& label_shape = ctx->InputShape("label", 0);
      CHECK_EQ_OR_RETURN(prediction_shape, label_shape);
      CHECK_GE_OR_RETURN(ctx->Attr<float>("beta"), 0);
      *ctx->OutputShape("loss", 0) = prediction_shape;
      return Maybe<void>::Ok();
    })
    .SetInputArgModifyFn([](user_op::GetInputArgModifier GetInputArgModifierFn,
                            const user_op::UserOpConfWrapper&) -> Maybe<void> {
      user_op::InputArgModifier* label_modifier = GetInputArgModifierFn("label", 0);
      CHECK_OR_RETURN(label_modifier != nullptr);
      label_modifier->set_requires_grad(false);
      return Maybe<void>::Ok();
    })
    .SetGetSbpFn([](user_op::SbpContext* ctx) -> Maybe<void> {
      const user_op::TensorDesc& prediction_tensor =
          ctx->LogicalTensorDesc4InputArgNameAndIndex("prediction", 0);
      FOR_RANGE(int64_t, i, 0, prediction_tensor.shape().NumAxes()) {
        ctx->NewBuilder().Split(ctx->inputs(), i).Split(ctx->outputs(), i).Build();
      }
      return Maybe<void>::Ok();
    })
    .SetDataTypeInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      CHECK_EQ_OR_RETURN(ctx->InputDType("prediction", 0), ctx->InputDType("label", 0));
      *ctx->OutputDType("loss", 0) = ctx->InputDType("prediction", 0);
      return Maybe<void>::Ok();
    });

REGISTER_USER_OP("smooth_l1_loss_grad")
    .Input("loss_grad")
    .Input("prediction")
    .Input("label")
    .Output("prediction_grad")
    .Attr<float>("beta")
    .SetTensorDescInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      const Shape& loss_grad_shape = ctx->InputShape("loss_grad", 0);
      const Shape& prediction_shape = ctx->InputShape("prediction", 0);
      const Shape& label_shape = ctx->InputShape("label", 0);
      CHECK_EQ_OR_RETURN(loss_grad_shape, prediction_shape);
      CHECK_EQ_OR_RETURN(prediction_shape, label_shape);
      CHECK_GE_OR_RETURN(ctx->Attr<float>("beta"), 0);
      *ctx->OutputShape("prediction_grad", 0) = loss_grad_shape;
      return Maybe<void>::Ok();
    })
    .SetGetSbpFn([](user_op::SbpContext* ctx) -> Maybe<void> {
      const user_op::TensorDesc& prediction_tensor =
          ctx->LogicalTensorDesc4InputArgNameAndIndex("prediction", 0);
      FOR_RANGE(int64_t, i, 0, prediction_tensor.shape().NumAxes()) {
        ctx->NewBuilder().Split(ctx->inputs(), i).Split(ctx->outputs(), i).Build();
      }
      return Maybe<void>::Ok();
    })
    .SetDataTypeInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      CHECK_EQ_OR_RETURN(ctx->InputDType("loss_grad", 0), ctx->InputDType("prediction", 0));
      CHECK_EQ_OR_RETURN(ctx->InputDType("prediction", 0), ctx->InputDType("label", 0));
      *ctx->OutputDType("prediction_grad", 0) = ctx->InputDType("loss_grad", 0);
      return Maybe<void>::Ok();
    });

REGISTER_USER_OP_GRAD("smooth_l1_loss")
    .SetGenBackwardOpConfFn([](const user_op::UserOpWrapper& op, user_op::AddOpFn AddOp) {
      if (op.NeedGenGradTensor4OpInput("prediction", 0)) {
        user_op::UserOpConfWrapperBuilder builder(op.op_name() + "_grad");
        user_op::UserOpConfWrapper grad_op =
            builder.Op("smooth_l1_loss_grad")
                .Input("loss_grad", op.GetGradTensorWithOpOutput("loss", 0))
                .Input("prediction", op.input("prediction", 0))
                .Input("label", op.input("label", 0))
                .Output("prediction_grad")
                .Attr("beta", op.attr<float>("beta"))
                .Build();
        op.BindGradTensorWithOpInput(grad_op.output("prediction_grad", 0), "prediction", 0);
        AddOp(grad_op);
      }
    });

}  // namespace oneflow
