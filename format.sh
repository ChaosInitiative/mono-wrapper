#!/bin/sh
TOP=$(cd $(dirname $0);pwd)
cd "$TOP"

clang-format -i `find ./src -iname "*.cpp" -o -iname "*.h"`