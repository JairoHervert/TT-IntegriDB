// cli.hpp
//
// Utilidades de linea de comandos para los comandos de tt_owner. Viven en
// la biblioteca compartida tt_cli (cli/), que tambien usan tt_prover y
// tt_verifier; aqui solo se traen al espacio de nombres tt::owner.

#pragma once

#include "tt/cli/cli.hpp"

namespace tt::owner {

using cli::Clock;
using cli::file_size_text;
using cli::option_value;
using cli::seconds_since;
using cli::UsageError;

}  // namespace tt::owner
