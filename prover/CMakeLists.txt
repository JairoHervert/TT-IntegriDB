add_executable(tt_prover main.cpp)
target_link_libraries(tt_prover PRIVATE tt_core)
target_compile_options(tt_prover PRIVATE ${TT_WARNINGS})
