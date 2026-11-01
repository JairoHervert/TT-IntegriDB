add_executable(tt_verifier
  main.cpp
  client_common.cpp
  query_command.cpp
  verify_command.cpp
  ping_command.cpp
  ui_command.cpp
)
target_link_libraries(tt_verifier PRIVATE tt_core tt_net tt_ui tt_cli)
target_compile_options(tt_verifier PRIVATE ${TT_WARNINGS})
# Carpeta de la pagina de la interfaz cuando no se indica --web.
target_compile_definitions(tt_verifier PRIVATE TT_UI_DEFAULT_WEB_DIR="${TT_UI_WEB_DIR}")
