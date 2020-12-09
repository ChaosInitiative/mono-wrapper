# Makefile for the mono wrapper
# If you can't or dont want to use make, compilation is easy. Just link against mono!
CC?=gcc
CXX?=g++
MCS?=mcs

DEFINES+=
LIBS+=-lm -ldl -lc -lrt
CXXFLAGS+=-std=c++17 $(LIBS) $(DEFINES) -m64 -std=c++17

ifeq ($(CFG),release)
	CXXFLAGS+=-O3
else
	CXXFLAGS+=-g -Og
endif

SRCS=$(wildcard src/*.cpp)
OUTDIR=$(abspath ./bin)

all: test_library
	g++ $(SRCS) $(CXXFLAGS) `pkg-config --cflags --libs mono-2` -o "$(OUTDIR)/mono-tst"

test_library:
	mkdir -p "$(OUTDIR)"
	make -C src/Scripts/ OUTDIR="$(OUTDIR)" MCS="$(MCS)"

clean: 
	rm bin/mono-tst
	make -C src/Scripts/ OUTDIR="$(OUTDIR)" MCS="$(MCS)" clean
