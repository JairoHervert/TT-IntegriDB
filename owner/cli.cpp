// cli.cpp

#include "cli.hpp"

#include <cstdio>

namespace tt::owner {

/*
 * Implementacion: diferencia de relojes convertida a segundos.
 */
double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

/*
 * Implementacion: revisa que exista el siguiente argumento y lo devuelve.
 */
const std::string& option_value(const std::vector<std::string>& args, std::size_t& i) {
    if (i + 1 >= args.size()) throw UsageError(args[i] + " necesita un valor");
    return args[++i];
}

/*
 * Implementacion: elige la unidad segun el tamano y usa un decimal.
 */
std::string file_size_text(const std::filesystem::path& path) {
    const double bytes = static_cast<double>(std::filesystem::file_size(path));
    char text[32];
    if (bytes < 1024) {
        std::snprintf(text, sizeof text, "%.0f B", bytes);
    } else if (bytes < 1024.0 * 1024.0) {
        std::snprintf(text, sizeof text, "%.1f KB", bytes / 1024.0);
    } else {
        std::snprintf(text, sizeof text, "%.1f MB", bytes / (1024.0 * 1024.0));
    }
    return text;
}

}  // namespace tt::owner
