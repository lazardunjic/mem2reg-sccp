add_library(CustomSCCP MODULE
  CustomSCCP.cpp
)

target_compile_features(CustomSCCP PRIVATE cxx_std_17)
set_target_properties(CustomSCCP PROPERTIES
  COMPILE_FLAGS "-fno-rtti"
  POSITION_INDEPENDENT_CODE ON
  PREFIX ""
)
