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

add_library(y8960upd_gca_core STATIC
    src/cli/cli.cpp
    src/image/image.cpp
    src/mga/mga.cpp
    src/mga/mga_script.cpp
    src/updater/updater.cpp
    src/y8960/config_rom.cpp
    src/y8960/system_controller.cpp
)
if(WIN32)
    target_sources(y8960upd_gca_core PRIVATE src/cli/interrupt_win32.cpp src/serial/serial_win32.cpp)
    target_link_libraries(y8960upd_gca_core PUBLIC setupapi advapi32)
else()
    target_sources(y8960upd_gca_core PRIVATE src/cli/interrupt_posix.cpp src/serial/serial_posix.cpp)
endif()
target_include_directories(y8960upd_gca_core PUBLIC src)
target_compile_definitions(y8960upd_gca_core PUBLIC
    Y8960UPD_GCA_NAME="${Y8960UPD_GCA_EXE_NAME}"
    Y8960UPD_GCA_VERSION="${PROJECT_VERSION}"
)

add_executable(${Y8960UPD_GCA_EXE_NAME}
    src/main.cpp
)
target_link_libraries(${Y8960UPD_GCA_EXE_NAME} PRIVATE y8960upd_gca_core)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(test)
endif()

install(TARGETS ${Y8960UPD_GCA_EXE_NAME} DESTINATION bin)
