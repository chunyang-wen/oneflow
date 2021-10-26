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
#include "oneflow/user/kernels/sparse_cross_entropy_kernel_util.h"
#include "oneflow/core/framework/framework.h"
#include "oneflow/core/cuda/softmax.cuh"
#include "oneflow/core/kernel/cuda_graph_support.h"

namespace oneflow {
namespace user_op {

namespace {

template<typename T>
void ComputeProb(DeviceCtx* ctx, const int64_t row, const int64_t col, const T* in, T* prob) {
  using ComputeType = typename cuda::softmax::DefaultComputeType<T>::type;
  cuda::softmax::DirectLoad<T, ComputeType> load(in, col);
  cuda::softmax::DirectStore<ComputeType, T> store(prob, col);
  OF_CUDA_CHECK((cuda::softmax::DispatchLogSoftmax<decltype(load), decltype(store), ComputeType>(
      ctx->cuda_stream(), load, store, row, col)));
}

template<>
void ComputeProb(DeviceCtx* ctx, const int64_t row, const int64_t col, const float16* in,
                 float16* prob) {
  cuda::softmax::DirectLoad<half, float> load(reinterpret_cast<const half*>(in), col);
  cuda::softmax::DirectStore<float, half> store(reinterpret_cast<half*>(prob), col);
  OF_CUDA_CHECK((cuda::softmax::DispatchLogSoftmax<decltype(load), decltype(store), float>(
      ctx->cuda_stream(), load, store, row, col)));
}

template<typename T, typename K>
__global__ void ComputeSparseSoftmaxCrossEntropyResultGpu(const int64_t num_instances,
                                                          const int64_t num_classes,
                                                          const int64_t depth,
                                                          const int64_t lower_bound,
                                                          const K* labels, const T* prob, T* out) {
  CUDA_1D_KERNEL_LOOP(i, num_instances) {
    assert(labels[i] >= 0);
    assert(labels[i] < depth);
    K label = labels[i] - lower_bound;
    if (label >= 0 && label < num_classes) { out[i] = -prob[i * num_classes + label]; }
  }
}
template<typename T, typename K>
inline typename std::enable_if<std::is_floating_point<T>::value, void>::type
ComputeSparseSoftmaxCrossEntropyResult(DeviceCtx* ctx, const int64_t num_instances,
                                       const int64_t num_classes, const int64_t depth,
                                       const int64_t lower_bound, const K* labels, const T* prob,
                                       T* out) {
  ComputeSparseSoftmaxCrossEntropyResultGpu<T, K>
      <<<BlocksNum4ThreadsNum(num_instances), kCudaThreadsNumPerBlock, 0, ctx->cuda_stream()>>>(
          num_instances, num_classes, depth, lower_bound, labels, prob, out);
}
template<typename T, typename K>
inline typename std::enable_if<std::is_same<T, float16>::value, void>::type
ComputeSparseSoftmaxCrossEntropyResult(DeviceCtx* ctx, const int64_t num_instances,
                                       const int64_t num_classes, const int64_t depth,
                                       const int64_t lower_bound, const K* labels, const T* prob,
                                       T* out) {
#if __CUDA_ARCH__ >= 530 || !defined(__CUDA_ARCH__)
  ComputeSparseSoftmaxCrossEntropyResultGpu<half, K>
      <<<BlocksNum4ThreadsNum(num_instances), kCudaThreadsNumPerBlock, 0, ctx->cuda_stream()>>>(
          num_instances, num_classes, depth, lower_bound, labels,
          reinterpret_cast<const half*>(prob), reinterpret_cast<half*>(out));
#else
  printf("use half need nvcc arch >= 530");
  assert(false);
#endif /* __CUDA_ARCH__ >= 530 || !defined(__CUDA_ARCH__)*/
}
}  // namespace

template<typename T, typename K>
class SparseSoftmaxCrossEntropyKernel final : public user_op::OpKernel,
                                              public user_op::CudaGraphSupport {
 public:
  SparseSoftmaxCrossEntropyKernel() = default;
  ~SparseSoftmaxCrossEntropyKernel() override = default;

 private:
  using user_op::OpKernel::Compute;
  void Compute(user_op::KernelComputeContext* ctx) const override {
    const user_op::Tensor* prediction = ctx->Tensor4ArgNameAndIndex("prediction", 0);
    const user_op::Tensor* label = ctx->Tensor4ArgNameAndIndex("label", 0);
    user_op::Tensor* prob = ctx->Tensor4ArgNameAndIndex("prob", 0);
    user_op::Tensor* out = ctx->Tensor4ArgNameAndIndex("out", 0);

    const int64_t num_instances = label->shape().elem_cnt();
    CHECK_EQ(prediction->shape().elem_cnt() % num_instances, 0);
    const int64_t num_classes = prediction->shape().elem_cnt() / num_instances;
    const int64_t lower_bound = 0;
    const int64_t depth = ctx->Attr<int64_t>("depth");

    ComputeProb<T>(ctx->device_ctx(), num_instances, num_classes, prediction->dptr<T>(),
                   prob->mut_dptr<T>());
    ComputeSparseSoftmaxCrossEntropyResult<T, K>(ctx->device_ctx(), num_instances, num_classes,
                                                 depth, lower_bound, label->dptr<K>(),
                                                 prob->dptr<T>(), out->mut_dptr<T>());
  }
  bool AlwaysComputeWhenAllOutputsEmpty() const override { return false; }
};

#define REGISTER_SPARSE_SOFTMAX_CROSS_ENTROPY_KERNEL(dtype_pair, ltype_pair)                 \
  REGISTER_USER_KERNEL("sparse_softmax_cross_entropy")                                       \
      .SetCreateFn<SparseSoftmaxCrossEntropyKernel<OF_PP_PAIR_FIRST(dtype_pair),             \
                                                   OF_PP_PAIR_FIRST(ltype_pair)>>()          \
      .SetIsMatchedHob((user_op::HobDeviceTag() == DeviceType::kGPU)                         \
                       & (user_op::HobDataType("label", 0) == OF_PP_PAIR_SECOND(ltype_pair)) \
                       & (user_op::HobDataType("out", 0) == OF_PP_PAIR_SECOND(dtype_pair)));

OF_PP_SEQ_PRODUCT_FOR_EACH_TUPLE(REGISTER_SPARSE_SOFTMAX_CROSS_ENTROPY_KERNEL,
                                 FLOATING_DATA_TYPE_SEQ FLOAT16_DATA_TYPE_SEQ, INDEX_DATA_TYPE_SEQ)

}  // namespace user_op
}  // namespace oneflow
