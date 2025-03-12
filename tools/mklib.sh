#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025 Debojeet Das

outdir=$1
outfile=$2
shift 2

echo "GROUP (" "$*" ")" > "$outdir/$outfile"
