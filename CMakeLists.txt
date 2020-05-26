cmake_minimum_required(VERSION 3.10)
project(Voxualize)

ADD_LIBRARY(Voxualize
    src/Voxualize.cpp
)
target_link_libraries(Voxualize -lybuycfitsio)
# target_link_libraries(Voxualize -lm)
# target_link_libraries(Voxualize -lnsl)
# target_link_libraries(Voxualize -lsocket)

add_executable(Voxualize.exe src/Voxualize.cpp)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
