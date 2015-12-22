cmake_minimum_required (VERSION 2.6)
project(libftl)

include_directories(libftl)

add_library(ftl SHARED libftl/activate.c
                       libftl/charon_protocol.c
                       libftl/components.c
                       libftl/deactivate.c
                       libftl/init.c
                       libftl/logging.c
                       libftl/stream_configuration.c
					   libftl/ftl.h
					   libftl/ftl_private.h)
if(WIN32)
  target_link_libraries(ftl ws2_32)
endif()

add_executable(charon charon/main.c)
target_link_libraries(charon ftl)
