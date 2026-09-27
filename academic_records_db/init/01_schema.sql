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
