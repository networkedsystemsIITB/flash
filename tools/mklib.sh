#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025 Debojeet Das

outdir=$1
outfile=$2
shift 2

echo "GROUP (" "$*" ")" > "$outdir/$outfile"
