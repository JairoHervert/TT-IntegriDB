# Utilidades de linea de comandos que comparten tt_owner, tt_prover y
# tt_verifier: errores de uso, lectura de opciones, tiempos y tamanos de
# archivo. No contiene nada del esquema.

add_library(tt_cli STATIC
  src/cli.cpp
)

target_include_directories(tt_cli PUBLIC include)
target_compile_options(tt_cli PRIVATE ${TT_WARNINGS})
