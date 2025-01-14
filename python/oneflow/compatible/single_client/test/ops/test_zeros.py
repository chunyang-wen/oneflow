"""
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
"""

import os
import unittest
from collections import OrderedDict
from typing import Dict

import numpy as np
from test_util import GenArgList

import oneflow.compatible.single_client.unittest
from oneflow.compatible import single_client as flow
from oneflow.compatible.single_client import typing as tp


def _compare_zeros_with_np(input_shape, device_type, machine_ids, device_counts):
    assert device_type in ["cpu", "gpu"]
    flow.clear_default_session()
    if device_type == "cpu":
        flow.config.cpu_device_num(device_counts)
    else:
        flow.config.gpu_device_num(device_counts)
    func_config = flow.FunctionConfig()
    func_config.default_placement_scope(flow.scope.placement(device_type, machine_ids))
    np_out_zeros = np.zeros(shape=input_shape, dtype=np.float32)

    @flow.global_function(type="train", function_config=func_config)
    def oneflow_zeros() -> tp.Numpy:
        with flow.scope.placement(device_type, "0:0"):
            v = flow.get_variable(
                shape=np_out_zeros.shape,
                dtype=flow.float32,
                initializer=flow.zeros_initializer(),
                name="x_var",
            )
        of_zeros = flow.zeros(shape=input_shape, dtype=flow.float32)
        of_out = of_zeros + v
        with flow.scope.placement(device_type, "0:0"):
            flow.optimizer.SGD(
                flow.optimizer.PiecewiseConstantScheduler([], [0.001]), momentum=0
            ).minimize(of_out)
        return of_zeros

    of_out_zeros = oneflow_zeros()
    assert np.allclose(of_out_zeros, np_out_zeros)


def _gen_arg_dict(shape, device_type, machine_ids, device_counts):
    arg_dict = OrderedDict()
    arg_dict["input_shape"] = [shape]
    arg_dict["device_type"] = [device_type]
    arg_dict["machine_ids"] = [machine_ids]
    arg_dict["device_counts"] = [device_counts]
    return arg_dict


@flow.unittest.skip_unless_1n1d()
class Testzeros1n1d(flow.unittest.TestCase):
    def test_zeros_cpu(test_case):
        arg_dict = _gen_arg_dict(
            shape=(3, 3), device_type="cpu", machine_ids="0:0", device_counts=1
        )
        for arg in GenArgList(arg_dict):
            _compare_zeros_with_np(*arg)

    @unittest.skipIf(os.getenv("ONEFLOW_TEST_CPU_ONLY"), "only test cpu cases")
    def test_zeros_gpu(test_case):
        arg_dict = _gen_arg_dict(
            shape=(3, 16, 32), device_type="gpu", machine_ids="0:0", device_counts=1
        )
        for arg in GenArgList(arg_dict):
            _compare_zeros_with_np(*arg)


@flow.unittest.skip_unless_1n2d()
class Testzeros1n2d(flow.unittest.TestCase):
    @unittest.skipIf(os.getenv("ONEFLOW_TEST_CPU_ONLY"), "only test cpu cases")
    def test_zeros_gpu_1n2d(test_case):
        arg_dict = _gen_arg_dict(
            shape=(3, 8, 8, 4), device_type="gpu", machine_ids="0:0-1", device_counts=2
        )
        for arg in GenArgList(arg_dict):
            _compare_zeros_with_np(*arg)


if __name__ == "__main__":
    unittest.main()
