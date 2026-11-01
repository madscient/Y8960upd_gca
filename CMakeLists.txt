cmake_minimum_required(VERSION 3.21)

project(y8960upd_gca VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(Y8960UPD_GCA_EXE_NAME y8960upd_gca)

add_executable(${Y8960UPD_GCA_EXE_NAME}
    src/main.cpp
)
target_compile_definitions(${Y8960UPD_GCA_EXE_NAME} PRIVATE
    Y8960UPD_GCA_NAME="${Y8960UPD_GCA_EXE_NAME}"
    Y8960UPD_GCA_VERSION="${PROJECT_VERSION}"
)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(test)
endif()

install(TARGETS ${Y8960UPD_GCA_EXE_NAME} DESTINATION bin)
