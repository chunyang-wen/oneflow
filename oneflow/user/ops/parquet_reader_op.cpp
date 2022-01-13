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
#include "oneflow/core/framework/op_generated.h"
#include "oneflow/user/data/parquet_util.h"

namespace oneflow {

Maybe<void> ParquetReaderOp::InferLogicalTensorDesc(user_op::InferContext* ctx) {
  using namespace data;
  ParquetColumnSchema schema;
  ParseParquetColumnSchemaFromJson(&schema, ctx->Attr<std::string>("schema_json_str"));
  int64_t batch_size = ctx->Attr<int64_t>("batch_size");
  int output_index = 0;
  for (const auto& col_desc : schema.col_descs) {
    user_op::TensorDesc* out_desc = ctx->OutputTensorDesc("out", output_index);
    if (col_desc.is_variadic) {
      *out_desc->mut_shape() = Shape({batch_size});
    } else {
      DimVector dim_vec({batch_size});
      for (auto dim : col_desc.shape.dim_vec()) { dim_vec.push_back(dim); }
      *out_desc->mut_shape() = Shape(dim_vec);
    }
    output_index++;
  }
  return Maybe<void>::Ok();
}

Maybe<void> ParquetReaderOp::InferDataType(user_op::InferContext* ctx) {
  using namespace data;
  ParquetColumnSchema schema;
  ParseParquetColumnSchemaFromJson(&schema, ctx->Attr<std::string>("schema_json_str"));
  int output_index = 0;
  for (const auto& col_desc : schema.col_descs) {
    auto* dtype = ctx->OutputTensorDesc("out", output_index)->mut_data_type();
    if (col_desc.is_variadic) {
      *dtype = DataType::kTensorBuffer;
    } else {
      *dtype = col_desc.dtype;
    }
    output_index++;
  }
  return Maybe<void>::Ok();
}

Maybe<void> ParquetReaderOp::GetSbp(user_op::SbpContext* ctx) {
  ctx->NewBuilder().Split(ctx->outputs(), 0).Build();
  return Maybe<void>::Ok();
}

Maybe<void> ParquetReaderOp::InferNdSbp(user_op::InferNdSbpFnContext* ctx) {
  cfg::SbpParallel default_sbp;
  default_sbp.mutable_split_parallel()->set_axis(0);
  return user_op::InferNdSbp4SrcOp(ctx, default_sbp);
}

}  // namespace oneflow