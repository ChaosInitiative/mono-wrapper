#!/bin/sh

TOP=$(cd $(dirname $0);pwd);
cd "$TOP"

$DEBUGGER ./mono-tst TestAssembly.dll mono-config