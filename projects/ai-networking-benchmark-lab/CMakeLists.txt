cmake_minimum_required(VERSION 3.16)
project(AINetworkingBenchmarkLab VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)

add_executable(tensor_transport src/transport.cpp)
target_link_libraries(tensor_transport PRIVATE pthread)
add_executable(collective_model src/collective_model.cpp)

include(CTest)
if(BUILD_TESTING)
  add_test(NAME collective_model_basic
    COMMAND collective_model --ranks 4 --tensor-mib 64 --gbps 100 --latency-us 5)
endif()
