#!/usr/bin/env bash
set -e

export CPPFLAGS="-I./poco/include -DPOCO_STATIC"
export CXXFLAGS="-g -std=c++1y"
export LDFLAGS="-static -L./poco/lib"
export LDLIBS="-lPocoFoundation"
respite
mv a.respite dorf.exe
