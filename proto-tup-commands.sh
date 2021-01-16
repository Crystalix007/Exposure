#! /usr/bin/env bash

GEN_DIR=$1
BUILD_DIR=$2
SRC_DIR=$3

headers=""

for source in "${GEN_DIR}"/*.capnp.c++; do
	header="${GEN_DIR}/$(basename -s ".c++" "$source").h"
	headers="${headers} ${header}"
	printf ': %s | %s |> $(CXX) $(CPPFLAGS) $(CXXFLAGS) -c %%f -o %%o |> %s/%%B.o\n' "$source" "$header" "${BUILD_DIR}"
done

printf ': foreach %s/*.cpp | %s |> $(CXX) $(CPPFLAGS) $(CXXFLAGS) -c %%f -o %%o |> %s/%%B.o\n' "${SRC_DIR}" "${headers}" "${BUILD_DIR}"
