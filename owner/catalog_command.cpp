// catalog_command.cpp - comando "tt_owner catalog"

#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "cli.hpp"
#include "commands.hpp"
#include "tt/scheme/tree_plan.hpp"

namespace tt::owner {

const char* const kCatalogUsage =
    "  tt_owner catalog\n"
    "      Muestra el catalogo de consultas del caso de estudio, los arboles AIT\n"
    "      que necesita cada una y el plan 'queries' que resulta de juntarlos.\n";

namespace {

/*
 * Imprime un texto largo partido en lineas de a lo mas `width` caracteres,
 * cada una con la sangria indicada (la primera despues de `label`).
 */
void print_wrapped(const std::string& label, const std::string& text, std::size_t width) {
    const std::string indent(label.size(), ' ');
    std::string line;
    bool first = true;

    /*
     * Imprime la linea acumulada con la etiqueta o la sangria que le toca.
     */
    auto flush = [&]() {
        std::printf("%s%s\n", first ? label.c_str() : indent.c_str(), line.c_str());
        line.clear();
        first = false;
    };
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find(' ', start);
        if (end == std::string::npos) end = text.size();
        const std::string word = text.substr(start, end - start);
        if (!line.empty() && line.size() + 1 + word.size() > width) flush();
        line += (line.empty() ? "" : " ") + word;
        start = end + 1;
    }
    if (!line.empty()) flush();
}

}  // namespace

/*
 * Implementacion: recorre el catalogo imprimiendo cada consulta con su SQL,
 * sus arboles y como se verifica; al final imprime la union de los
 * arboles (el plan 'queries') con las consultas que usan cada uno.
 */
int run_catalog(const std::vector<std::string>& args) {
    if (!args.empty()) throw UsageError("catalog no recibe opciones");

    const std::vector<scheme::CatalogQuery>& catalog = scheme::case_study_catalog();
    std::printf("Catalogo de consultas del caso de estudio (%zu consultas)\n", catalog.size());
    std::printf("Fuente: tabla \"Relacion entre el modelo de datos y las consultas consideradas\" del reporte.\n\n");

    std::map<scheme::TreeSpec, std::vector<std::string>> used_by;
    for (const scheme::CatalogQuery& q : catalog) {
        std::printf("%-4s %s - %s\n", q.id.c_str(), q.kind.c_str(), q.description.c_str());
        print_wrapped("     SQL:     ", q.sql, 64);
        std::string trees;
        for (const scheme::TreeSpec& t : q.trees) {
            trees += (trees.empty() ? "" : ", ") + t.label();
            used_by[t].push_back(q.id);
        }
        print_wrapped("     Arboles: ", trees, 64);
        print_wrapped("     Como:    ", q.how, 64);
        std::printf("\n");
    }

    std::printf("Plan 'queries' = union de los arboles anteriores: %zu arboles AIT\n", used_by.size());
    std::map<std::string, std::size_t> per_table;
    for (const auto& [spec, ids] : used_by) {
        std::string list;
        for (const std::string& id : ids) list += (list.empty() ? "" : " ") + id;
        std::printf("  %-48s usado por %s\n", spec.label().c_str(), list.c_str());
        ++per_table[spec.table];
    }
    std::printf("\nArboles por tabla:");
    for (const auto& [table, count] : per_table) std::printf("  %s %zu", table.c_str(), count);
    std::printf("\nEl plan 'full' construye todos los pares de columnas enteras (m^2 por tabla).\n");
    return 0;
}

}  // namespace tt::owner
