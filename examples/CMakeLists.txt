add_executable(examples main.cpp ../pbar.hpp)

target_include_directories(examples PRIVATE ../)

target_compile_options(examples PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8 /Zc:__cplusplus /Zc:preprocessor>
)
target_compile_features(examples PRIVATE cxx_std_17)