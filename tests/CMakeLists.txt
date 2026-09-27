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
