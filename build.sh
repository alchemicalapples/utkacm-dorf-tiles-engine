#!/usr/bin/env bash
set -e

export CXXFLAGS="-g -std=c++1y -I./poco/include"
export LDFLAGS="-static -L./poco/lib"
export LDLIBS="-lPocoFoundation"
respite
mv a.respite dorf.exe
