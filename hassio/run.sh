#!/bin/bash
set -e

echo "[Info] Starting gateway"

xcomfortd/xcomfortd -v -h 172.30.32.1 -u username -P password

