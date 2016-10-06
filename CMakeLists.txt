cmake_minimum_required (VERSION 3.5.2)
enable_language(C)
project(libftl)

find_package(Threads REQUIRED)

include_directories(libftl)

if (WIN32)
  set(FTL_PLATFORM_FILES ftl_app/win32/xgetopt.c
                            ftl_app/win32/xgetopt.h
                            ftl_app/win32/ctrlc_handler.c
							ftl_app/win32/gettimeofday.c
							ftl_app/win32/gettimeofday.h)
  set(FTL_PLATFORM_LIBS kernel32 user32 gdi32 advapi32 )
  set(FTLSDK_PLATFORM_FILES libftl/win32/socket.c)
else()
  set(FTL_PLATFORM_FILES ftl_app/posix/ctrlc_handler.c)
  set(FTLSDK_PLATFORM_FILES libftl/posix/socket.c)
endif()

add_library(ftl SHARED libftl/hmac/hmac.c
                       libftl/hmac/hmac.h
                       libftl/hmac/sha2.c
                       libftl/hmac/sha2.h
                       libftl/gettimeofday/gettimeofday.c
                       libftl/gettimeofday/gettimeofday.h
                       libftl/ftl-sdk.c
                       libftl/handshake.c
                       libftl/ftl_helpers.c
					   libftl/media.c
                       libftl/logging.c
                       libftl/ftl.h
                       libftl/ftl_private.h
             ${FTLSDK_PLATFORM_FILES})
set_target_properties(ftl PROPERTIES VERSION "0.2.3")
set_target_properties(ftl PROPERTIES SOVERSION 0)

if(WIN32)
  target_link_libraries(ftl ws2_32)
endif()

add_executable(ftl_app
               ftl_app/main.c
               ftl_app/main.h
               ftl_app/file_parser.c
               ftl_app/file_parser.h			   
               ${FTL_PLATFORM_FILES})
target_link_libraries(ftl_app ftl Threads::Threads ${FTL_PLATFORM_LIBS})
#target_link_libraries(ftl_app ftl ${CMAKE_THREAD_LIBS_INIT} ${FTL_PLATFORM_LIBS})
target_include_directories(ftl_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ftl_app)

# Install rules
install(TARGETS ftl DESTINATION lib)
install(FILES libftl/ftl.h DESTINATION "include/ftl")
