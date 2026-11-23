# CMake generated Testfile for 
# Source directory: /mnt/A/SOFIT
# Build directory: /mnt/A/SOFIT/build-04
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test("sofit_core" "/mnt/A/SOFIT/build-04/sofit_core_tests")
set_tests_properties("sofit_core" PROPERTIES  _CMAKE_TEST_BUILD_DEPENDS "/mnt/A/SOFIT/build-04/sofit_core_tests" _BACKTRACE_TRIPLES "/mnt/A/SOFIT/CMakeLists.txt;35;add_test;/mnt/A/SOFIT/CMakeLists.txt;0;")
add_test("sofit_pattern_workflow" "/mnt/A/SOFIT/build-04/sofit_ui_tests")
set_tests_properties("sofit_pattern_workflow" PROPERTIES  ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT "20" _CMAKE_TEST_BUILD_DEPENDS "/mnt/A/SOFIT/build-04/sofit_ui_tests" _BACKTRACE_TRIPLES "/mnt/A/SOFIT/CMakeLists.txt;39;add_test;/mnt/A/SOFIT/CMakeLists.txt;0;")
