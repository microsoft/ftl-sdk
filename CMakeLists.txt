cmake_minimum_required (VERSION 2.6)
project(libftl)

include_directories(libftl)

add_library(ftl SHARED libftl/init.c
                       libftl/create_stream.c
                       libftl/destroy_stream.c)

add_executable(charon charon/main.c)
target_link_libraries(charon ftl)
