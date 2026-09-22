add_executable(TxtPageIndexTest
  TxtPageIndexTest.cpp
  ${REPO_ROOT}/lib/Txt/TxtPageIndex.cpp
)

target_include_directories(TxtPageIndexTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/Txt
)

target_link_libraries(TxtPageIndexTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TxtPageIndexTest)
