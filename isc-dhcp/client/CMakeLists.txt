
include_directories (
    ${CMAKE_CURRENT_LIST_DIR}/
    )

list (APPEND DHCLIENT_SRC_LIST 
    ${CMAKE_CURRENT_LIST_DIR}/clparse.c
    ${CMAKE_CURRENT_LIST_DIR}/dhc6.c
    ${CMAKE_CURRENT_LIST_DIR}/hwfunc.c
    ${CMAKE_CURRENT_LIST_DIR}/dhclient.c
    ${CMAKE_CURRENT_LIST_DIR}/dhclient.c
    )
