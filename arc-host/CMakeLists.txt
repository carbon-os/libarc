cmake_minimum_required(VERSION 3.21)

add_executable(arc-host main.cpp)
target_compile_features(arc-host PRIVATE cxx_std_20)
target_link_libraries(arc-host PRIVATE host)

if(UNIX)
    target_link_libraries(arc-host PRIVATE ${CMAKE_DL_LIBS})
endif()