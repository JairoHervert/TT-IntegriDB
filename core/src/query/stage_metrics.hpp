#pragma once

// Cronometros de diagnostico para los procedimientos C8, C9 y C14.
// Se activan por peticion, por hilo, sin modificar las pruebas TTRSP.

#include <chrono>
#include <utility>

namespace tt::query::metrics {

// Las categorias registran solo llamadas instrumentadas. El resto del
// procedimiento incluye conversiones, validaciones y trabajo auxiliar.
struct Totals {
    double ait_seconds = 0;
    double merkle_seconds = 0;
    double aso_seconds = 0;
};

enum class Stage { kAit, kMerkle, kAso };

inline thread_local Totals* current = nullptr;

/*
 * Activa una medicion para la peticion actual y restaura el contexto previo
 * al terminar, incluso cuando la verificacion rechaza una respuesta.
 */
class Capture {
public:
    explicit Capture(Totals& totals, bool enabled = true) : previous_(current) {
        if (enabled) current = &totals;
    }
    ~Capture() { current = previous_; }
    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;

private:
    Totals* previous_;
};

/*
 * Mide una operacion concreta, incluso cuando lanza una excepcion.
 * No debe envolver operaciones que ya esten medidas en otra categoria:
 * asi los tiempos AIT, Merkle y ASO no se solapan.
 */
class Timer {
public:
    explicit Timer(Stage stage)
        : destination_(current == nullptr ? nullptr : destination(*current, stage)),
          start_(std::chrono::steady_clock::now()) {}
    ~Timer() {
        if (destination_) {
            *destination_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
        }
    }
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

private:
    static double* destination(Totals& totals, Stage stage) {
        switch (stage) {
            case Stage::kAit: return &totals.ait_seconds;
            case Stage::kMerkle: return &totals.merkle_seconds;
            case Stage::kAso: return &totals.aso_seconds;
        }
        return nullptr;
    }
    double* destination_;
    std::chrono::steady_clock::time_point start_;
};

template <class F>
decltype(auto) measure(Stage stage, F&& operation) {
    if (current == nullptr) return std::forward<F>(operation)();
    Timer timer(stage);
    return std::forward<F>(operation)();
}

}  // namespace tt::query::metrics
