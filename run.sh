#!/bin/bash
# run.sh – Build and launch the Helios clickstream pipeline
#
# Usage: ./run.sh [input_dir] [output_dir] [num_threads] [queue_size]
# Defaults: input=./input  output=./output  threads=4  queue=8

set -e

INPUT_DIR=${1:-./input}
OUTPUT_DIR=${2:-./output}
NUM_THREADS=${3:-4}
QUEUE_SIZE=${4:-8}

echo "[RUN] Building all components..."
make all

echo ""
echo "[RUN] Starting dispatcher..."
echo "[RUN]   input_dir  = $INPUT_DIR"
echo "[RUN]   output_dir = $OUTPUT_DIR"
echo "[RUN]   threads    = $NUM_THREADS"
echo "[RUN]   queue_size = $QUEUE_SIZE"
echo ""

./build/dispatcher "$INPUT_DIR" "$OUTPUT_DIR" "$NUM_THREADS" "$QUEUE_SIZE"

echo ""
echo "[RUN] Pipeline finished. Check '$OUTPUT_DIR/report.csv' for results."
echo "[RUN] Per-process logs are in the 'logs/' directory."
