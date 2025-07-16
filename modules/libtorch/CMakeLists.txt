cmake_minimum_required(VERSION 3.10)
project(gots_libtorch_module)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find PyTorch
find_package(Torch REQUIRED)

# Create shared library
add_library(gots_torch SHARED
    libtorch_wrapper.cpp
)

# Link against libtorch
target_link_libraries(gots_torch "${TORCH_LIBRARIES}")
target_include_directories(gots_torch PRIVATE "${TORCH_INCLUDE_DIRS}")

# Set output name without lib prefix
set_target_properties(gots_torch PROPERTIES PREFIX "")

# Installation
install(TARGETS gots_torch DESTINATION lib)
install(FILES torch.gts DESTINATION modules)