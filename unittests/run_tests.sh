#! /bin/bash
set -e

SOURCE=$(readlink -f "${BASH_SOURCE[0]}")
BASE_DIR="$(dirname "$SOURCE")"

cd "$BASE_DIR"

if [ "$1" != "-nc" ]; then
    rm -rf build_tests
    mkdir -p build_tests
fi
cd build_tests
cmake $BASE_DIR
make -j 2

ctest --timeout 5 --output-on-failure -O "$BASE_DIR/tests_memcheck.log"
cat ./Testing/Temporary/MemoryChecker.*.log
