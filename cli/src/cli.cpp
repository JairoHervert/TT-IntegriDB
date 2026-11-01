// cli.cpp

#include "tt/cli/cli.hpp"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <thread>

#include <pthread.h>

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

namespace {

/*
 * Conjunto de senales con las que se detiene un servidor.
 */
sigset_t stop_signals() {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    return set;
}

}  // namespace

/*
 * Implementacion: pthread_sigmask afecta solo a este hilo y a los que cree
 * despues.
 */
void block_stop_signals() {
    const sigset_t signals = stop_signals();
    pthread_sigmask(SIG_BLOCK, &signals, nullptr);
}

/*
 * Implementacion: el vigilante revisa cada 250 ms si run() ya termino, para
 * no quedarse esperando una senal que nunca llegara.
 */
void run_until_signal(const std::function<void()>& run, const std::function<void()>& stop, const std::string& tag) {
    const sigset_t signals = stop_signals();
    std::atomic<bool> finished{false};
    std::thread watcher([&] {
        const timespec wait{0, 250 * 1000 * 1000};
        while (!finished.load()) {
            const int sig = sigtimedwait(&signals, nullptr, &wait);
            if (sig == SIGINT || sig == SIGTERM) {
                std::printf("[%s] senal %s recibida; deteniendo el servidor\n", tag.c_str(),
                            sig == SIGINT ? "SIGINT" : "SIGTERM");
                stop();
                return;
            }
        }
    });
    try {
        run();
    } catch (...) {
        finished = true;
        watcher.join();
        throw;
    }
    finished = true;
    watcher.join();
}

}  // namespace tt::cli
