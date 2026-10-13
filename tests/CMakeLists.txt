# Pruebas. Cada prueba es un ejecutable que devuelve 0 si todo sale bien;
# ctest las corre todas:  ctest --test-dir build --output-on-failure

function(tt_add_test name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE tt_core)
  target_compile_options(${name} PRIVATE ${TT_WARNINGS})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

tt_add_test(pairing_group_test)
tt_add_test(aso_test)
tt_add_test(scheme_keys_test)
tt_add_test(crypto_primitives_test)
tt_add_test(ait_test)
tt_add_test(db_test)
tt_add_test(merkle_test)
tt_add_test(tree_plan_test)
tt_add_test(setup_test)
tt_add_test(state_io_test)
tt_add_test(query_test)
tt_add_test(selection_test)
tt_add_test(mysql_source_test)
target_link_libraries(mysql_source_test PRIVATE tt_mysql)

# Comando del propietario, de punta a punta: genera claves pequenas,
# comprueba su estructura y despues se niega a sobrescribirlas sin --force.
set(OWNER_TEST_DIR ${CMAKE_CURRENT_BINARY_DIR}/owner_init_keys)
add_test(NAME owner_init
         COMMAND tt_owner init --q 32 --out ${OWNER_TEST_DIR} --check --force)
set_tests_properties(owner_init PROPERTIES FIXTURES_SETUP owner_keys)
add_test(NAME owner_init_refuses_overwrite
         COMMAND tt_owner init --q 32 --out ${OWNER_TEST_DIR})
set_tests_properties(owner_init_refuses_overwrite PROPERTIES
                     FIXTURES_REQUIRED owner_keys WILL_FAIL TRUE)

# Catalogo de consultas y un plan invalido (no necesitan MySQL).
add_test(NAME owner_catalog COMMAND tt_owner catalog)
add_test(NAME owner_setup_rejects_unknown_plan COMMAND tt_owner setup --trees consultas)
set_tests_properties(owner_setup_rejects_unknown_plan PROPERTIES WILL_FAIL TRUE)

# Comandos del probador y del verificador: una consulta mal escrita es un
# error de uso, que se informa antes de tocar archivos o MySQL. El
# recorrido completo necesita MySQL y se prueba a mano (ver README).
add_test(NAME prover_rejects_unknown_query COMMAND tt_prover answer C99 1)
set_tests_properties(prover_rejects_unknown_query PROPERTIES PASS_REGULAR_EXPRESSION "consulta desconocida 'C99'")
add_test(NAME verifier_rejects_inverted_range COMMAND tt_verifier verify C4 8 6)
set_tests_properties(verifier_rejects_inverted_range PROPERTIES PASS_REGULAR_EXPRESSION "el rango esta invertido")
