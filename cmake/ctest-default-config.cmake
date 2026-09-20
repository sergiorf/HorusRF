# Multi-config generators require CTEST_CONFIGURATION_TYPE even when every
# test command has a configuration-independent path. Match the default used by
# an unqualified `cmake --build` while still allowing `ctest -C ...` to win.
if(NOT CTEST_CONFIGURATION_TYPE)
    set(CTEST_CONFIGURATION_TYPE Debug)
endif()
