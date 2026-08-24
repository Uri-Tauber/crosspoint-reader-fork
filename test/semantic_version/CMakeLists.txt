add_executable(SemanticVersionTest
  SemanticVersionTest.cpp
)

target_include_directories(SemanticVersionTest PRIVATE
  ${REPO_ROOT}/lib/Version
)

target_link_libraries(SemanticVersionTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SemanticVersionTest)
