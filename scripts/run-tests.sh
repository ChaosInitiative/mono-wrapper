#!/bin/bash

# This script gets copied into bin/

TOP=$(cd $(dirname $0);pwd)
cd "$TOP"

LIB_PATH="$(realpath ../thirdparty/mono/lib/runtime/linux-x64)"
export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$LIB_PATH"
export MONO_LIB_PATH="$LIB_PATH"

$DEBUGGER ./MonoWrapperTest