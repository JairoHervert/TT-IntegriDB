// file.cpp

#include "tt/io/file.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>

namespace tt::io {

namespace {

/*
 * Lanza std::system_error con el errno actual y la descripcion `what`.
 */
[[noreturn]] void throw_errno(const std::string& what) {
    throw std::system_error(errno, std::generic_category(), what);
}

/*
 * Escribe todos los bytes en el descriptor, repitiendo write() si el
 * sistema escribe solo una parte o lo interrumpe una senal.
 */
void write_all(int fd, const char* data, std::size_t size, const std::string& name) {
    while (size > 0) {
        const ssize_t n = ::write(fd, data, size);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw_errno("error al escribir " + name);
        }
        data += n;
        size -= static_cast<std::size_t>(n);
    }
}

/*
 * Escribe `size` bytes en un temporal junto al destino, lo manda al disco
 * (fsync), lo renombra a la ruta final y manda al disco tambien la carpeta,
 * para que el cambio de nombre sobreviva a un corte de energia. El temporal
 * lleva el pid del proceso, asi dos ejecuciones simultaneas no se pisan.
 * Ante cualquier error borra el temporal y relanza.
 */
void write_atomic(const std::filesystem::path& path, const char* data, std::size_t size) {
    std::filesystem::path tmp = path;
    tmp += ".tmp." + std::to_string(::getpid());
    int fd = -1;
    try {
        fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (fd < 0) throw_errno("no se pudo crear " + tmp.string());
        write_all(fd, data, size, tmp.string());
        if (::fsync(fd) != 0) throw_errno("fsync fallo en " + tmp.string());
        const int closed = ::close(fd);
        fd = -1;
        if (closed != 0) throw_errno("error al cerrar " + tmp.string());
        std::filesystem::rename(tmp, path);
    } catch (...) {
        if (fd >= 0) ::close(fd);
        std::error_code ignored;
        std::filesystem::remove(tmp, ignored);
        throw;
    }
    const std::filesystem::path dir = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
    const int dir_fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir_fd >= 0) {  // si no se puede abrir la carpeta, el archivo ya quedo escrito
        ::fsync(dir_fd);
        ::close(dir_fd);
    }
}

}  // namespace

/*
 * Implementacion: abre en modo binario y copia todo con iteradores de flujo.
 */
std::vector<unsigned char> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("no se pudo abrir " + path.string());
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) throw std::runtime_error("error al leer " + path.string());
    return data;
}

/*
 * Implementacion: delega en write_atomic.
 */
void write_file_atomic(const std::filesystem::path& path, const std::string& data) {
    write_atomic(path, data.data(), data.size());
}

/*
 * Implementacion: delega en write_atomic.
 */
void write_file_atomic(const std::filesystem::path& path, const std::vector<unsigned char>& data) {
    write_atomic(path, reinterpret_cast<const char*>(data.data()), data.size());
}

}  // namespace tt::io
