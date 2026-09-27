# Pruebas del nucleo. Cada prueba es un ejecutable que devuelve 0 si todo
# sale bien; ctest las corre todas.

add_executable(pairing_group_test pairing_group_test.cpp)
target_link_libraries(pairing_group_test PRIVATE tt_core)
target_compile_options(pairing_group_test PRIVATE ${TT_WARNINGS})
add_test(NAME pairing_group COMMAND pairing_group_test)
