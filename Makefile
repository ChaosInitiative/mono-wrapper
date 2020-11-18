CC?=gcc
CXX?=g++

DEFINES+=
LIBS+=-lm -ldl -lc -lrt
CXXFLAGS+=-std=c++17 $(LIBS) $(DEFINES) -g -Og -m64
SRC_MONO=$(wildcard src/mono/*.cpp)
SRC_DOTNET=$(wildcard src/netcore/*.cpp)

all:
	g++ $(SRC_MONO) $(CXXFLAGS) `pkg-config --cflags --libs mono-2` -o mono-tst
	g++ $(SRC_DOTNET) $(CXXFLAGS) -o dotnet-tst