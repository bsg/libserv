set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(libserv_SOURCES serv.c serv_epoll.c serv_select.c serv_tcp.c conn.c)
set(libserv_HEADERS serv.h)

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    set_source_files_properties(serv.c PROPERTIES LANGUAGE CXX)
    set_source_files_properties(serv_epoll.c PROPERTIES LANGUAGE CXX)
    set_source_files_properties(serv_select.c PROPERTIES LANGUAGE CXX)
    set_source_files_properties(serv_tcp.c PROPERTIES LANGUAGE CXX)
    set_source_files_properties(conn.c PROPERTIES LANGUAGE CXX)
endif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")

if(${WIN32})
    set(STATIC_NAME libserv-static)
    set(SHARED_NAME libserv)
else(${WIN32})
    set(STATIC_NAME serv-static)
    set(SHARED_NAME serv)
endif(${WIN32})

add_library(${STATIC_NAME} STATIC  ${libserv_SOURCES})

add_library(${SHARED_NAME} SHARED ${libserv_SOURCES})
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    GENERATE_EXPORT_HEADER(libserv
        BASE_NAME libserv
        EXPORT_MACRO_NAME libserv_EXPORT
        EXPORT_FILE_NAME libserv_export.h
        STATIC_DEFINE libserv_BUILT_AS_STATIC
    )
else(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    set_target_properties(${SHARED_NAME} PROPERTIES VERSION 0.0.1 SOVERSION 1)
endif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")

set_target_properties(${STATIC_NAME} PROPERTIES OUTPUT_NAME "serv")

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    message(${STATIC_NAME} "Win32 detected - Linking ws2_32")
    target_link_libraries(libserv ws2_32)
endif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")

install(TARGETS ${STATIC_NAME} ${SHARED_NAME}
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
install(FILES libserv.dll
        DESTINATION lib
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")        

install(FILES ${libserv_HEADERS}
        DESTINATION include)
