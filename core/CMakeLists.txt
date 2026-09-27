# Nucleo compartido: todo lo que es el esquema (primitivas, ASO, AIT,
# Init/Setup/ProofGen/Verify). No debe depender de MySQL ni de la red; esas
# dependencias viven en owner/, prover/ y verifier/.

add_library(tt_core STATIC
  src/crypto/pairing_group.cpp
)

target_include_directories(tt_core PUBLIC include)

target_link_libraries(tt_core PUBLIC
  ate_pairing
  ${NTL_LIB}
  ${GMPXX_LIB}
  ${GMP_LIB}
  Threads::Threads
)

target_compile_options(tt_core PRIVATE ${TT_WARNINGS})
