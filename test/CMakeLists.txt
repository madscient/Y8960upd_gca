# 引数なしで走らせると MGA を探してカートリッジの電源を入れるので、-h で走らせる
add_test(NAME smoke_help COMMAND ${Y8960UPD_GCA_EXE_NAME} -h)
set_tests_properties(smoke_help PROPERTIES
    PASS_REGULAR_EXPRESSION "^${Y8960UPD_GCA_EXE_NAME} ${PROJECT_VERSION} \\[.*Usage: "
)

# vcpkg を使わない Linux のビルドでは Catch2 が無いことがある。そのときは単体の試験を飛ばす
find_package(Catch2 3 QUIET)
if(NOT Catch2_FOUND)
    message(WARNING "Catch2 3 not found: unit tests are not built")
    return()
endif()

add_executable(unit_tests
    fake/fake_mga.cpp
    unit/test_cli.cpp
    unit/test_image.cpp
    unit/test_mga.cpp
    unit/test_mga_script.cpp
    unit/test_updater.cpp
    unit/test_y8960.cpp
    unit/test_zx0.cpp
)
if(NOT WIN32)
    target_sources(unit_tests PRIVATE unit/test_poll_posix.cpp)
endif()
target_include_directories(unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(unit_tests PRIVATE y8960upd_gca_core Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(unit_tests)
