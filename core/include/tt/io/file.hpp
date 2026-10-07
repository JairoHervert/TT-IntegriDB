// file.hpp
//
// Lectura y escritura de archivos completos. La escritura es atomica: se
// escribe un temporal, se manda al disco y se renombra, asi quien lea el
// archivo nunca ve una version a medias (aunque el programa se interrumpa o
// se vaya la luz).

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace tt::io {

/*
 * Lee un archivo completo en memoria.
 *
 * Parametros:
 *   path  ruta del archivo
 *
 * Devuelve:
 *   Todos sus bytes.
 *
 * Errores:
 *   Lanza std::runtime_error si no se puede abrir o leer.
 */
std::vector<unsigned char> read_file(const std::filesystem::path& path);

/*
 * Escribe un archivo de forma atomica: primero en un temporal
 * "<ruta>.tmp.<pid>", lo manda al disco (fsync) y despues lo renombra a la
 * ruta final (en Linux el renombrado es atomico). Si algo falla, borra el
 * temporal y deja intacto el archivo anterior.
 *
 * Parametros:
 *   path  ruta final
 *   data  contenido
 *
 * Errores:
 *   Lanza std::system_error o std::filesystem::filesystem_error si no se
 *   puede escribir.
 */
void write_file_atomic(const std::filesystem::path& path, const std::string& data);

/*
 * Igual que la version anterior, con el contenido en bytes.
 */
void write_file_atomic(const std::filesystem::path& path, const std::vector<unsigned char>& data);

}  // namespace tt::io
