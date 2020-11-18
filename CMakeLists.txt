project(cspoc C CXX)

set(SRCS src/mono/Main.cpp)

include_directories(/usr/lib/pkgconfig/../../include/mono-2.0 )

set(CMAKE_CXX_STANDARD "17")
add_executable(cspoc ${SRCS})