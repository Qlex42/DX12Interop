PROJECT(DX12Interop CXX C)
cmake_minimum_required(VERSION 3.18)

add_subdirectory(glew)

add_executable(DX12Interop WIN32 main.cpp)

TARGET_LINK_LIBRARIES(DX12Interop DXGI D3D11 D3D12 glew)

TARGET_INCLUDE_DIRECTORIES(DX12Interop PRIVATE glew/include)

SET_PROPERTY(DIRECTORY ${Smode_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT "DX12Interop")

