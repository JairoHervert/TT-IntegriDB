// case_study_fixture.hpp
//
// Base pequena con el mismo esquema que el caso de estudio
// (academic_records_db/init/01_schema.sql), para probar Setup sin MySQL.
// Los datos son deterministas (generador con semilla fija).

#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "tt/db/table.hpp"

namespace tt::test {

/*
 * Atajo para una celda entera.
 */
inline db::Cell int_cell(std::int64_t v) { return db::Cell{v}; }

/*
 * Atajo para una celda de texto.
 */
inline db::Cell text_cell(const std::string& s) { return db::Cell{s}; }

/*
 * Estructura de las cuatro tablas del caso de estudio, con los mismos
 * nombres, tipos y llaves primarias que 01_schema.sql.
 */
inline std::vector<db::TableSchema> case_study_schemas() {
    using db::ColumnType;
    const ColumnType I = ColumnType::kInteger;
    const ColumnType T = ColumnType::kText;
    return {
        {"academic_record",
         {{"record_id", I}, {"student_id", I}, {"course_id", I}, {"term_code", I}, {"evaluation_type", I}, {"grade", I}},
         0},
        {"course", {{"course_id", I}, {"course_code", T}, {"course_name", T}, {"credits_tenths", I}}, 0},
        {"program", {{"program_id", I}, {"program_code", T}, {"program_name", T}}, 0},
        {"student",
         {{"student_id", I},
          {"enrollment_number", I},
          {"first_name", T},
          {"paternal_surname", T},
          {"maternal_surname", T},
          {"program_id", I},
          {"admission_term", I}},
         0},
    };
}

/*
 * Base de prueba con el esquema del caso de estudio: 3 programas, 8
 * alumnos, 6 unidades de aprendizaje y `records` registros academicos con
 * calificaciones de 5 a 10, periodos de 2024-1 a 2025-2 y formas de
 * evaluacion de 1 a 5. Las filas de academic_record van desordenadas a
 * proposito (Setup debe ordenarlas).
 */
inline db::Database case_study_database(std::size_t records = 40) {
    const std::vector<db::TableSchema> schemas = case_study_schemas();
    db::Database d;
    for (const db::TableSchema& s : schemas) d.tables.push_back(db::Table{s, {}});
    db::Table& ar = d.tables[0];
    db::Table& course = d.tables[1];
    db::Table& program = d.tables[2];
    db::Table& student = d.tables[3];

    const char* codes[] = {"ISC", "IIA", "LCD"};
    const char* names[] = {"Ingenieria en Sistemas Computacionales", "Ingenieria en Inteligencia Artificial",
                           "Licenciatura en Ciencia de Datos"};
    for (int i = 0; i < 3; ++i) program.rows.push_back({int_cell(i + 1), text_cell(codes[i]), text_cell(names[i])});

    const std::int64_t credits[] = {75, 90, 105, 75, 90, 75};
    for (int i = 0; i < 6; ++i) {
        course.rows.push_back({int_cell(i + 1), text_cell("ISC10" + std::to_string(i + 1)),
                               text_cell("Unidad de aprendizaje " + std::to_string(i + 1)), int_cell(credits[i])});
    }

    const char* first[] = {"Ana", "Luis", "Maria", "Jose", "Sofia", "Diego", "Lucia", "Jairo"};
    const std::int64_t admission[] = {20241, 20241, 20242, 20242, 20251, 20251, 20241, 20242};
    for (int i = 0; i < 8; ++i) {
        student.rows.push_back({int_cell(i + 1), int_cell(2024630001 + i), text_cell(first[i]),
                                text_cell("Martinez"), text_cell("Hernandez"), int_cell(i % 3 + 1),
                                int_cell(admission[i])});
    }

    std::mt19937 rng(2026);
    const std::int64_t terms[] = {20241, 20242, 20251, 20252};
    for (std::size_t r = 0; r < records; ++r) {
        ar.rows.push_back({int_cell(static_cast<std::int64_t>(records - r)),  // record_id en orden inverso
                           int_cell(static_cast<std::int64_t>(rng() % 7 + 1)),  // el alumno 8 no tiene registros
                           int_cell(static_cast<std::int64_t>(rng() % 6 + 1)),
                           int_cell(terms[rng() % 4]),
                           int_cell(static_cast<std::int64_t>(rng() % 5 + 1)),
                           int_cell(static_cast<std::int64_t>(rng() % 6 + 5))});
    }
    return d;
}

}  // namespace tt::test
