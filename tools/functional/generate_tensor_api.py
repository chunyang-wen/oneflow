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
import re
import argparse
import yaml

from generator import Generator

parser = argparse.ArgumentParser()
parser.add_argument(
    "--project_source_dir", type=str, help="The project source code directory.",
)
args = parser.parse_args()

license = """/*
Copyright 2020 The OneFlow Authors. All rights reserved.

Licensed under the Apache License, Version 2.0 (the \"License\");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an \"AS IS\" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

// Generated from oneflow/api/python/functional/tensor_api.yaml. DO NOT EDIT!"""

header_fmt = (
    license
    + """

#ifndef ONEFLOW_API_PYTHON_FUNCTIONAL_GENERATED_TENSOR_API_H_
#define ONEFLOW_API_PYTHON_FUNCTIONAL_GENERATED_TENSOR_API_H_

#include <Python.h>

#include "oneflow/core/common/optional.h"
#include "oneflow/core/common/scalar.h"
#include "oneflow/core/framework/dtype.h"
#include "oneflow/core/framework/tensor.h"
#include "oneflow/core/framework/tensor_tuple.h"
#include "oneflow/core/framework/random_generator.h"
#include "oneflow/core/functional/tensor_index.h"

namespace oneflow {{
namespace one {{
namespace functional {{
{0}
}}  // namespace functional
}}  // namespace one
}}  // namespace oneflow

#endif  // ONEFLOW_API_PYTHON_FUNCTIONAL_GENERATED_TENSOR_API_H_"""
)

source_fmt = (
    license
    + """

#include "oneflow/api/python/functional/tensor_api.yaml.h"
#include "oneflow/core/functional/function_library.h"

namespace oneflow {{
namespace one {{
namespace functional {{
{0}
}}  // namespace functional
}}  // namespace one
}}  // namespace oneflow
"""
)

pybind_header_fmt = (
    license
    + """

#include <pybind11/pybind11.h>

namespace py = pybind11;

namespace oneflow {{
namespace one {{
namespace functional {{
{0}
}}  // namespace functional
}}  // namespace one
}}  // namespace oneflow
"""
)

pybind_source_fmt = (
    license
    + """

#include <vector>
#include <pybind11/pybind11.h>

#include "oneflow/api/python/of_api_registry.h"
#include "oneflow/api/python/functional/function_def.h"
#include "oneflow/api/python/functional/py_function.h"
#include "oneflow/api/python/functional/tensor_api.yaml.h"
#include "oneflow/api/python/functional/tensor_api.yaml.pybind.h"
#include "oneflow/core/common/maybe.h"
#include "oneflow/core/common/optional.h"

namespace py = pybind11;

namespace oneflow {{
namespace one {{
namespace functional {{
{0}
}}  // namespace functional
}}  // namespace one

namespace functional = one::functional;

ONEFLOW_API_PYBIND11_MODULE("_C", m) {{
  py::options options;
  options.disable_function_signatures();

{1}
  options.enable_function_signatures();
}}

}}  // namespace oneflow
"""
)

yaml_file_path = os.path.join(
    args.project_source_dir, "oneflow/api/python/functional/tensor_api.yaml"
)
generated_api_dir = "oneflow/api/python/functional"
generated_pybind_dir = "oneflow/api/python/functional"

if __name__ == "__main__":
    assert os.path.isfile(yaml_file_path), (
        "It is not a regular file for the yaml file which is " + yaml_file_path
    )
    g = Generator(yaml_file_path)

    assert os.path.isdir(generated_api_dir), (
        "Could not locate the api generate directory which is " + generated_api_dir
    )
    target_header_file = os.path.join(generated_api_dir, "tensor_api.yaml.h")
    g.generate_cpp_header_file(header_fmt, target_header_file)
    target_source_file = os.path.join(generated_api_dir, "tensor_api.yaml.cpp")
    g.generate_cpp_source_file(source_fmt, target_source_file)

    assert os.path.isdir(generated_pybind_dir), (
        "Could not locate the pybind generate directory which is "
        + generated_pybind_dir
    )
    target_pybind_header_file = os.path.join(
        generated_pybind_dir, "tensor_api.yaml.pybind.h"
    )
    target_pybind_source_file = os.path.join(
        generated_pybind_dir, "tensor_api.yaml.pybind.cpp"
    )
    g.generate_pybind_for_python(
        pybind_header_fmt,
        pybind_source_fmt,
        target_pybind_header_file,
        target_pybind_source_file,
    )
