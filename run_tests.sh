#!/bin/bash
# run_tests.sh
cd /mnt/c/Workspace/quartus/video_processing
export PYTHONPATH=$PYTHONPATH:$(pwd)/tests/cocotb
python3 -m pytest -s tests/ > test_summary.log 2>&1
