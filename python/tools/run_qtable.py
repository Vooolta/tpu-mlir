#!/usr/bin/env python3
# ==============================================================================
#
# Copyright (C) 2022 Sophgo Technologies Inc.  All rights reserved.
#
# TPU-MLIR is licensed under the 2-Clause BSD License except for the
# third-party components.
#
# ==============================================================================

import argparse
import pymlir
import numpy as np
from pathlib import Path
from calibration.mix_precision import MixPrecSearcher

if __name__ == '__main__':
    print("SOPHGO Toolchain {}".format(pymlir.module().version))
    # yapf: disable
    parser = argparse.ArgumentParser(description="Generate quantization table")
    parser.add_argument('mlir_file', help='fp32 mlir file')
    parser.add_argument('--dataset', help='dataset path for mix precision searching')
    parser.add_argument("--data_list", help="specify a file with inputs's absolute path for mix precision searching")
    parser.add_argument('--input_num', type=int, default=10,
                        help='num of inputs for quantization searching')
    parser.add_argument('--expected_cos', type=float, default=0.99,
                        help='expected net output cos')
    parser.add_argument('--global_compare_layers', type=str, default='',
                        help='global compare layers, for example:\'layer1,layer2\' or \'layer1:0.3,layer2:0.7\'')
    parser.add_argument('--min_layer_cos', type=float, default=0.99,
                        help='minimum cos of layer')
    parser.add_argument('--calibration_table', required=True,
                        help='calibration table generated by calibration or tune tool')
    parser.add_argument('--chip', required=True, type=str,
                        choices=['bm1684x', 'bm1684', 'cv183x', 'cv182x', 'cv181x', 'cv180x'],
                        help='chip platform name')
    parser.add_argument('--fp_type', default='auto', type=str,
                        choices=['auto', 'F16', 'F32', 'BF16'],
                        help='float type of mix precision')
    parser.add_argument('--loss_table', default='full_loss_table.txt',
                        help="output all loss of layers if each layer is quantized to f16")
    parser.add_argument('-o', '--quantize_table', required=True,
                        help='output searched bf16 layer table')
    parser.add_argument('--debug_cmd', type=str, default='', help='debug cmd')

    # yapf: enable
    args = parser.parse_args()
    searcher = MixPrecSearcher(args)
    if 'run_bias_correction' in args.debug_cmd:
        searcher.run_bias_correction()
    else:
        searcher.run()
