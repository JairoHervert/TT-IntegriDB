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
  src/aso/intersection.cpp
  src/aso/sum.cpp
  src/scheme/keys.cpp
  src/scheme/key_io.cpp
  src/ait/node.cpp
  src/ait/tree.cpp
  src/ait/range_proof.cpp
  src/db/table.cpp
  src/db/row_hash.cpp
  src/db/row_store.cpp
  src/merkle/tree.cpp
  src/io/binary.cpp
  src/io/file.cpp
  src/scheme/tree_plan.cpp
  src/scheme/digest.cpp
  src/scheme/authenticated_db.cpp
  src/scheme/setup.cpp
  src/query/query.cpp
  src/query/selection.cpp
  src/query/multirange.cpp
  src/query/minimum.cpp
  src/query/average.cpp
  src/query/join_common.cpp
  src/query/join_program.cpp
  src/query/join_course.cpp
  src/query/count_intersection.cpp
  src/query/response_io.cpp
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
