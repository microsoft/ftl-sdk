cmake_minimum_required (VERSION 2.6)
enable_language(C)
project(libftl)

include_directories(src/libftl)

if (WIN32)
  set(CHARON_PLATFORM_FILES src/charon/win32/xgetopt.c
                            src/charon/win32/xgetopt.h
                            src/charon/win32/ctrlc_handler.c)
  set(CHARON_PLATFORM_LIBS kernel32 user32 gdi32 advapi32)
  set(FTL_PLATFORM_FILES src/libftl/win32/socket.c)
else()
  set(CHARON_PLATFORM_FILES src/charon/posix/ctrlc_handler.c)
  set(FTL_PLATFORM_FILES src/libftl/posix/socket.c)
endif()

add_library(ftl SHARED src/libftl/activate.c
                       src/libftl/charon_protocol.c
                       src/libftl/components.c
                       src/libftl/deactivate.c
                       src/libftl/init.c
                       src/libftl/logging.c
                       src/libftl/stream_configuration.c
                       src/libftl/ftl.h
                       src/libftl/ftl_private.h
             ${FTL_PLATFORM_FILES})
set_target_properties(ftl PROPERTIES VERSION "0.2.3")
set_target_properties(ftl PROPERTIES SOVERSION 0)

if(WIN32)
  target_link_libraries(ftl ws2_32)
endif()

add_executable(charon
               src/charon/main.c
               src/charon/charon.h
               ${CHARON_PLATFORM_FILES})
target_link_libraries(charon ftl ${CHARON_PLATFORM_LIBS})
target_include_directories(charon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/charon)

# Install rules
install(TARGETS ftl DESTINATION lib)
install(FILES src/libftl/ftl.h DESTINATION "include/ftl")
