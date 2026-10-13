add_executable(tt_prover
  main.cpp
  answer_command.cpp
)
target_link_libraries(tt_prover PRIVATE tt_core tt_mysql tt_cli)
target_compile_options(tt_prover PRIVATE ${TT_WARNINGS})
