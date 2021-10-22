set(MEMCHECK_DIR "${THIRDPARTY_DIR}/memcheck")

set(MEMCHECK_SOURCES
    ${MEMCHECK_DIR}/src/memcheck.c
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -rdynamic")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fasynchronous-unwind-tables")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -funwind-tables")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")

add_library(memcheck SHARED ${MEMCHECK_SOURCES})
target_link_libraries(memcheck dl pthread)
