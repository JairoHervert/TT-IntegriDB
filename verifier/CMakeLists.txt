add_executable(tt_verifier
  main.cpp
  client_common.cpp
  query_command.cpp
  verify_command.cpp
  ping_command.cpp
)
target_link_libraries(tt_verifier PRIVATE tt_core tt_net tt_cli)
target_compile_options(tt_verifier PRIVATE ${TT_WARNINGS})
