cmake_minimum_required(VERSION 3.21)
project(SOFIT VERSION 0.3.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
qt_add_executable(sofit
    src/app/main.cpp
    src/core/PatternDocument.cpp
    src/core/PatternDocument.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/PatternView.cpp
    src/ui/PatternView.h
)
target_link_libraries(sofit PRIVATE Qt6::Widgets)
set_target_properties(sofit PROPERTIES WIN32_EXECUTABLE TRUE MACOSX_BUNDLE TRUE)
install(TARGETS sofit BUNDLE DESTINATION . RUNTIME DESTINATION bin)
