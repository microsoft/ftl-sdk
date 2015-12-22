cmake_minimum_required (VERSION 2.6)
project(libftl)

include_directories(libftl)

if (WIN32)
	set(FTL_PLATFORM_FILES libftl/win32/socket.c)
endif()

add_library(ftl SHARED libftl/activate.c
                       libftl/charon_protocol.c
                       libftl/components.c
                       libftl/deactivate.c
                       libftl/init.c
                       libftl/logging.c
                       libftl/stream_configuration.c
					   libftl/ftl.h
					   libftl/ftl_private.h
					   ${FTL_PLATFORM_FILES})
if(WIN32)
  target_link_libraries(ftl ws2_32)
endif()

add_executable(charon charon/main.c)
target_link_libraries(charon ftl)
