# TT 2026-B143 - construccion de todo el codigo C++
#
# Estructura:
#   core/      biblioteca compartida con el esquema (sin MySQL ni red)
#   cli/       utilidades de linea de comandos de los tres programas
#   mysql/     adaptador de MySQL (libmysqlclient) para leer la base
#   owner/     Propietario de datos: Init + Setup (se ejecuta una vez)
#   prover/    modulo probador (bloque Servidor)
#   verifier/  Cliente SQL + modulo verificador (bloque Cliente)
#   tests/     pruebas del nucleo
#
# Compilar (en la VM, desde la raiz del repositorio):
#   cmake -S . -B build
#   cmake --build build
#   ctest --test-dir build --output-on-failure

cmake_minimum_required(VERSION 3.18)
project(tt_integridb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- Dependencias externas -------------------------------------------------
#
# ate-pairing no se instala en el sistema: se clona y se compila con
#   git clone https://github.com/herumi/ate-pairing.git ~/ate-pairing
#   make -C ~/ate-pairing/src
# Si esta en otra ruta:  cmake -S . -B build -DATE_PAIRING_DIR=/otra/ruta
set(ATE_PAIRING_DIR "$ENV{HOME}/ate-pairing" CACHE PATH
    "Carpeta donde se clono y compilo ate-pairing")

find_path(ATE_PAIRING_INCLUDE_DIR bn.h
          PATHS "${ATE_PAIRING_DIR}/include" NO_DEFAULT_PATH REQUIRED)
find_library(ATE_PAIRING_LIB zm
             PATHS "${ATE_PAIRING_DIR}/lib" NO_DEFAULT_PATH REQUIRED)

# libntl-dev, libgmp-dev, libssl-dev y libmysqlclient-dev (apt)
find_library(NTL_LIB ntl REQUIRED)
find_library(GMP_LIB gmp REQUIRED)
find_library(GMPXX_LIB gmpxx REQUIRED)
find_package(OpenSSL 3 REQUIRED)
find_package(Threads REQUIRED)

# ate-pairing como objetivo importado. MIE_ATE_USE_GMP debe coincidir con la
# forma en que se compilo libzm.a (su common.mk lo define siempre).
add_library(ate_pairing STATIC IMPORTED)
set_target_properties(ate_pairing PROPERTIES IMPORTED_LOCATION "${ATE_PAIRING_LIB}")
target_include_directories(ate_pairing INTERFACE "${ATE_PAIRING_INCLUDE_DIR}")
target_compile_definitions(ate_pairing INTERFACE MIE_ATE_USE_GMP)
target_link_libraries(ate_pairing INTERFACE ${GMPXX_LIB} ${GMP_LIB})

# Advertencias para nuestro codigo (no para el de las bibliotecas externas)
set(TT_WARNINGS -Wall -Wextra)

# --- Modulos ----------------------------------------------------------------
add_subdirectory(core)
add_subdirectory(cli)
add_subdirectory(mysql)
add_subdirectory(owner)
add_subdirectory(prover)
add_subdirectory(verifier)

enable_testing()
add_subdirectory(tests)
