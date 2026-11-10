# Adaptador MySQL (libmysqlclient): conexion y lectura de la base para el
# propietario (Setup) y para el probador (filas de cada respuesta). Esta
# separado del nucleo para que tt_core no dependa de la base de datos ni de
# la red.
#
# Requiere:  sudo apt install libmysqlclient-dev

find_path(MYSQL_INCLUDE_DIR mysql/mysql.h)
find_library(MYSQLCLIENT_LIB mysqlclient)
if(NOT MYSQL_INCLUDE_DIR OR NOT MYSQLCLIENT_LIB)
  message(FATAL_ERROR
    "No se encontro libmysqlclient. Instalala con: sudo apt install libmysqlclient-dev")
endif()

add_library(tt_mysql STATIC
  src/connection.cpp
  src/database_source.cpp
  src/row_store.cpp
  src/direct_query.cpp
)

target_include_directories(tt_mysql PUBLIC include PRIVATE ${MYSQL_INCLUDE_DIR})
target_link_libraries(tt_mysql PUBLIC tt_core PRIVATE ${MYSQLCLIENT_LIB})
target_compile_options(tt_mysql PRIVATE ${TT_WARNINGS})
