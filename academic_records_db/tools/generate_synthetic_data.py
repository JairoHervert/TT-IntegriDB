#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
generate_synthetic_data.py
===========================

Genera un archivo .sql con datos sinteticos reproducibles para el esquema
academico del TT 2026-B143 (program, student, course, academic_record).

Incluye 5 carreras, cada una con su propio mapa curricular de 8 semestres.
ISC usa el mapa real del plan 2020 de la ESCOM (50 unidades, 387
creditos); IIA, LCD, ITI e ICS usan planes inventados que solo mantienen
una secuencia logica. Cada alumno avanza semestre a semestre dentro del
plan de su carrera: primero cursa lo pendiente o reprobado de semestres
anteriores y despues lo de su semestre actual, nunca materias de
semestres futuros.

Solo usa la biblioteca estandar de Python, asi que corre igual en la VM
de Ubuntu sin instalar nada extra.

Tamanos predefinidos (numero de alumnos):
    small  ->    500 alumnos   (por defecto)
    medium ->  1,000 alumnos
    large  ->  2,000 alumnos

El numero de calificaciones (academic_record) no se fija directo: sale de
simular la trayectoria de cada alumno.

Ver USAGE.txt (en esta misma carpeta) para los comandos listos para usar.

Nota: los nombres de materias, programas y personas estan en espanol a
proposito, porque representan los datos de una escuela mexicana.
"""

import argparse
import random
import sys
from datetime import datetime

# ---------------------------------------------------------------------
# Catalogos fijos (no dependen del tamano elegido)
# ---------------------------------------------------------------------

# Mapas curriculares: semestre -> [(nombre de la materia, creditos en decimas)]
# ISC sigue el mapa real del plan 2020 de la ESCOM (50 materias, 387 creditos).
# Los otros cuatro planes son inventados: no son reales, solo mantienen una
# secuencia logica (lo basico al inicio, lo especializado y el Trabajo
# Terminal al final).

ISC_CURRICULUM = {
    1: [("Calculo", 75), ("Analisis Vectorial", 75), ("Matematicas Discretas", 105),
        ("Comunicacion Oral y Escrita", 75), ("Fundamentos de Programacion", 75)],
    2: [("Algebra Lineal", 90), ("Calculo Aplicado", 75), ("Mecanica y Electromagnetismo", 105),
        ("Ingenieria, Etica y Sociedad", 90), ("Fundamentos Economicos", 75),
        ("Algoritmos y Estructuras de Datos", 75)],
    3: [("Ecuaciones Diferenciales", 90), ("Circuitos Electricos", 75),
        ("Fundamentos de Diseno Digital", 75), ("Bases de Datos", 75),
        ("Finanzas Empresariales", 75), ("Paradigmas de Programacion", 75),
        ("Analisis y Diseno de Algoritmos", 75)],
    4: [("Probabilidad y Estadistica", 90), ("Matematicas Avanzadas para la Ingenieria", 90),
        ("Electronica Analogica", 75), ("Diseno de Sistemas Digitales", 75),
        ("Tecnologias para el Desarrollo de Aplicaciones Web", 75), ("Sistemas Operativos", 75),
        ("Teoria de la Computacion", 75)],
    5: [("Procesamiento Digital de Senales", 75), ("Instrumentacion y Control", 75),
        ("Arquitectura de Computadoras", 75), ("Analisis y Diseno de Sistemas", 75),
        ("Formulacion y Evaluacion de Proyectos Informaticos", 60), ("Compiladores", 75),
        ("Redes de Computadoras", 75)],
    6: [("Sistemas en Chip", 75), ("Optativa A1", 75), ("Optativa B1", 75),
        ("Metodos Cuantitativos para la Toma de Decisiones", 75), ("Ingenieria de Software", 75),
        ("Inteligencia Artificial", 75), ("Aplicaciones para Comunicaciones en Red", 75)],
    7: [("Desarrollo de Aplicaciones Moviles Nativas", 75), ("Optativa A2", 75),
        ("Optativa B2", 75), ("Trabajo Terminal I", 120), ("Sistemas Distribuidos", 75),
        ("Administracion de Servicios en Red", 75)],
    8: [("Estancia Profesional", 30), ("Desarrollo de Habilidades Sociales para la Alta Direccion", 30),
        ("Trabajo Terminal II", 120), ("Gestion Empresarial", 75), ("Liderazgo Personal", 75)],
}

IIA_CURRICULUM = {
    1: [("Calculo", 75), ("Matematicas Discretas", 105), ("Fundamentos de Programacion", 75),
        ("Comunicacion Oral y Escrita", 75), ("Introduccion a la Inteligencia Artificial", 75)],
    2: [("Algebra Lineal", 90), ("Calculo Multivariable", 75), ("Algoritmos y Estructuras de Datos", 75),
        ("Probabilidad", 90), ("Programacion Orientada a Objetos", 75), ("Etica y Legislacion en IA", 60)],
    3: [("Estadistica Inferencial", 90), ("Optimizacion", 75), ("Bases de Datos", 75),
        ("Analisis y Diseno de Algoritmos", 75), ("Logica y Representacion del Conocimiento", 75),
        ("Fundamentos Economicos", 75)],
    4: [("Aprendizaje Automatico", 90), ("Metodos Numericos", 75), ("Sistemas Operativos", 75),
        ("Teoria de la Computacion", 75), ("Programacion para Ciencia de Datos", 75),
        ("Ingenieria de Software", 75)],
    5: [("Redes Neuronales", 90), ("Vision por Computadora", 75), ("Procesamiento de Lenguaje Natural", 75),
        ("Computo de Alto Desempeno", 75), ("Formulacion y Evaluacion de Proyectos", 60), ("Optativa I", 75)],
    6: [("Aprendizaje Profundo", 90), ("Aprendizaje por Refuerzo", 75), ("Sistemas Multiagente", 75),
        ("Big Data", 75), ("Optativa II", 75), ("Metodologia de la Investigacion", 60)],
    7: [("Trabajo Terminal I", 120), ("Inteligencia Artificial Explicable", 75), ("Robotica Inteligente", 75),
        ("Despliegue de Modelos de IA", 75), ("Optativa III", 75)],
    8: [("Trabajo Terminal II", 120), ("Estancia Profesional", 30), ("Gestion de Proyectos de IA", 75),
        ("Liderazgo Personal", 75), ("Optativa IV", 75)],
}

LCD_CURRICULUM = {
    1: [("Calculo", 75), ("Algebra Superior", 90), ("Fundamentos de Programacion", 75),
        ("Introduccion a la Ciencia de Datos", 75), ("Comunicacion Oral y Escrita", 75)],
    2: [("Calculo Multivariable", 75), ("Algebra Lineal", 90), ("Probabilidad", 90),
        ("Programacion para Ciencia de Datos", 75), ("Bases de Datos", 75), ("Fundamentos Economicos", 75)],
    3: [("Estadistica Inferencial", 90), ("Metodos Numericos", 75), ("Estructuras de Datos", 75),
        ("Bases de Datos Avanzadas", 75), ("Visualizacion de Datos", 75), ("Etica y Privacidad de Datos", 60)],
    4: [("Modelos de Regresion", 90), ("Aprendizaje Automatico", 90), ("Mineria de Datos", 75),
        ("Analisis Multivariado", 75), ("Ingenieria de Datos", 75), ("Finanzas Empresariales", 75)],
    5: [("Series de Tiempo", 75), ("Aprendizaje Profundo", 75), ("Big Data", 75),
        ("Estadistica Bayesiana", 75), ("Formulacion y Evaluacion de Proyectos", 60), ("Optativa I", 75)],
    6: [("Procesamiento de Lenguaje Natural", 75), ("Analitica de Negocios", 75), ("Computo en la Nube", 75),
        ("Diseno de Experimentos", 75), ("Optativa II", 75), ("Metodologia de la Investigacion", 60)],
    7: [("Trabajo Terminal I", 120), ("Gobierno de Datos", 75), ("Sistemas de Recomendacion", 75),
        ("Analitica Web", 75), ("Optativa III", 75)],
    8: [("Trabajo Terminal II", 120), ("Estancia Profesional", 30), ("Gestion de Proyectos de Datos", 75),
        ("Liderazgo Personal", 75), ("Optativa IV", 75)],
}

ITI_CURRICULUM = {
    1: [("Calculo", 75), ("Matematicas Discretas", 105), ("Fundamentos de Programacion", 75),
        ("Introduccion a las Tecnologias de la Informacion", 75), ("Comunicacion Oral y Escrita", 75)],
    2: [("Algebra Lineal", 90), ("Probabilidad y Estadistica", 90), ("Programacion Orientada a Objetos", 75),
        ("Fundamentos de Redes", 75), ("Arquitectura de Computadoras", 75), ("Fundamentos Economicos", 75)],
    3: [("Estructuras de Datos", 75), ("Bases de Datos", 75), ("Sistemas Operativos", 75),
        ("Redes de Area Local", 75), ("Desarrollo Web", 75), ("Contabilidad y Finanzas", 75)],
    4: [("Bases de Datos Avanzadas", 75), ("Ingenieria de Software", 75), ("Redes de Area Amplia", 75),
        ("Administracion de Sistemas Linux", 75), ("Desarrollo de Aplicaciones Web", 75),
        ("Metodos Cuantitativos", 75)],
    5: [("Seguridad de la Informacion", 75), ("Computo en la Nube", 75), ("Virtualizacion", 75),
        ("Gestion de Servicios de TI", 75), ("Formulacion y Evaluacion de Proyectos", 60), ("Optativa I", 75)],
    6: [("Auditoria de TI", 75), ("Desarrollo de Aplicaciones Moviles", 75), ("Integracion de Sistemas", 75),
        ("Internet de las Cosas", 75), ("Optativa II", 75), ("Metodologia de la Investigacion", 60)],
    7: [("Trabajo Terminal I", 120), ("Arquitectura Empresarial", 75), ("DevOps", 75),
        ("Continuidad del Negocio", 75), ("Optativa III", 75)],
    8: [("Trabajo Terminal II", 120), ("Estancia Profesional", 30), ("Gestion Empresarial", 75),
        ("Liderazgo Personal", 75), ("Optativa IV", 75)],
}

ICS_CURRICULUM = {
    1: [("Calculo", 75), ("Matematicas Discretas", 105), ("Fundamentos de Programacion", 75),
        ("Introduccion a la Ciberseguridad", 75), ("Comunicacion Oral y Escrita", 75)],
    2: [("Algebra Lineal", 90), ("Teoria de Numeros", 75), ("Programacion Orientada a Objetos", 75),
        ("Fundamentos de Redes", 75), ("Arquitectura de Computadoras", 75),
        ("Etica y Legislacion Informatica", 60)],
    3: [("Probabilidad y Estadistica", 90), ("Estructuras de Datos", 75), ("Sistemas Operativos", 75),
        ("Protocolos de Red", 75), ("Bases de Datos", 75), ("Fundamentos Economicos", 75)],
    4: [("Criptografia", 90), ("Programacion Segura", 75), ("Administracion de Sistemas", 75),
        ("Seguridad en Redes", 75), ("Teoria de la Computacion", 75), ("Ingenieria de Software", 75)],
    5: [("Criptografia Aplicada", 75), ("Analisis de Malware", 75), ("Seguridad en Aplicaciones Web", 75),
        ("Hacking Etico", 75), ("Formulacion y Evaluacion de Proyectos", 60), ("Optativa I", 75)],
    6: [("Computo Forense", 75), ("Respuesta a Incidentes", 75), ("Seguridad en la Nube", 75),
        ("Gestion de Riesgos", 75), ("Optativa II", 75), ("Metodologia de la Investigacion", 60)],
    7: [("Trabajo Terminal I", 120), ("Auditoria de Seguridad", 75), ("Seguridad en Sistemas Industriales", 75),
        ("Inteligencia de Amenazas", 75), ("Optativa III", 75)],
    8: [("Trabajo Terminal II", 120), ("Estancia Profesional", 30), ("Gobierno de la Seguridad", 75),
        ("Liderazgo Personal", 75), ("Optativa IV", 75)],
}

# (program_id, program_code, program_name, codigo de 2 digitos para la boleta,
#  peso relativo de alumnos, mapa curricular)
# El codigo de boleta solo lo usa este generador para armar un
# enrollment_number con forma de boleta; no es una columna de la tabla.
# ISC tiene mas peso porque suele ser la carrera con mas matricula.
PROGRAMS = [
    (1, "ISC", "Ingenieria en Sistemas Computacionales", 63, 35, ISC_CURRICULUM),
    (2, "IIA", "Ingenieria en Inteligencia Artificial", 64, 20, IIA_CURRICULUM),
    (3, "LCD", "Licenciatura en Ciencia de Datos", 65, 15, LCD_CURRICULUM),
    (4, "ITI", "Ingenieria en Tecnologias de la Informacion", 66, 15, ITI_CURRICULUM),
    (5, "ICS", "Ingenieria en Ciberseguridad", 67, 15, ICS_CURRICULUM),
]


def build_courses():
    """
    Convierte los mapas curriculares en las filas de la tabla course.
    Cada carrera tiene sus propias materias. La clave es codigo de carrera
    + semestre + consecutivo (ISC101 = primera materia del 1er semestre de
    ISC, LCD805 = quinta del 8o de LCD), asi en la clave se lee a que
    carrera y semestre pertenece.
    Regresa (course_id, course_code, course_name, credits_tenths, semestre, program_id).
    """
    courses = []
    course_id = 1
    for program_id, program_code, _, _, _, curriculum in PROGRAMS:
        for semester in sorted(curriculum):
            for index, (name, credits) in enumerate(curriculum[semester], start=1):
                code = f"{program_code}{semester}{index:02d}"
                courses.append((course_id, code, name, credits, semester, program_id))
                course_id += 1
    return courses


COURSES = build_courses()

# Materias agrupadas por carrera, para no filtrar la lista completa cada vez.
COURSES_BY_PROGRAM = {
    p[0]: [c for c in COURSES if c[5] == p[0]] for p in PROGRAMS
}
PLAN_SEMESTERS = {p[0]: max(p[5]) for p in PROGRAMS}

FIRST_NAMES = [
    "Maria", "Jose", "Juan", "Ana", "Luis", "Fernanda", "Carlos", "Sofia",
    "Miguel", "Daniela", "Jorge", "Andrea", "Alejandro", "Paola", "Diego",
    "Valeria", "Ricardo", "Monica", "Francisco", "Gabriela", "Rodrigo",
    "Alejandra", "Eduardo", "Cynthia", "Roberto", "Karla", "Sergio", "Itzel",
    "Adrian", "Perla", "Emilio", "Ximena", "Raul", "Yolanda", "Victor",
    "Guadalupe", "Ivan", "Diana", "Oscar", "Leslie",
]
LAST_NAMES = [
    "Hernandez", "Garcia", "Martinez", "Lopez", "Gonzalez", "Rodriguez",
    "Perez", "Sanchez", "Ramirez", "Torres", "Flores", "Rivera", "Gomez",
    "Diaz", "Cruz", "Morales", "Reyes", "Jimenez", "Ortiz", "Gutierrez",
    "Chavez", "Ramos", "Mendoza", "Ruiz", "Castillo", "Alvarez", "Vazquez",
    "Castro", "Nunez", "Salazar", "Herrera", "Medina", "Aguilar", "Vargas",
    "Contreras", "Guzman", "Rojas", "Fuentes", "Cervantes", "Santiago",
]

# Periodo actual (2026-2). Puede haber alumnos recien admitidos en el,
# pero todavia no tiene calificaciones porque el semestre esta en curso.
CURRENT_TERM = 20262
# Ultimo periodo con calificaciones ya registradas.
LAST_GRADED_TERM = 20261
# El plan 2020 entra en vigor en 2020-1, asi que nadie ingresa antes.
FIRST_ADMISSION_YEAR = 2020
# Plazo maximo para cursar la carrera (duracion del plan + 50%).
MAX_ACTIVE_TERMS = 12
# Probabilidad de que un alumno deje de inscribirse en un periodo dado.
DROPOUT_PROBABILITY = 0.03

# Peso relativo de cada calificacion (sesgado hacia aprobatorias).
GRADE_WEIGHTS = {5: 10, 6: 15, 7: 25, 8: 25, 9: 15, 10: 10}


# ---------------------------------------------------------------------
# Utilidades de periodo (term_code = anio*10 + semestre, semestre en {1,2})
# ---------------------------------------------------------------------

def next_term(term: int) -> int:
    year, semester = divmod(term, 10)
    if semester == 1:
        return year * 10 + 2
    return (year + 1) * 10 + 1


# ---------------------------------------------------------------------
# Generacion de alumnos
# ---------------------------------------------------------------------

class StudentGenerator:
    def __init__(self, rng: random.Random):
        self.rng = rng
        # (anio, codigo_programa) -> siguiente consecutivo de boleta
        self.enrollment_counter = {}

    def _enrollment_number(self, year: int, program_code: int) -> int:
        key = (year, program_code)
        seq = self.enrollment_counter.get(key, 1)
        self.enrollment_counter[key] = seq + 1
        if seq > 9999:
            raise RuntimeError(
                f"Se agotaron los consecutivos de boleta para {key}; "
                "reduce el numero de alumnos."
            )
        # Formato de boleta: anio (4) + codigo de escuela/programa (2) + consecutivo (4)
        return int(f"{year}{program_code:02d}{seq:04d}")

    def generate(self, student_id: int):
        weights = [p[4] for p in PROGRAMS]
        program_id, _, _, enrollment_code, _, _ = self.rng.choices(PROGRAMS, weights=weights, k=1)[0]

        current_year = CURRENT_TERM // 10
        admission_year = self.rng.randint(FIRST_ADMISSION_YEAR, current_year)
        admission_term = admission_year * 10 + self.rng.choice([1, 2])
        if admission_term > CURRENT_TERM:
            admission_term = CURRENT_TERM

        return {
            "student_id": student_id,
            "enrollment_number": self._enrollment_number(admission_year, enrollment_code),
            "first_name": self.rng.choice(FIRST_NAMES),
            "paternal_surname": self.rng.choice(LAST_NAMES),
            "maternal_surname": self.rng.choice(LAST_NAMES),
            "program_id": program_id,
            "admission_term": admission_term,
        }


# ---------------------------------------------------------------------
# Generacion del historial academico (academic_record) de un alumno
# ---------------------------------------------------------------------

def generate_grade(rng: random.Random) -> int:
    values = list(GRADE_WEIGHTS.keys())
    weights = list(GRADE_WEIGHTS.values())
    return rng.choices(values, weights=weights, k=1)[0]


def generate_history(rng: random.Random, student: dict, next_record_id):
    """
    Regresa una lista de tuplas (record_id, student_id, course_id, term_code,
    evaluation_type, grade) para un alumno.

    Reglas de la simulacion:
    - Solo cursa materias del mapa curricular de su carrera.
    - En su k-esimo periodo inscrito, el alumno solo puede cursar materias
      de los semestres 1..k de ese mapa (nunca adelanta materias).
    - Primero toma lo pendiente o reprobado de semestres anteriores y luego
      completa su carga con materias de su semestre actual.
    - Si reprueba la ordinaria, puede presentar extraordinario o ETS en el
      mismo periodo; si tampoco aprueba, la vuelve a cursar despues.
    - Termina cuando aprueba todo el plan, cuando deja de inscribirse, al
      llegar al plazo maximo o al ultimo periodo con calificaciones.

    next_record_id: funcion sin argumentos que regresa el siguiente id
    global disponible, para que record_id no se repita entre alumnos.
    """
    records = []
    passed_courses = set()
    term = student["admission_term"]
    student_id = student["student_id"]
    # Solo se usan las materias de la carrera del alumno.
    program_courses = COURSES_BY_PROGRAM[student["program_id"]]
    plan_semesters = PLAN_SEMESTERS[student["program_id"]]

    # Con baja probabilidad el alumno trae una equivalencia o revalidacion
    # (evaluation_type = 5) de alguna materia de 1er semestre.
    if rng.random() < 0.06 and term <= LAST_GRADED_TERM:
        course = rng.choice([c for c in program_courses if c[4] == 1])
        records.append((next_record_id(), student_id, course[0], term, 5, max(generate_grade(rng), 6)))
        passed_courses.add(course[0])

    # Algunos alumnos llevan menos carga por periodo que otros.
    usual_load = rng.choice([5, 6, 6, 7, 7, 7])

    level = 1
    while term <= LAST_GRADED_TERM and level <= MAX_ACTIVE_TERMS:
        if level > 1 and rng.random() < DROPOUT_PROBABILITY:
            break

        # Materias que ya puede cursar: pendientes de los semestres 1..level,
        # ordenadas para que primero salgan las de semestres anteriores.
        max_semester = min(level, plan_semesters)
        candidates = [c for c in program_courses if c[0] not in passed_courses and c[4] <= max_semester]
        if not candidates and len(passed_courses) == len(program_courses):
            break  # ya aprobo todo el plan
        candidates.sort(key=lambda c: (c[4], rng.random()))
        load = max(3, usual_load + rng.choice([-1, 0, 0, 0]))
        chosen = candidates[:load]

        for course in chosen:
            course_id = course[0]
            grade = generate_grade(rng)
            records.append((next_record_id(), student_id, course_id, term, 1, grade))
            if grade >= 6:
                passed_courses.add(course_id)
            elif rng.random() < 0.70:
                # Reprobo la ordinaria: extraordinario o ETS en el mismo periodo.
                retake_type = rng.choice([2, 2, 2, 3, 4])
                retake_grade = generate_grade(rng)
                # Sesgamos un poco la recuperacion hacia aprobar.
                if retake_grade < 6 and rng.random() < 0.6:
                    retake_grade = rng.randint(6, 9)
                records.append((next_record_id(), student_id, course_id, term, retake_type, retake_grade))
                if retake_grade >= 6:
                    passed_courses.add(course_id)

        term = next_term(term)
        level += 1

    return records


# ---------------------------------------------------------------------
# Emision de SQL
# ---------------------------------------------------------------------

SCHEMA_SQL = """\
-- Esquema generado por generate_synthetic_data.py (TT 2026-B143).
SET NAMES utf8mb4;

DROP TABLE IF EXISTS academic_record;
DROP TABLE IF EXISTS student;
DROP TABLE IF EXISTS course;
DROP TABLE IF EXISTS program;

CREATE TABLE program (
  program_id   INT          NOT NULL,
  program_code VARCHAR(10)  NOT NULL,
  program_name VARCHAR(100) NOT NULL,
  CONSTRAINT pk_program PRIMARY KEY (program_id),
  CONSTRAINT uq_program_code UNIQUE (program_code)
) ENGINE=InnoDB COMMENT='Programa academico (carrera)';

CREATE TABLE student (
  student_id        INT         NOT NULL,
  enrollment_number BIGINT      NOT NULL COMMENT 'Boleta',
  first_name        VARCHAR(60) NOT NULL,
  paternal_surname  VARCHAR(60) NOT NULL,
  maternal_surname  VARCHAR(60) NOT NULL,
  program_id        INT         NOT NULL,
  admission_term    INT         NOT NULL COMMENT 'Periodo de ingreso: anio*10 + semestre; 20261 = 2026-1',
  CONSTRAINT pk_student PRIMARY KEY (student_id),
  CONSTRAINT uq_student_enrollment UNIQUE (enrollment_number),
  CONSTRAINT fk_student_program FOREIGN KEY (program_id) REFERENCES program (program_id),
  CONSTRAINT ck_student_admission_term CHECK (admission_term % 10 IN (1, 2))
) ENGINE=InnoDB COMMENT='Alumno';

CREATE TABLE course (
  course_id      INT          NOT NULL,
  course_code    VARCHAR(10)  NOT NULL,
  course_name    VARCHAR(100) NOT NULL,
  credits_tenths INT          NOT NULL COMMENT 'Creditos en decimas: 75 = 7.5',
  CONSTRAINT pk_course PRIMARY KEY (course_id),
  CONSTRAINT uq_course_code UNIQUE (course_code),
  CONSTRAINT ck_course_credits CHECK (credits_tenths > 0)
) ENGINE=InnoDB COMMENT='Unidad de aprendizaje';

CREATE TABLE academic_record (
  record_id       INT     NOT NULL,
  student_id      INT     NOT NULL,
  course_id       INT     NOT NULL,
  term_code       INT     NOT NULL COMMENT 'Periodo de la evaluacion, mismo formato que admission_term',
  evaluation_type TINYINT NOT NULL COMMENT '1 ordinaria, 2 extraordinaria, 3 ETS, 4 ETS especial, 5 saberes previos/equivalencia',
  grade           TINYINT NOT NULL COMMENT 'Escala registrada 5 a 10; aprobatoria desde 6',
  CONSTRAINT pk_academic_record PRIMARY KEY (record_id),
  CONSTRAINT uq_academic_record UNIQUE (student_id, course_id, term_code, evaluation_type),
  CONSTRAINT fk_record_student FOREIGN KEY (student_id) REFERENCES student (student_id),
  CONSTRAINT fk_record_course  FOREIGN KEY (course_id)  REFERENCES course (course_id),
  CONSTRAINT ck_record_term       CHECK (term_code % 10 IN (1, 2)),
  CONSTRAINT ck_record_eval_type  CHECK (evaluation_type BETWEEN 1 AND 5),
  CONSTRAINT ck_record_grade      CHECK (grade BETWEEN 5 AND 10)
) ENGINE=InnoDB COMMENT='Evaluacion de un alumno en una unidad de aprendizaje, en un periodo y bajo un tipo de evaluacion';
"""


def sql_escape(value) -> str:
    if isinstance(value, str):
        return "'" + value.replace("\\", "\\\\").replace("'", "\\'") + "'"
    return str(value)


def emit_inserts(file, table: str, columns, rows, batch_size: int):
    """Escribe INSERTs de varias filas, en lotes de batch_size."""
    if not rows:
        return
    cols = ", ".join(columns)
    for start in range(0, len(rows), batch_size):
        chunk = rows[start:start + batch_size]
        file.write(f"INSERT INTO {table} ({cols}) VALUES\n")
        lines = []
        for row in chunk:
            values = ", ".join(sql_escape(v) for v in row)
            lines.append(f"  ({values})")
        file.write(",\n".join(lines))
        file.write(";\n")


def main():
    sizes = {"small": 500, "medium": 1000, "large": 2000}

    parser = argparse.ArgumentParser(
        description="Genera datos sinteticos para el esquema academico del TT 2026-B143."
    )
    size_group = parser.add_mutually_exclusive_group()
    size_group.add_argument(
        "--size", choices=sizes.keys(), default="small",
        help="Tamano predefinido (numero de alumnos). Por defecto: small (500).",
    )
    size_group.add_argument(
        "--students", type=int,
        help="Numero exacto de alumnos (ignora --size).",
    )
    parser.add_argument("--seed", type=int, default=42, help="Semilla aleatoria (reproducibilidad). Por defecto: 42.")
    parser.add_argument("--output", type=str, default=None, help="Ruta del archivo .sql de salida.")
    parser.add_argument(
        "--data-only", action="store_true",
        help="No incluye CREATE TABLE; asume que el esquema ya existe y vacia las tablas antes de insertar.",
    )
    parser.add_argument("--batch-size", type=int, default=500, help="Filas por sentencia INSERT. Por defecto: 500.")
    args = parser.parse_args()

    student_count = args.students if args.students else sizes[args.size]
    if student_count <= 0:
        sys.exit("--students debe ser mayor a 0")

    output = args.output or f"data_{args.size if not args.students else student_count}.sql"

    rng = random.Random(args.seed)
    student_generator = StudentGenerator(rng)
    students = [student_generator.generate(i) for i in range(1, student_count + 1)]

    # Contador global para que record_id sea unico en toda la tabla.
    record_counter = {"n": 0}

    def next_record_id():
        record_counter["n"] += 1
        return record_counter["n"]

    records = []
    for student in students:
        records.extend(generate_history(rng, student, next_record_id))

    with open(output, "w", encoding="utf-8") as f:
        f.write(
            f"-- Generado por generate_synthetic_data.py el {datetime.now():%Y-%m-%d %H:%M}\n"
            f"-- Tamano: {args.size if not args.students else 'personalizado'} "
            f"({student_count} alumnos) | semilla={args.seed}\n"
            f"-- Materias: ISC con el mapa real del plan 2020 (ESCOM); IIA, LCD, ITI e ICS con planes inventados\n\n"
        )
        f.write("SET NAMES utf8mb4;\n")
        f.write("SET FOREIGN_KEY_CHECKS = 0;\n\n")

        if not args.data_only:
            f.write(SCHEMA_SQL)
            f.write("\n")
        else:
            f.write("-- (--data-only: se asume que las tablas ya existen)\n")
            f.write("TRUNCATE TABLE academic_record;\n")
            f.write("TRUNCATE TABLE student;\n")
            f.write("TRUNCATE TABLE course;\n")
            f.write("TRUNCATE TABLE program;\n\n")

        emit_inserts(
            f, "program", ["program_id", "program_code", "program_name"],
            [(p[0], p[1], p[2]) for p in PROGRAMS], args.batch_size,
        )
        f.write("\n")
        emit_inserts(
            f, "course", ["course_id", "course_code", "course_name", "credits_tenths"],
            [(c[0], c[1], c[2], c[3]) for c in COURSES], args.batch_size,
        )
        f.write("\n")
        emit_inserts(
            f, "student",
            ["student_id", "enrollment_number", "first_name", "paternal_surname",
             "maternal_surname", "program_id", "admission_term"],
            [(s["student_id"], s["enrollment_number"], s["first_name"], s["paternal_surname"],
              s["maternal_surname"], s["program_id"], s["admission_term"]) for s in students],
            args.batch_size,
        )
        f.write("\n")
        emit_inserts(
            f, "academic_record",
            ["record_id", "student_id", "course_id", "term_code", "evaluation_type", "grade"],
            records, args.batch_size,
        )
        f.write("\nSET FOREIGN_KEY_CHECKS = 1;\n")

    print(f"Listo: {output}")
    print(f"  Alumnos:              {len(students)}")
    print(f"  Registros academicos: {len(records)}")
    print(f"  Programas:            {len(PROGRAMS)}")
    print(f"  Materias:             {len(COURSES)}")


if __name__ == "__main__":
    main()
