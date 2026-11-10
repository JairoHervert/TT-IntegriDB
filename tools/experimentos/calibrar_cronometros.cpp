/*
 * calibrar_cronometros.cpp - costo aislado de la instrumentacion de etapas.
 *
 * Comparacion de llamadas al mismo envoltorio con Capture activo e inactivo.
 * Es una microprueba orientativa: no reemplaza medir el algoritmo completo
 * con y sin instrumentacion bajo las mismas condiciones.
 */
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

#include "stage_metrics.hpp"

namespace {

using Clock = std::chrono::steady_clock;
volatile std::uint64_t sink = 0;

/* Mide una repeticion sin efectuar E/S dentro de la region cronometrada. */
double run(bool enabled, int iterations) {
    tt::query::metrics::Totals totals;
    tt::query::metrics::Capture capture(totals, enabled);
    const auto start = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        tt::query::metrics::measure(tt::query::metrics::Stage::kAso, [] {
            ++sink;
        });
    }
    return std::chrono::duration<double, std::nano>(Clock::now() - start).count()
        / static_cast<double>(iterations);
}

/* La mediana evita que una interrupcion aislada defina todo el resultado. */
double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2;
    return values.size() % 2 == 0
        ? (values[middle - 1] + values[middle]) / 2.0 : values[middle];
}

}  // namespace

int main() {
    constexpr int kIterations = 200000;
    std::vector<double> disabled, enabled;
    disabled.reserve(7);
    enabled.reserve(7);
    run(false, kIterations);  // calentamiento
    run(true, kIterations);
    for (int i = 0; i < 7; ++i) {
        if (i % 2 == 0) {
            disabled.push_back(run(false, kIterations));
            enabled.push_back(run(true, kIterations));
        } else {
            enabled.push_back(run(true, kIterations));
            disabled.push_back(run(false, kIterations));
        }
    }
    const double a = median(disabled);
    const double b = median(enabled);
    std::cout << std::fixed << std::setprecision(2)
              << "Intervalo sin Capture activo: " << a << " ns/llamada\n"
              << "Intervalo con Capture activo: " << b << " ns/llamada\n"
              << "Diferencia estimada: " << b - a << " ns/llamada\n"
              << "Nota: esta calibracion no cuantifica el impacto global sobre C8/C14.\n";
    return 0;
}
