# Some notes:
#  - If it can't figure out the platform you're building for, pass it via -DPLATFORM=[win|linux]-[arm|arm64|x86|x64]
#  - We're using precompiled mono libs here because it's a special version of mono 
#  - You need to have .NET 5 SDK installed and on the path
cmake_minimum_required(VERSION 3.14) # Min 3.14 for vs2019 support

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

##################################################################################################################
# Holy dear mother of god this is AWFUL
# Why does CMake NOT have any functionality to detect target arch????
if(UNIX)
	EXECUTE_PROCESS(COMMAND uname -m COMMAND tr -d '\n' OUTPUT_VARIABLE ARCHITECTURE)
	if(${ARCHITECTURE} STREQUAL "x86_64")
		set(PLATFORM "linux-x64")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse -msse2")
	elseif (${ARCHITECTURE} STREQUAL "i386" OR ${ARCHITECTURE} STREQUAL "i686")
		set(PLATFORM "linux-x86")
	elseif (${ARCHITECTURE} STREQUAL "arm")
		set(PLATFORM "linux-arm")
	elseif (${ARCHITECTURE} STREQUAL "aarch64" OR ${ARCHITECTURE} STREQUAL "armv8l"
		OR ${ARCHITECTURE} STREQUAL "armv8b" OR ${ARCHITECTURE} STREQUAL "aarch64_be")
		set(PLATFORM "linux-arm64")
	elseif (NOT DEFINED PLATFORM)
		message(FATAL_ERROR "Unable to determine architecture, sorry. Please give it to us with -DPLATFORM")
	endif()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable")
elseif(MINGW OR MSYS OR CYGWIN)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
	EXECUTE_PROCESS(COMMAND uname -m COMMAND tr -d '\n' OUTPUT_VARIABLE ARCHITECTURE)
	if(${ARCHITECTURE} STREQUAL "x86_64")
		set(PLATFORM "win-x64")
	elseif (${ARCHITECTURE} STREQUAL "i386" OR ${ARCHITECTURE} STREQUAL "i686")
		set(PLATFORM "win-x86")
	elseif (${ARCHITECTURE} STREQUAL "arm")
		set(PLATFORM "win-arm")
	elseif (NOT DEFINED PLATFORM)
		message(FATAL_ERROR "Unable to determine architecture, sorry. Please give it to us with -DPLATFORM")
	endif()
elseif(WIN32)
	set(PLATFORM "windows-x86")
	if("${MSVC_C_ARCHITECTURE_ID}" STREQUAL "X86")
		set(PLATFORM "win-x86")
	elseif("${MSVC_C_ARCHITECTURE_ID}" STREQUAL "x64")
		set(PLATFORM "win-x64")
	elseif("${MSVC_C_ARCHITECTURE_ID}" STREQUAL "ARM")
		set(PLATFORM "win-arm")
	elseif(NOT DEFINED PLATFORM)
		message(FATAL_ERROR "Unable to determine architecture, sorry. Please give it to us with -DPLATFORM")
	endif()
endif()

##################################################################################################################

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib/${PLATFORM})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib/${PLATFORM})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin/)

include_directories(thirdparty/mono/include)
link_directories(thirdparty/mono/lib/${PLATFORM} ${CMAKE_CURRENT_SOURCE_DIR}/lib/${PLATFORM})

##################################################################################################################
project(MonoWrapper C CXX)

set(MONOWRAPPER_SRC	src/monowrapper.cpp)

add_library(MonoWrapper STATIC ${MONOWRAPPER_SRC})

set_target_properties(MonoWrapper PROPERTIES PUBLIC_HEADER "monowrapper.h")

INSTALL(TARGETS MonoWrapper
	LIBRARY DESTINATION lib/${PLATFORM}
	PUBLIC_HEADER DESTINATION include)
##################################################################################################################


##################################################################################################################
function(BUILD_DOTNET SRCDIR)
	add_custom_command(
		POST_BUILD
		OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/bin/${SRCDIR}.dll"
		COMMAND dotnet build "${CMAKE_CURRENT_SOURCE_DIR}/src/scripts/${SRCDIR}" --output "${CMAKE_CURRENT_SOURCE_DIR}/bin/" -c Debug
	)
	add_custom_target(${SRCDIR} ALL DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/bin/${SRCDIR}.dll")
endfunction()

function(INSTALL_RANDOM_FILE TARGET INFILE OUTFILE)
	add_custom_command(
		TARGET ${TARGET} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/${INFILE}" "${CMAKE_CURRENT_SOURCE_DIR}/${OUTFILE}"
	)
endfunction()

##################################################################################################################

##################################################################################################################
if(DEFINED BUILD_TESTS)
	project(MonoWrapperTest C CXX)
	
	set(TEST_SRC 	src/tests/test_main.cpp)
	
	add_executable(MonoWrapperTest ${TEST_SRC})
	
	target_link_libraries(MonoWrapperTest PUBLIC MonoWrapper coreclr)
	target_include_directories(MonoWrapperTest PUBLIC src)
	
	INSTALL(TARGETS	MonoWrapperTest
		RUNTIME DESTINATION bin
	)
	
	# Build test DLLs and stuff
	BUILD_DOTNET(test1)
	
	INSTALL_RANDOM_FILE(MonoWrapperTest src/mono-config bin/mono-config)
	if(UNIX)
		INSTALL_RANDOM_FILE(MonoWrapperTest thirdparty/mono/lib/linux-x64/libcoreclr.so bin/libmonosgen-2.0.so.1)
		INSTALL_RANDOM_FILE(MonoWrapperTest scripts/run-tests.sh bin/run-tests.sh)
	endif()
endif()
##################################################################################################################