#!/usr/bin/env bash
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

set -ex

source_dir=${1}/cpp
build_dir=${2}/cpp
with_docs=${3:-false}

: ${ARROW_USE_CCACHE:=OFF}

# TODO(kszucs): consider to move these to CMake
if [ ! -z "${CONDA_PREFIX}" ]; then
  export CMAKE_ARGS="${CMAKE_ARGS} -DCMAKE_AR=${AR} -DCMAKE_RANLIB=${RANLIB}"
  export ARROW_GANDIVA_PC_CXX_FLAGS=$(echo | ${CXX} -E -Wp,-v -xc++ - 2>&1 | grep '^ ' | awk '{print "-isystem;" substr($1, 1)}' | tr '\n' ';')
elif [ -x "$(command -v xcrun)" ]; then
  export ARROW_GANDIVA_PC_CXX_FLAGS="-isysroot;$(xcrun --show-sdk-path)"
fi

if [ "${ARROW_USE_CCACHE}" == "ON" ]; then
    echo -e "===\n=== ccache statistics before build\n==="
    ccache -s
fi

mkdir -p ${build_dir}
pushd ${build_dir}

cmake -G "${CMAKE_GENERATOR:-Ninja}" \
      -DARROW_BOOST_USE_SHARED=${ARROW_BOOST_USE_SHARED:-ON} \
      -DARROW_BUILD_BENCHMARKS=${ARROW_BUILD_BENCHMARKS:-OFF} \
      -DARROW_BUILD_EXAMPLES=${ARROW_BUILD_EXAMPLES:-OFF} \
      -DARROW_BUILD_INTEGRATION=${ARROW_BUILD_INTEGRATION:-OFF} \
      -DARROW_BUILD_SHARED=${ARROW_BUILD_SHARED:-ON} \
      -DARROW_BUILD_STATIC=${ARROW_BUILD_STATIC:-ON} \
      -DARROW_BUILD_TESTS=${ARROW_BUILD_TESTS:-OFF} \
      -DARROW_BUILD_UTILITIES=${ARROW_BUILD_UTILITIES:-ON} \
      -DARROW_COMPUTE=${ARROW_COMPUTE:-ON} \
      -DARROW_CSV=${ARROW_CSV:-ON} \
      -DARROW_CUDA=${ARROW_CUDA:-OFF} \
      -DARROW_CXXFLAGS=${ARROW_CXXFLAGS:-} \
      -DARROW_DATASET=${ARROW_DATASET:-ON} \
      -DARROW_DEPENDENCY_SOURCE=${ARROW_DEPENDENCY_SOURCE:-AUTO} \
      -DARROW_EXTRA_ERROR_CONTEXT=${ARROW_EXTRA_ERROR_CONTEXT:-OFF} \
      -DARROW_FILESYSTEM=${ARROW_FILESYSTEM:-ON} \
      -DARROW_FLIGHT=${ARROW_FLIGHT:-OFF} \
      -DARROW_FUZZING=${ARROW_FUZZING:-OFF} \
      -DARROW_FUZZING=${ARROW_FUZZING:-OFF} \
      -DARROW_GANDIVA_JAVA=${ARROW_GANDIVA_JAVA:-OFF} \
      -DARROW_GANDIVA_PC_CXX_FLAGS=${ARROW_GANDIVA_PC_CXX_FLAGS:-} \
      -DARROW_GANDIVA=${ARROW_GANDIVA:-OFF} \
      -DARROW_HDFS=${ARROW_HDFS:-ON} \
      -DARROW_HIVESERVER2=${ARROW_HIVESERVER2:-OFF} \
      -DARROW_INSTALL_NAME_RPATH=${ARROW_INSTALL_NAME_RPATH:-ON} \
      -DARROW_JEMALLOC=${ARROW_JEMALLOC:-ON} \
      -DARROW_JNI=${ARROW_JNI:-OFF} \
      -DARROW_JSON=${ARROW_JSON:-ON} \
      -DARROW_LARGE_MEMORY_TESTS=${ARROW_LARGE_MEMORY_TESTS:-OFF} \
      -DARROW_MIMALLOC=${ARROW_MIMALLOC:-OFF} \
      -DARROW_NO_DEPRECATED_API=${ARROW_NO_DEPRECATED_API:-OFF} \
      -DARROW_ORC=${ARROW_ORC:-OFF} \
      -DARROW_PARQUET=${ARROW_PARQUET:-OFF} \
      -DARROW_PLASMA_JAVA_CLIENT=${ARROW_PLASMA_JAVA_CLIENT:-OFF} \
      -DARROW_PLASMA=${ARROW_PLASMA:-OFF} \
      -DARROW_PYTHON=${ARROW_PYTHON:-OFF} \
      -DARROW_S3=${ARROW_S3:-OFF} \
      -DARROW_TEST_LINKAGE=${ARROW_TEST_LINKAGE:-shared} \
      -DARROW_USE_ASAN=${ARROW_USE_ASAN:-OFF} \
      -DARROW_USE_CCACHE=${ARROW_USE_CCACHE:-ON} \
      -DARROW_USE_GLOG=${ARROW_USE_GLOG:-ON} \
      -DARROW_USE_STATIC_CRT=${ARROW_USE_STATIC_CRT:-OFF} \
      -DARROW_USE_UBSAN=${ARROW_USE_UBSAN:-OFF} \
      -DARROW_VERBOSE_THIRDPARTY_BUILD=${ARROW_VERBOSE_THIRDPARTY_BUILD:-OFF} \
      -DARROW_WITH_BROTLI=${ARROW_WITH_BROTLI:-OFF} \
      -DARROW_WITH_BZ2=${ARROW_WITH_BZ2:-OFF} \
      -DARROW_WITH_LZ4=${ARROW_WITH_LZ4:-OFF} \
      -DARROW_WITH_SNAPPY=${ARROW_WITH_SNAPPY:-OFF} \
      -DARROW_WITH_ZLIB=${ARROW_WITH_ZLIB:-OFF} \
      -DARROW_WITH_ZSTD=${ARROW_WITH_ZSTD:-OFF} \
      -Dbenchmark_SOURCE=${benchmark_SOURCE:-AUTO} \
      -DBOOST_SOURCE=${BOOST_SOURCE:-AUTO} \
      -DBrotli_SOURCE=${Brotli_SOURCE:-AUTO} \
      -DBUILD_WARNING_LEVEL=${DARROW_BUILD_WARNING_LEVEL:-CHECKIN} \
      -Dc-ares_SOURCE=${cares_SOURCE:-AUTO} \
      -DCMAKE_BUILD_TYPE=${ARROW_BUILD_TYPE:-debug} \
      -DCMAKE_CXX_FLAGS=${CXXFLAGS:-} \
      -DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR:-lib} \
      -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX:-${ARROW_HOME}} \
      -Dgflags_SOURCE=${gflags_SOURCE:-AUTO} \
      -DgRPC_SOURCE=${gRPC_SOURCE:-AUTO} \
      -DGTest_SOURCE=${GTest_SOURCE:-AUTO} \
      -DLz4_SOURCE=${Lz4_SOURCE:-AUTO} \
      -DORC_SOURCE=${ORC_SOURCE:-AUTO} \
      -DPARQUET_BUILD_EXECUTABLES=${PARQUET_BUILD_EXECUTABLES:-OFF} \
      -DPARQUET_BUILD_EXAMPLES=${PARQUET_BUILD_EXAMPLES:-OFF} \
      -DPARQUET_REQUIRE_ENCRYPTION=${ARROW_WITH_OPENSSL:-ON} \
      -DProtobuf_SOURCE=${Protobuf_SOURCE:-AUTO} \
      -DRapidJSON_SOURCE=${RapidJSON_SOURCE:-AUTO} \
      -DRE2_SOURCE=${RE2_SOURCE:-AUTO} \
      -DThrift_SOURCE=${Thrift_SOURCE:-AUTO} \
      -DZSTD_SOURCE=${ZSTD_SOURCE:-AUTO} \
      ${CMAKE_ARGS} \
      ${source_dir}

cmake --build . --target install

popd

if [ "${ARROW_USE_CCACHE}" == "ON" ]; then
    echo -e "===\n=== ccache statistics after build\n==="
    ccache -s
fi

if [ "${with_docs}" == "true" ]; then
  pushd ${source_dir}/apidoc
  doxygen
  popd
fi
