# SpatialCore: pure C++ geometry and gain computation. Must never depend on JUCE.
add_library(TriPanCore STATIC
    src/Limits.cpp
    src/Triangulation.cpp
    src/Gains.cpp)

target_include_directories(TriPanCore PUBLIC include)
target_compile_features(TriPanCore PUBLIC cxx_std_20)
