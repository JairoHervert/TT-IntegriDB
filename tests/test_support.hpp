// test_support.hpp
//
// Utilidades minimas para las pruebas: cada prueba es un ejecutable que
// imprime [OK]/[FALLO] por comprobacion y devuelve 0 solo si todo paso.

#pragma once

#include <cstdio>
#include <exception>
#include <string>

namespace tt::test {

inline int& failure_count() {
    static int count = 0;
    return count;
}

inline void section(const char* title) { std::printf("%s\n", title); }

inline void check(bool ok, const std::string& name) {
    std::printf("  [%s] %s\n", ok ? "OK" : "FALLO", name.c_str());
    if (!ok) ++failure_count();
}

// Pasa si fn() lanza una excepcion del tipo Exception (o derivada).
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

inline int finish() {
    const int failures = failure_count();
    std::printf("%s (%d fallas)\n", failures == 0 ? "TODO BIEN" : "HAY FALLAS", failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace tt::test
