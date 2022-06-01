#!/usr/bin/env python3
# ==============================================================================
#
# Copyright (c) 2020-2030 by Sophgo Technologies Inc. All rights reserved.
#
# Licensed under the Apache License v2.0.
# See http://www.apache.org/licenses/LICENSE-2.0 for license information.
# SPDX-License-Identifier: Apache-2.0
#
# ==============================================================================

from __future__ import division
import numpy as np
import os
import sys
import argparse
import plotly.graph_objects as go
from plotly.subplots import make_subplots

def parse_args(args_list):
    parser = argparse.ArgumentParser(description='visualize_diff two npz tensor files.')
    parser.add_argument("target_file",
                        help="Comparing target file")
    parser.add_argument("ref_file",
                        help="Comparing reference file")
    parser.add_argument('--verbose', '-v', action='count', default=0)
    parser.add_argument("--excepts", type=str,
                        help="List of tensors except from comparing")
    parser.add_argument("--max_sampling", type=int, default=10000,
                        help="max sampling points")
    args = parser.parse_args(args_list)
    return args

def cos_sim(vector_a, vector_b):
    vector_a = np.mat(vector_a)
    vector_b = np.mat(vector_b)
    num = float(vector_a * vector_b.T)
    denom = np.linalg.norm(vector_a) * np.linalg.norm(vector_b)
    with np.errstate(invalid='ignore'):
        cos = np.nan_to_num(num / denom)
    sim = 0.5 + 0.5 * cos
    return sim

def npz_visualize_diff(args_list):
  os.system('mkdir -p tensor_diff_fp32_vs_int8/')
  args = parse_args(args_list)
  f1 = args.target_file
  f2 = args.ref_file

  np.set_printoptions(precision=6)
  np.set_printoptions(suppress=True)
  excepts = []
  if args.excepts:
    excepts = [str(s) for s in args.excepts.split(',')]

  npz1 = np.load(f1)
  npz2 = np.load(f2)
  print('npz1.files:', npz1.files)
  print('npz2.files:', npz2.files)
  common = list()
  for name in npz1.files:
    if name in npz2.files and name not in excepts:
       common.append(name)

  for i, name in enumerate(common):
    blob_int = npz1[name]
    blob_fp = npz2[name]

    if blob_fp.shape != blob_int.shape:
        minsize= min(blob_fp.size,blob_int.size)
        blob_fp=blob_fp.flatten()[:minsize]
        blob_int=blob_int.flatten()[:minsize]

    blob_COS = cos_sim(blob_fp.reshape(-1), blob_int.reshape(-1))
    data_size = blob_fp.size
    if data_size > args.max_sampling:
        step = data_size//args.max_sampling
    else:
        step = 1

    index = np.arange(data_size)
    index = index[::step]

    rows = 2
    fig = make_subplots(
        rows=rows,
        cols=1,
        shared_xaxes=True,
        vertical_spacing=0.1,
        subplot_titles=("ref vs target",
                    "ref - target   COS:{}".format(blob_COS)))
    fig.add_trace(go.Scattergl(y=blob_fp.reshape([-1])[::step],
                                x=index,
                                name='ref',
                                mode='lines+markers',
                                marker={
                                    "size": 6,
                                    "symbol": 300
                                },
                                line={"width": 1}),
                    row=1,
                    col=1)
    fig.add_trace(go.Scattergl(y=blob_int.reshape([-1])[::step],
                                x=index,
                                name='target',
                                mode='lines+markers',
                                marker={
                                    "size": 6,
                                    "symbol": 304,
                                    "opacity": 0.8
                                },
                                line={"width": 1}),
                    row=1,
                    col=1)

    fig.add_trace(go.Scattergl(y=(blob_fp - blob_int).reshape([-1])[::step],
                                x=index,
                                name='diff',
                                line={"width": 1}),
                    row=2,
                    col=1)
    fig.update_layout(height=400*rows, width=900)
    fig.write_html('./tensor_diff_fp32_vs_int8/{:0>4d}_{}'.format(i, name.replace('/','_'))+'.html')
    fig.write_image('./tensor_diff_fp32_vs_int8/{:0>4d}_{}'.format(i, name.replace('/','_'))+'.png')

if __name__ == '__main__':
    npz_visualize_diff(sys.argv)
