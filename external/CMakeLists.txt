# external/CMakeLists.txt
add_library(websocketpp INTERFACE)
target_include_directories(websocketpp INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/websocketpp>
)