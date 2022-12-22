//===----------------------------------------------------------------------===//
//
// Copyright (c) 2020-2030 by Sophgo Technologies Inc. All rights reserved.
//
// Licensed under the Apache License v2.0.
// See http://www.apache.org/licenses/LICENSE-2.0 for license information.
// SPDX-License-Identifier: Apache-2.0
//
//===----------------------------------------------------------------------===//

#include "tpu_mlir/Conversion/TopToTpu/LoweringCV18xx.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "lowering-conv"

namespace tpu_mlir {
namespace cv18xx {

static bool ConvertPading(PatternRewriter &rewriter, top::ConvOp op,
                          conv_attr_t &attr) {
  // deal with pad > 16
  bool insert_pad = false;
  auto kernel_size = Module::getI64Array(op.kernel_shape())->size();
  std::vector<int64_t> input_shape;
  Module::getShapeVec(op.input(), input_shape);
  auto _pads = Module::getI64Array(op.pads());
  std::vector<int64_t> pad_v;
  std::vector<int64_t> new_pad_v(2 * input_shape.size(), 0);
  pad_v.assign(_pads->begin(), _pads->end());
  for (auto p : pad_v) {
    if (p > 15) {
      insert_pad = true;
      break;
    }
  }
  if (insert_pad && kernel_size > 3) {
    // todo pad 3d (padOp not support now)
    llvm_unreachable("Not supported now");
  }
  if (insert_pad) {
    if (kernel_size == 2) {
      if (attr.pht > 15) {
        assert(attr.pht == pad_v[0]);
        pad_v[0] = 0;
        new_pad_v[2] = attr.pht;
        input_shape[2] += attr.pht;
      }
      if (attr.pwl > 15) {
        assert(attr.pwl == pad_v[1]);
        pad_v[1] = 0;
        new_pad_v[3] = attr.pwl;
        input_shape[3] += attr.pwl;
      }
      if (attr.phb > 15) {
        assert(attr.phb == pad_v[2]);
        pad_v[2] = 0;
        new_pad_v[6] = attr.phb;
        input_shape[2] += attr.phb;
      }
      if (attr.pwr > 15) {
        assert(attr.pwr == pad_v[3]);
        pad_v[3] = 0;
        new_pad_v[7] = attr.pwr;
        input_shape[3] += attr.pwr;
      }
    }
    if (kernel_size == 1) {
      if (attr.pht > 15) {
        assert(attr.pht == pad_v[0]);
        pad_v[0] = 0;
        new_pad_v[2] = attr.pht;
        input_shape[2] += attr.pht;
      }
      if (attr.phb > 15) {
        assert(attr.phb == pad_v[1]);
        pad_v[1] = 0;
        new_pad_v[5] = attr.phb;
        input_shape[2] += attr.phb;
      }
    }
    std::vector<NamedAttribute> attrs;
    attrs.push_back(rewriter.getNamedAttr(
        "paddings", rewriter.getI64ArrayAttr(ArrayRef<int64_t>{new_pad_v})));
    auto op_name = Module::getName(op.getOperation()).str();
    auto loc = NameLoc::get(rewriter.getStringAttr(op_name + "_pad"));
    auto type = op.input().getType().cast<RankedTensorType>();
    auto new_type = RankedTensorType::get(input_shape, type.getElementType());
    auto padOp = rewriter.create<top::PadOp>(loc, new_type,
                                             ValueRange{
                                                 op.input(),
                                             },
                                             attrs);
    op->setAttr("pads", rewriter.getI64ArrayAttr(pad_v));
    op->setOperand(0, padOp);
    auto convOp = rewriter.create<top::ConvOp>(
        op->getLoc(), op->getResultTypes(), op->getOperands(), op->getAttrs());
    rewriter.replaceOp(op, {convOp.output()});
  }
  return insert_pad;
}
void ConvLowering::LoweringINT8(PatternRewriter &rewriter, top::ConvOp op,
                                bool asymmetric) const {
  rewriter.setInsertionPointAfter(op);
  std::vector<Value> operands;
  operands.push_back(op.input());
  conv_attr_t attr = {0};
  op.parseParam(&attr);
  if (ConvertPading(rewriter, op, attr)) {
    return;
  }
  double in_thr, out_thr;
  in_thr = Quant::getThreshold(op.input());
  out_thr = Quant::getThreshold(op.output());
  // filter
  auto filterOp = cast<top::WeightOp>(op.filter().getDefiningOp());
  auto filter_f32 = filterOp.read<float>();

  std::shared_ptr<std::vector<int32_t>> bias_int32;
  std::shared_ptr<std::vector<float>> bias_fp32;
  auto filter_i8 = std::make_shared<std::vector<int8_t>>(filter_f32->size());
  if (attr.has_bias) {
    auto biasOp = cast<top::WeightOp>(op.bias().getDefiningOp());
    bias_fp32 = biasOp.read<float>();
    bias_int32 = std::make_shared<std::vector<int32_t>>(bias_fp32->size());
  }
  std::vector<int64_t> rshift_v;
  std::vector<int64_t> multiplier_v;
  int64_t multiplier, rshift;
  int inner_dim = filter_f32->size() / attr.oc;
  // per-channel
  for (int c = 0; c < attr.oc; c++) { // per-channel quantize
    float *p_filter = filter_f32->data() + c * inner_dim;
    float w_max = findMaxabs(p_filter, inner_dim);
    double qscale = getQscaleForFilter(w_max, out_thr, in_thr);
    if (qscale >= 1) {
      // Now cv18xx not support lshift, if qscale > 1, rshift <= 0 not working
      // now we fix threshold_w to limit value qscale = (thr_w * thr_x) / (127.0
      // * thr_y) thr_w = qscale * 127.0 * thr_y / thr_x qscale = 0.99999999
      qscale = 0.999999;
      LLVM_DEBUG(llvm::errs()
                     << "WARNING: adjust threshold_w for qscale"
                     << ", qscale_filter = " << qscale << ", max_filter[" << c
                     << "] = " << qscale * 127 * out_thr / in_thr << "\n";);
    }
    if (attr.has_bias) {
      float b_max = fabs(bias_fp32->data()[c]);
      double qscale_bias = getQscaleForBias(b_max, out_thr);
      if (qscale_bias > qscale) {
        LLVM_DEBUG(llvm::errs() << "WARNING: adjust qscale for bias"
                                << ", qscale_filter = " << qscale
                                << ", qscale_bias = " << qscale_bias << "\n";);
        if (qscale_bias >= 1) {
          // prevent for auto tuning
          LLVM_DEBUG(llvm::errs()
                         << "WARNING:  qscale_bias are valid, keep org qscale"
                         << ", qscale_filter = " << qscale
                         << ", qscale_bias = " << qscale_bias << "\n";);
        } else {
          qscale = qscale_bias;
        }
      }
    }
    // decompose qscale into rshift and muliplier
    // QuantizeMultiplier(qscale, &multiplier, &rshift, false);
    getRShiftAndMultiplierFromQScale(qscale, &multiplier, &rshift, true);
    multiplier_v.push_back(multiplier);
    rshift_v.push_back(rshift);
    // quantize weight
    quantizeFilterRShiftAndMultiplier(
        p_filter, filter_i8->data() + c * inner_dim, inner_dim, out_thr, in_thr,
        rshift, multiplier, true);
    if (attr.has_bias) {
      quantizeBiasRShiftAndMultiplier(bias_fp32->data() + c,
                                      bias_int32->data() + c, 1, out_thr,
                                      rshift, multiplier, true);
    }
  }
  auto filter_type = op.filter().getType().cast<RankedTensorType>();
  auto new_type = RankedTensorType::get(filter_type.getShape(),
                                        rewriter.getIntegerType(8, true));
  auto new_filter =
      top::WeightOp::create(op, "filter_i8", *filter_i8, new_type);
  operands.push_back(new_filter);

  if (attr.has_bias) {
    auto new_type =
        RankedTensorType::get({1, attr.oc, 1, 1}, rewriter.getI32Type());
    auto new_bias =
        top::WeightOp::create(op, "bias_int32", *bias_int32, new_type);
    operands.push_back(new_bias);
  } else {
    operands.push_back(op.bias()); // none
  }
  std::vector<NamedAttribute> attrs;
  for (auto &attr : op->getAttrs()) {
    attrs.push_back(attr);
  }
  auto ctx = op->getContext();
  attrs.push_back(rewriter.getNamedAttr(
      "quant_mode", tpu::RequantModeAttr::get(ctx, tpu::RequantMode::Normal)));
  attrs.push_back(rewriter.getNamedAttr(
      "rshift", rewriter.getI64ArrayAttr(ArrayRef<int64_t>{rshift_v})));
  attrs.push_back(rewriter.getNamedAttr(
      "multiplier", rewriter.getI64ArrayAttr(ArrayRef<int64_t>{multiplier_v})));
  attrs.push_back(
      rewriter.getNamedAttr("with_bias", rewriter.getBoolAttr(attr.has_bias)));
  auto newType = getQuantInt8Type(op.output(), asymmetric);
  auto newOp = rewriter.create<tpu::Conv2DOp>(op->getLoc(), newType,
                                              ArrayRef<Value>{operands},
                                              ArrayRef<NamedAttribute>{attrs});
  Value newValue = newOp.output();
  rewriter.replaceOp(op, {newValue});
}

void ConvLowering::LoweringBF16(PatternRewriter &rewriter,
                                top::ConvOp op) const {
  rewriter.setInsertionPointAfter(op);
  std::vector<Value> operands;
  conv_attr_t attr = {0};
  op.parseParam(&attr);
  if (ConvertPading(rewriter, op, attr)) {
    return;
  }
  auto filterOp = cast<top::WeightOp>(op.filter().getDefiningOp());
  operands.push_back(op.input());
  operands.push_back(filterOp.clone_bf16(op));
  operands.push_back(op.bias());

  std::vector<NamedAttribute> attrs;
  for (auto &attr : op->getAttrs()) {
    attrs.push_back(attr);
  }
  bool with_bias = !op.bias().getType().isa<mlir::NoneType>();
  attrs.push_back(
      rewriter.getNamedAttr("with_bias", rewriter.getBoolAttr(with_bias)));
  auto newType = getQuantBF16Type(op.output());
  Value newValue;
  if (op.kernel_shape().size() == 1) {
    auto newOp =
        rewriter.create<tpu::Conv1DOp>(op->getLoc(), newType, operands, attrs);
    newValue = newOp.output();
  } else if (op.kernel_shape().size() == 2) {
    auto newOp =
        rewriter.create<tpu::Conv2DOp>(op->getLoc(), newType, operands, attrs);
    newValue = newOp.output();
  } else {
    auto newOp =
        rewriter.create<tpu::Conv3DOp>(op->getLoc(), newType, operands, attrs);
    newValue = newOp.output();
  }
  rewriter.replaceOp(op, {newValue});
}

} // namespace cv18xx
} // namespace tpu_mlir
