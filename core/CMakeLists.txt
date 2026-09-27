# Nucleo compartido: todo lo que es el esquema (primitivas, ASO, AIT,
# Init/Setup/ProofGen/Verify). No depende de MySQL ni de la red; esas
# dependencias viven en owner/, prover/ y verifier/.

add_library(tt_core STATIC
  src/crypto/pairing_group.cpp
  src/crypto/random.cpp
  src/crypto/hash.cpp
  src/crypto/symmetric.cpp
  src/aso/keys.cpp
  src/aso/accumulator.cpp
  src/scheme/keys.cpp
  src/scheme/key_io.cpp
  src/ait/node.cpp
  src/ait/tree.cpp
  src/ait/range_proof.cpp
)

target_include_directories(tt_core PUBLIC include)

target_link_libraries(tt_core PUBLIC
  ate_pairing
  OpenSSL::Crypto
  ${NTL_LIB}
  ${GMPXX_LIB}
  ${GMP_LIB}
  Threads::Threads
)

target_compile_options(tt_core PRIVATE ${TT_WARNINGS})
