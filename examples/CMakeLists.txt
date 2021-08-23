add_executable(examples main.cpp ../pbar.hpp)

target_include_directories(examples PRIVATE ../)

target_compile_options(examples PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8 /Zc:__cplusplus /Zc:preprocessor>
)

option(ENABLE_CXX20 "Enable C++20 feature" OFF)

if (ENABLE_CXX20)
target_compile_features(examples PRIVATE cxx_std_20)
else()
target_compile_features(examples PRIVATE cxx_std_17)
endif()

if (UNIX)
find_package(Threads REQUIRED)
target_link_libraries(examples Threads::Threads)
endif()