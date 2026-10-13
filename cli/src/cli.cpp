// cli.cpp

#include "tt/cli/cli.hpp"

#include <cstdio>

namespace tt::cli {

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
std::string size_text(std::size_t size) {
    const double bytes = static_cast<double>(size);
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

/*
 * Implementacion: lee el tamano del archivo y usa size_text.
 */
std::string file_size_text(const std::filesystem::path& path) {
    return size_text(static_cast<std::size_t>(std::filesystem::file_size(path)));
}

}  // namespace tt::cli
