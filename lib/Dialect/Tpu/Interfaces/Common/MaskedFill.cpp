//===----------------------------------------------------------------------===//
//
// Copyright (C) 2022 Sophgo Technologies Inc.  All rights reserved.
//
// TPU-MLIR is licensed under the 2-Clause BSD License except for the
// third-party components.
//
//===----------------------------------------------------------------------===//

#include "tpu_mlir/Dialect/Tpu/IR/TpuOps.h"
#include "tpu_mlir/Support/Dnnl/Dnnl.h"
#include "tpu_mlir/Support/Helper/Quant.h"
#include "tpu_mlir/Support/Helper/Module.h"
#include "tpu_mlir/Support/MathUtils.h"
#include "tpu_mlir/Support/Float16.h"

using namespace tpu_mlir;
using namespace tpu_mlir::helper;
using namespace mlir;

LogicalResult tpu::MaskedFillOp::init(InferenceParameter &p) { return success(); }
void tpu::MaskedFillOp::deinit(InferenceParameter &p) {}

LogicalResult tpu::MaskedFillOp::inference(InferenceParameter &p) {
  const auto num_element = Module::getNumElements(output());
  const float const_val_ = const_val().convertToDouble();
  const float* in = p.inputs[0];
  const float* brn = p.inputs[1];
#pragma omp parallel for schedule(static, omp_schedule(num_element))
  for (int i = 0; i < num_element; ++i) {
    const float tbrn = inversed() ? const_val_ : brn[i];
    const float fbrn = inversed() ? brn[i] : const_val_;
    p.outputs[0][i] = in[i] ? tbrn : fbrn;
  }
  return success();
}
