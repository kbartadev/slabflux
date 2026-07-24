#!/bin/bash -eu
# OSS-Fuzz / ClusterFuzzLite build script for slabflux.
# Usage (from repo root): fuzz/oss-fuzz/build.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

export CC="${CC:-clang}"
export CXX="${CXX:-clang++}"

BUILD_DIR="${ROOT_DIR}/build-oss-fuzz"
OUT_DIR="${OUT:-${BUILD_DIR}/out}"
mkdir -p "${OUT_DIR}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DSLABFLUX_ENABLE_FUZZ=ON \
  -DCMAKE_CXX_COMPILER="${CXX}" \
  -DCMAKE_C_COMPILER="${CC}"

cmake --build "${BUILD_DIR}" --target http_parser_fuzz http_avx_fuzz -j"$(nproc)"

install -m 0755 "${BUILD_DIR}/tests/fuzz/http_parser_fuzz" "${OUT_DIR}/http_parser_fuzz"
install -m 0755 "${BUILD_DIR}/tests/fuzz/http_avx_fuzz" "${OUT_DIR}/http_avx_fuzz"
