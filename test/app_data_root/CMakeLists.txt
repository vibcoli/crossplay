add_executable(AppDataRootTest
  AppDataRootTest.cpp
  ${REPO_ROOT}/src/util/AppDataRootCore.cpp
)

target_include_directories(AppDataRootTest PRIVATE
  ${REPO_ROOT}/src/util
)

target_link_libraries(AppDataRootTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(AppDataRootTest)
