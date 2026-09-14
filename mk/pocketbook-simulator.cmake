include_guard(GLOBAL)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick)
set(POCKETBOOK_SIMULATOR_DIR "${CMAKE_CURRENT_LIST_DIR}/../tools/pocketbook-simulator")
get_filename_component(POCKETBOOK_SIMULATOR_DIR "${POCKETBOOK_SIMULATOR_DIR}" ABSOLUTE)
set(POCKETBOOK_SIM_CONTROLS "${POCKETBOOK_SIMULATOR_DIR}/qt-controls")
add_library(pocketbook-simulator OBJECT
  "${POCKETBOOK_SIMULATOR_DIR}/device.cpp"
  "${POCKETBOOK_SIMULATOR_DIR}/device.h"
  "${POCKETBOOK_SIMULATOR_DIR}/device.qrc")
set_target_properties(pocketbook-simulator PROPERTIES AUTOMOC ON AUTORCC ON)
target_compile_features(pocketbook-simulator PUBLIC cxx_std_17)
target_compile_definitions(pocketbook-simulator PUBLIC POCKETBOOK_SIMULATOR POCKETBOOK_SIM_CONTROLS="${POCKETBOOK_SIM_CONTROLS}")
target_include_directories(pocketbook-simulator PUBLIC "${POCKETBOOK_SIMULATOR_DIR}")
target_link_libraries(pocketbook-simulator PUBLIC Qt6::Quick Qt6::Qml Qt6::Gui Qt6::Core)

function(pocketbook_add_simulator_app target)
  cmake_parse_arguments(APP "" "" "SOURCES;QRC" ${ARGN})
  add_executable(${target} ${APP_SOURCES} ${APP_QRC})
  set_target_properties(${target} PROPERTIES AUTOMOC ON AUTORCC ON)
  target_link_libraries(${target} PRIVATE pocketbook-simulator)
endfunction()
