add_library(CustomMem2Reg MODULE
  CustomMem2Reg.cpp
)

target_compile_features(CustomMem2Reg PRIVATE cxx_std_17)
set_target_properties(CustomMem2Reg PROPERTIES
  COMPILE_FLAGS "-fno-rtti"
  POSITION_INDEPENDENT_CODE ON
  PREFIX ""
)