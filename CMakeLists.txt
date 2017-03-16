cmake_minimum_required(VERSION 3.1.0 FATAL_ERROR)
set (CMAKE_CXX_STANDARD 11)
project(libftl CXX)

# Options
option(DISABLE_AUTO_INGEST "Set to TRUE to disable auto ingest feature which removes curl and jansson dependancies" FALSE)

if(NOT DISABLE_CMAKE_FOLDER_GEN)
  set_property(GLOBAL PROPERTY USE_FOLDERS ON) 
endif()

find_package(Threads REQUIRED)

set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

FIND_PACKAGE(CURL)

if (DISABLE_AUTO_INGEST)
add_definitions(-DDISABLE_AUTO_INGEST)
endif()

if (NOT CURL_FOUND AND NOT DISABLE_AUTO_INGEST)
SET(CURL_DISABLE_NTLM ON CACHE BOOL "Disabling NTLM")
SET(CURL_DISABLE_TELNET ON CACHE BOOL "Disabling Telnet")
SET(CURL_DISABLE_LDAP ON CACHE BOOL "Disabling Ldap")
SET(CURL_DISABLE_LDAPS ON CACHE BOOL "Disabling secure ldap")
SET(BUILD_CURL_EXE OFF CACHE BOOL "Building libcurl")
SET(HTTP_ONLY ON CACHE BOOL "using compiling HTTP")
SET(BUILD_TESTING OFF CACHE BOOL "Not building Tests")
add_subdirectory(libcurl)
SET(CURL_INCLUDE_DIRS libcurl/include ${CMAKE_CURRENT_BINARY_DIR}/libcurl/include/curl)
SET(CURL_LIBRARIES libcurl)
endif()
MESSAGE( STATUS "Curl include dir: " ${CURL_INCLUDE_DIRS}) 
include_directories(${CURL_INCLUDE_DIRS})

if (NOT DISABLE_AUTO_INGEST)
SET(JANSSON_BUILD_DOCS OFF CACHE BOOL "Jansson docs off")
SET(JANSSON_WITHOUT_TESTS ON CACHE BOOL "Jansson build without tests")
SET(JANSSON_EXAMPLES OFF CACHE BOOL "Jansson disable examples")
SET(USE_WINDOWS_CRYPTOAPI off)
add_subdirectory(libjansson)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/libjansson/include)
SET(JANSSON_LIBRARIES jansson)
endif()

################
# Protobuf
################
SET(protobuf_BUILD_TESTS OFF)
SET(protobuf_BUILD_EXAMPLES OFF)
SET(protobuf_BUILD_SHARED_LIBS ON)
add_subdirectory(ftl-protobuf/protobuf-cpp-3.2.0/cmake/)
set_target_properties(libprotobuf PROPERTIES FOLDER "ProtoBuf")
set_target_properties(libprotobuf-lite PROPERTIES FOLDER "ProtoBuf")
include_directories(libftl ftl-protobuf/protobuf-cpp-3.2.0/src)
set(PROTOBUF_LIBRARIES libprotobuf libprotobuf-lite)

include_directories(libftl libftl/gettimeofday)
include_directories(libftl ftl-protobuf/ftl/generated-cpp/)

if (WIN32)
  set(FTL_PLATFORM_FILES ftl_app/win32/xgetopt.cpp
                         ftl_app/win32/xgetopt.h
                         ftl_app/win32/ctrlc_handler.cpp)
  #set(FTL_PLATFORM_LIBS kernel32 user32 gdi32 advapi32 )
  #set(FTL_PLATFORM_LIBS ws2_32 )
  set(FTLSDK_PLATFORM_FILES libftl/win32/socket.cpp
                      libftl/win32/socket.h
	                    libftl/win32/threads.cpp
		                  libftl/win32/threads.h)

  include_directories(libftl/win32)
else()
  set(FTL_PLATFORM_FILES ftl_app/posix/ctrlc_handler.cpp)
  set(FTLSDK_PLATFORM_FILES libftl/posix/socket.cpp
                            libftl/posix/socket.h
	        	                libftl/posix/threads.cpp
			                      libftl/posix/threads.h)
  include_directories(libftl/posix)
endif()

add_library(ftl SHARED libftl/hmac/hmac.cpp
                       libftl/hmac/hmac.h
                       libftl/hmac/sha2.cpp
                       libftl/hmac/sha2.h
                       libftl/gettimeofday/gettimeofday.cpp
                       libftl/gettimeofday/gettimeofday.h
                       ftl-protobuf/ftl/generated-cpp/IngestConnection.pb.cc
                       ftl-protobuf/ftl/generated-cpp/IngestConnection.pb.h
                       libftl/ftl-sdk.cpp
                       libftl/handshake.cpp
                       libftl/ingest.cpp
                       libftl/ftl_helpers.cpp
                       libftl/media.cpp
                       libftl/logging.cpp
                       libftl/ftl.h
                       libftl/ftl_private.h
                       ${FTLSDK_PLATFORM_FILES})
set_target_properties(ftl PROPERTIES VERSION "0.5.0")
set_target_properties(ftl PROPERTIES SOVERSION 0)

target_link_libraries(ftl ${CURL_LIBRARIES} ${JANSSON_LIBRARIES} ${PROTOBUF_LIBRARIES})
if(WIN32)
  target_link_libraries(ftl ws2_32)
endif()

add_executable(ftl_app
               ftl_app/main.cpp
               ftl_app/main.h
               ftl_app/file_parser.cpp
               ftl_app/file_parser.h			   
               ftl_app/gettimeofday.cpp
	             ftl_app/gettimeofday.h
               ${FTL_PLATFORM_FILES})

target_link_libraries(ftl_app ftl ${CURL_LIBRARIES} ${JANSSON_LIBRARIES} ${PROTOBUF_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${FTL_PLATFORM_LIBS})
target_include_directories(ftl_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ftl_app)

# Install rules
install(TARGETS ftl DESTINATION lib)


