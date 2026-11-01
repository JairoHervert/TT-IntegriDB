# Interfaz del caso de estudio (bloque Cliente): servidor HTTP local que
# entrega la pagina de ui/web y verifica, con delta y la clave publica, cada
# respuesta que pide al probador. Es un adaptador: la verificacion esta en
# tt_core y la comunicacion con el probador en tt_net. La ejecuta
# 'tt_verifier ui'.

add_library(tt_ui STATIC
  src/json.cpp
  src/portal.cpp
  src/ui_server.cpp
)

target_include_directories(tt_ui PUBLIC include)
target_include_directories(tt_ui SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/cpp-httplib)
target_link_libraries(tt_ui PUBLIC tt_core tt_net PRIVATE Threads::Threads)
target_compile_options(tt_ui PRIVATE ${TT_WARNINGS})

# Carpeta de la pagina que usa 'tt_verifier ui' si no se indica --web.
set(TT_UI_WEB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/web" PARENT_SCOPE)
