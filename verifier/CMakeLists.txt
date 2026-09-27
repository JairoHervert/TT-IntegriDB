add_executable(tt_verifier main.cpp)
target_link_libraries(tt_verifier PRIVATE tt_core)
target_compile_options(tt_verifier PRIVATE ${TT_WARNINGS})
