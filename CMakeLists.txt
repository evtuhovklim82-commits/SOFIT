cmake_minimum_required(VERSION 3.21)
project(SOFIT VERSION 0.5.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)

add_library(sofit_core STATIC
    src/core/EntityPath.cpp
    src/core/PatternPdf.cpp
    src/core/PieceMarkings.cpp
    src/core/PatternDxf.cpp
    src/core/PatternDocument.cpp
    src/core/PatternDocument.h
    src/core/PieceGeometry.cpp
    src/core/PieceGeometry.h
)
target_link_libraries(sofit_core PUBLIC Qt6::Gui)
target_include_directories(sofit_core PUBLIC src/core)

add_library(sofit_widgets STATIC
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/PatternView.cpp
    src/ui/PatternView.h
)
target_link_libraries(sofit_widgets PUBLIC sofit_core Qt6::Widgets)
target_include_directories(sofit_widgets PUBLIC src/ui)

qt_add_executable(sofit src/app/main.cpp)
target_link_libraries(sofit PRIVATE sofit_widgets)
set_target_properties(sofit PROPERTIES WIN32_EXECUTABLE TRUE MACOSX_BUNDLE TRUE)
install(TARGETS sofit BUNDLE DESTINATION . RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
    add_executable(sofit_core_tests tests/PieceGeometryTest.cpp)
    target_link_libraries(sofit_core_tests PRIVATE sofit_core)
    add_test(NAME sofit_core COMMAND sofit_core_tests)

    add_executable(sofit_ui_tests tests/PatternWorkflowTest.cpp)
    target_link_libraries(sofit_ui_tests PRIVATE sofit_widgets)
    add_test(NAME sofit_pattern_workflow COMMAND sofit_ui_tests)
    set_tests_properties(sofit_pattern_workflow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 20)
endif()
