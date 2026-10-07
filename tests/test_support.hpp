// test_support.hpp
//
// Utilidades minimas para las pruebas: cada prueba es un ejecutable que
// imprime [OK]/[FALLO] por comprobacion y devuelve 0 solo si todo paso.

#pragma once

#include <cstdio>
#include <exception>
#include <string>

namespace tt::test {

/*
 * Contador global de comprobaciones fallidas del ejecutable de prueba. Es
 * una variable estatica dentro de la funcion para que el encabezado se
 * pueda incluir sin definir variables globales.
 *
 * Devuelve:
 *   Referencia al contador (check lo incrementa, finish lo lee).
 */
inline int& failure_count() {
    static int count = 0;
    return count;
}

/*
 * Imprime el titulo de un grupo de comprobaciones, para que la salida de la
 * prueba se lea por secciones.
 */
inline void section(const char* title) { std::printf("%s\n", title); }

/*
 * Registra una comprobacion: imprime [OK] o [FALLO] con su nombre y, si
 * fallo, suma uno al contador de fallas. No detiene la prueba, asi un solo
 * error no oculta los demas.
 *
 * Parametros:
 *   ok    resultado de la comprobacion
 *   name  descripcion de lo que se comprueba
 */
inline void check(bool ok, const std::string& name) {
    std::printf("  [%s] %s\n", ok ? "OK" : "FALLO", name.c_str());
    if (!ok) ++failure_count();
}

/*
 * Comprueba que una operacion falle de la forma esperada: pasa si fn()
 * lanza una excepcion del tipo Exception (o de una clase derivada). Falla
 * si no lanza nada o si lanza otro tipo, e indica cual fue el problema.
 *
 * Parametros:
 *   fn    funcion sin argumentos (normalmente una lambda) que debe lanzar
 *   name  descripcion de lo que se comprueba
 */
template <class Exception, class Fn>
void check_throws(Fn&& fn, const std::string& name) {
    std::string problem = "no lanzo ninguna excepcion";
    try {
        fn();
    } catch (const Exception&) {
        check(true, name);
        return;
    } catch (const std::exception& e) {
        problem = std::string("lanzo otro tipo de excepcion: ") + e.what();
    }
    check(false, name + " (" + problem + ")");
}

/*
 * Imprime el resumen final y calcula el codigo de salida del ejecutable.
 * Se llama al final de main: return tt::test::finish();
 *
 * Devuelve:
 *   0 si no hubo fallas (ctest lo toma como prueba aprobada); 1 si hubo
 *   alguna.
 */
inline int finish() {
    const int failures = failure_count();
    std::printf("%s (%d fallas)\n", failures == 0 ? "TODO BIEN" : "HAY FALLAS", failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace tt::test
