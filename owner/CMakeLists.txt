add_executable(tt_owner
  main.cpp
  init_command.cpp
  setup_command.cpp
  catalog_command.cpp
)
target_link_libraries(tt_owner PRIVATE tt_core tt_mysql tt_cli)
target_compile_options(tt_owner PRIVATE ${TT_WARNINGS})
