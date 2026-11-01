add_test(NAME smoke_banner COMMAND ${Y8960UPD_GCA_EXE_NAME})
set_tests_properties(smoke_banner PROPERTIES
    PASS_REGULAR_EXPRESSION "^${Y8960UPD_GCA_EXE_NAME} ${PROJECT_VERSION}[\r\n]"
)
