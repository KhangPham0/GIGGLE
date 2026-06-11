add_executable(core_tests core_tests.cpp)
target_link_libraries(core_tests PRIVATE giggle_core doctest::doctest)

add_test(NAME core_tests COMMAND core_tests)

# The rootbridge tests write their own test data with ROOT, so they link
# ROOT directly in addition to the library under test.
add_executable(rootbridge_tests rootbridge_tests.cpp)
target_link_libraries(rootbridge_tests PRIVATE giggle_rootbridge doctest::doctest ROOT::Hist ROOT::RIO)

add_test(NAME rootbridge_tests COMMAND rootbridge_tests)
