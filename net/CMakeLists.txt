# Comunicacion por red entre el modulo probador (servidor) y el modulo
# verificador (cliente), con HTTP/1.1 (cpp-httplib, copia fija en
# third_party/). Es un adaptador: la logica del esquema esta en tt_core.

add_library(tt_net STATIC
  src/protocol.cpp
  src/prover_server.cpp
  src/prover_client.cpp
)

target_include_directories(tt_net PUBLIC include)
# SYSTEM: las advertencias de la biblioteca externa no se mezclan con las
# del proyecto.
target_include_directories(tt_net SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/cpp-httplib)
target_link_libraries(tt_net PUBLIC tt_core PRIVATE Threads::Threads)
target_compile_options(tt_net PRIVATE ${TT_WARNINGS})
