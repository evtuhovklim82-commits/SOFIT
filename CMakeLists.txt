cmake_minimum_required(VERSION 3.21)
project(SOFIT VERSION 4.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(MSVC)
    add_compile_options(/utf-8)
endif()
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)

add_library(sofit_core STATIC
    src/core/BodiceDraft.cpp
    src/core/NeckFinish.cpp
    src/core/FittingBlocks.cpp
    src/core/ContourModeling.cpp
    src/core/EntityPath.cpp
    src/core/Modeling.cpp
    src/core/ModelingHistory.cpp
    src/core/PatternPdf.cpp
    src/core/ProductInformation.cpp
    src/core/Production.cpp
    src/core/SeamLinks.cpp
    src/core/PieceMarkings.cpp
    src/core/PatternDxf.cpp
    src/core/DxfImport.cpp
    src/core/ParametricDraft.cpp
    src/core/PatternTemplates.cpp
    src/core/ProjectRecovery.cpp
    src/core/PatternDocument.cpp
    src/core/PatternDocument.h
    src/core/PieceGeometry.cpp
    src/core/PieceGeometry.h
)
target_link_libraries(sofit_core PUBLIC Qt6::Gui)
target_include_directories(sofit_core PUBLIC src/core)

add_library(sofit_widgets STATIC
    src/ui/ParametricDialog.cpp
    src/ui/TemplateWizard.cpp
    src/ui/Workflow.cpp
    src/ui/MainWindow.cpp
    src/ui/DxfImportDialog.cpp
    src/ui/ProductionDialogs.cpp
    src/ui/MainWindow.h
    src/ui/PatternView.cpp
    src/ui/PatternView.h
)
target_link_libraries(sofit_widgets PUBLIC sofit_core Qt6::Widgets)
target_include_directories(sofit_widgets PUBLIC src/ui)

qt_add_executable(sofit src/app/main.cpp)
target_link_libraries(sofit PRIVATE sofit_widgets)
qt_add_resources(sofit "app_icon" PREFIX "/" BASE src/app/assets FILES src/app/assets/sofit.png)
if(WIN32)
    enable_language(RC)
    target_sources(sofit PRIVATE src/app/Sofit.rc)
    target_include_directories(sofit PRIVATE src/app/assets)
endif()
target_compile_definitions(sofit PRIVATE SOFIT_VERSION="${PROJECT_VERSION}")
set_target_properties(sofit PROPERTIES WIN32_EXECUTABLE TRUE MACOSX_BUNDLE TRUE)
install(TARGETS sofit BUNDLE DESTINATION . RUNTIME DESTINATION bin)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES LICENSE-NOTICE.txt README.md DESTINATION share/doc/sofit)
    install(FILES docs/SOFIT-format.md docs/GRAFIS-26-audit.md DESTINATION share/doc/sofit/docs)
    install(DIRECTORY examples DESTINATION share/doc/sofit)
    set(CPACK_GENERATOR TGZ)
    set(CPACK_PACKAGE_CHECKSUM SHA256)
    include(CPack)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(sofit_project_tests tests/ProjectPersistenceTest.cpp)
    target_link_libraries(sofit_project_tests PRIVATE sofit_core)
    add_test(NAME sofit_project_persistence COMMAND sofit_project_tests "${CMAKE_CURRENT_SOURCE_DIR}/examples")
    set_tests_properties(sofit_project_persistence PROPERTIES TIMEOUT 30)

    add_executable(sofit_dxf_tests tests/DxfImportTest.cpp)
    target_link_libraries(sofit_dxf_tests PRIVATE sofit_core)
    add_test(NAME sofit_dxf_import COMMAND sofit_dxf_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

    add_executable(sofit_core_tests tests/PieceGeometryTest.cpp)
    target_link_libraries(sofit_core_tests PRIVATE sofit_core)
    add_test(NAME sofit_core COMMAND sofit_core_tests)

    add_executable(sofit_production_tests tests/ProductionTest.cpp)
    target_link_libraries(sofit_production_tests PRIVATE sofit_core)
    add_test(NAME sofit_production COMMAND sofit_production_tests)
    set_tests_properties(sofit_production PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(sofit_parametric_tests tests/ParametricDraftTest.cpp)
    target_link_libraries(sofit_parametric_tests PRIVATE sofit_core)
    add_test(NAME sofit_parametric COMMAND sofit_parametric_tests)

    add_executable(sofit_professional_tests tests/ProfessionalModelingTest.cpp)
    target_link_libraries(sofit_professional_tests PRIVATE sofit_core)
    add_test(NAME sofit_professional_modeling COMMAND sofit_professional_tests)
    set_tests_properties(sofit_professional_modeling PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_test(NAME sofit_production_automation COMMAND ${CMAKE_COMMAND}
        "-DSOFIT=$<TARGET_FILE:sofit>"
        "-DEXAMPLES=${CMAKE_CURRENT_SOURCE_DIR}/examples"
        "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/automation-test"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/ProductionAutomation.cmake")
    set_tests_properties(sofit_production_automation PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

    add_executable(sofit_ui_tests tests/PatternWorkflowTest.cpp)
    target_link_libraries(sofit_ui_tests PRIVATE sofit_widgets)
    add_test(NAME sofit_pattern_workflow COMMAND sofit_ui_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    set_tests_properties(sofit_pattern_workflow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 20)
endif()
