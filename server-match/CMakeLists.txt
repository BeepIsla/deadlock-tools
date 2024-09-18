cmake_minimum_required(VERSION 3.10)

project(server-match)

# Set some things globally
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_compile_definitions(
	-D_CRT_SECURE_NO_WARNINGS
	-D_SILENCE_ALL_CXX23_DEPRECATION_WARNINGS
	-DNOMINMAX
)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	add_compile_options(-Wno-invalid-offsetof)
endif()
if(CMAKE_CXX_BYTE_ORDER STREQUAL "BIG_ENDIAN")
	add_compile_definitions(-DENDIAN_BIG)
elseif(CMAKE_CXX_BYTE_ORDER STREQUAL "LITTLE_ENDIAN")
	add_compile_definitions(-DENDIAN_LITTLE)
endif()

include(FetchContent)

add_executable(server-match WIN32
	src/main.cpp
	src/library.cpp

	${CMAKE_CURRENT_SOURCE_DIR}/../submodules/protobufs/deadlock/citadel_gcmessages_server.proto
	${CMAKE_CURRENT_SOURCE_DIR}/../submodules/protobufs/deadlock/citadel_gcmessages_common.proto
	${CMAKE_CURRENT_SOURCE_DIR}/../submodules/protobufs/deadlock/gcsdk_gcmessages.proto
	${CMAKE_CURRENT_SOURCE_DIR}/../submodules/protobufs/deadlock/gcsystemmsgs.proto
	${CMAKE_CURRENT_SOURCE_DIR}/../submodules/protobufs/deadlock/steammessages.proto
	${CMAKE_CURRENT_SOURCE_DIR}/../submodules/protobufs/deadlock/steammessages_steamlearn.steamworkssdk.proto
	${CMAKE_CURRENT_SOURCE_DIR}/../submodules/protobufs/deadlock/steammessages_unified_base.steamworkssdk.proto
)

target_compile_features(server-match PUBLIC cxx_std_23)

target_link_libraries(server-match PUBLIC
	safetyhook
	protobuf::libprotobuf
	nlohmann_json::nlohmann_json
)

# Download safetyhook
message(STATUS "Fetching safetyhook (v0.4.1)...")
set(SAFETYHOOK_FETCH_ZYDIS ON)
FetchContent_Declare(safetyhook
	GIT_REPOSITORY "https://github.com/cursey/safetyhook.git"
	GIT_TAG v0.4.1
	GIT_SHALLOW ON
)
FetchContent_MakeAvailable(safetyhook)

# Download protobuf
message(STATUS "Fetching protobuf (v28.1)...")
set(ABSL_PROPAGATE_CXX_STD ON)
set(protobuf_INSTALL OFF)
set(protobuf_BUILD_TESTS OFF)
set(protobuf_BUILD_CONFORMANCE OFF)
set(protobuf_BUILD_EXAMPLES OFF)
set(protobuf_BUILD_PROTOBUF_BINARIES ON)
set(protobuf_BUILD_PROTOC_BINARIES ON)
set(protobuf_BUILD_LIBPROTOC ON)
set(protobuf_BUILD_SHARED_LIBS OFF)
set(protobuf_WITH_ZLIB OFF)
set(protobuf_INSTALL_EXAMPLES OFF)
set(protobuf_BUILD_LIBUPB OFF)
FetchContent_Declare(protobuf
	GIT_REPOSITORY "https://github.com/protocolbuffers/protobuf.git"
	GIT_TAG v28.1
	GIT_SHALLOW ON
)
FetchContent_MakeAvailable(protobuf)

# Download json
message(STATUS "Fetching json (v3.11.3)...")
FetchContent_Declare(json
	GIT_REPOSITORY "https://github.com/nlohmann/json.git"
	GIT_TAG v3.11.3
	GIT_SHALLOW ON
)
FetchContent_MakeAvailable(json)

# Compile *.proto files
set(PROTO_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${PROTO_BINARY_DIR})

target_include_directories(server-match PUBLIC "$<BUILD_INTERFACE:${PROTO_BINARY_DIR}>")

include(${protobuf_SOURCE_DIR}/cmake/protobuf-generate.cmake)
protobuf_generate(
	TARGET server-match
	IMPORT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../submodules/protobufs/deadlock;${CMAKE_CURRENT_SOURCE_DIR}/../submodules/protobufs"
	PROTOC_OUT_DIR "${PROTO_BINARY_DIR}"
)
