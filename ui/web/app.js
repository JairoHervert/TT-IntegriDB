/*
 * app.js - interfaz del caso de estudio (TT 2026-B143)
 *
 * La pagina habla solo con 'tt_verifier ui' (mismo origen):
 *   GET  /api/status    estado de delta local y del probador
 *   POST /api/preview   SQL y AIT de una consulta, sin enviarla al probador
 *   POST /api/query     veredicto del verificador y resultado verificado
 *   POST /api/direct    SQL directo experimental SIN integridad criptografica
 *
 * Nada se verifica aqui: el veredicto lo decide el verificador en C++ con
 * delta y la clave publica. Esta pagina solo lo muestra. Todos los datos de
 * la base se escriben con textContent (nunca como HTML), porque vienen de un
 * servidor que no es de confianza.
 */
'use strict';

/* ---- Datos fijos del caso de estudio ----------------------------------- */

// Tipos de evaluacion (columna academic_record.evaluation_type).
const EVALUATION_TYPES = {
  1: 'Ordinaria',
  2: 'Extraordinaria',
  3: 'ETS',
  4: 'ETS especial',
  5: 'Saberes previos',
};

// Catalogo de consultas como lo ve el usuario. El texto que se envia es el
// mismo que acepta 'tt_verifier query' (por ejemplo "C4 6 8").
const CATALOG = [
  {
    group: 'Alumnos',
    items: [
      { id: 'C1', title: 'Alumno por boleta', what: 'Datos de un alumno a partir de su número de boleta.',
        params: [{ label: 'Boleta', value: '2020650001' }] },
      { id: 'C6', title: 'Alumnos por periodo de ingreso', what: 'Alumnos que ingresaron entre dos periodos escolares.',
        params: [{ label: 'Desde el periodo', value: '20241', hint: 'año y semestre: 20241 es 2024-1' },
                 { label: 'Hasta el periodo', value: '20242' }] },
      { id: 'C9', title: 'Alumnos con su programa', what: 'Cada alumno junto con su programa académico (JOIN de student y program).',
        params: [] },
    ],
  },
  {
    group: 'Historial académico',
    items: [
      { id: 'C2', title: 'Registros de un alumno', what: 'Todas las evaluaciones registradas de un alumno.',
        params: [{ label: 'Número de alumno', value: '1', hint: 'student_id' }] },
      { id: 'C4', title: 'Registros por calificación', what: 'Evaluaciones con calificación dentro de un intervalo.',
        params: [{ label: 'Calificación mínima', value: '9' }, { label: 'Calificación máxima', value: '10' }] },
      { id: 'C5', title: 'Registros por periodo', what: 'Evaluaciones presentadas entre dos periodos escolares.',
        params: [{ label: 'Desde el periodo', value: '20241', hint: 'año y semestre: 20241 es 2024-1' },
                 { label: 'Hasta el periodo', value: '20242' }] },
      { id: 'C7', title: 'Registros por calificación y periodo', what: 'Evaluaciones que cumplen los dos intervalos a la vez.',
        params: [{ label: 'Calificación mínima', value: '6' }, { label: 'Calificación máxima', value: '8' },
                 { label: 'Desde el periodo', value: '20241' }, { label: 'Hasta el periodo', value: '20242' }],
        note: 'El probador calcula la intersección con el acumulador; suele tardar varios segundos.' },
      { id: 'C8', title: 'Registros con su unidad de aprendizaje', what: 'Cada evaluación junto con la unidad evaluada (JOIN de academic_record y course).',
        params: [], note: 'Devuelve toda la tabla de registros: la respuesta pesa varios megabytes.' },
    ],
  },
  {
    group: 'Unidades de aprendizaje',
    items: [
      { id: 'C3', title: 'Unidad de aprendizaje', what: 'Clave, nombre y créditos de una unidad de aprendizaje.',
        params: [{ label: 'Número de unidad', value: '5', hint: 'course_id' }] },
    ],
  },
  {
    group: 'Indicadores de un alumno',
    items: [
      { id: 'C10', title: 'Promedio', what: 'Promedio de las calificaciones de un alumno.',
        params: [{ label: 'Número de alumno', value: '1', hint: 'student_id' }] },
      { id: 'C11', title: 'Evaluaciones no acreditadas', what: 'Cuántas evaluaciones de un alumno tienen calificación menor a 6.',
        params: [{ label: 'Número de alumno', value: '1', hint: 'student_id' }] },
      { id: 'C12', title: 'Evaluaciones por tipo', what: 'Cuántas evaluaciones de un alumno son de un tipo.',
        params: [{ label: 'Número de alumno', value: '1', hint: 'student_id' },
                 { label: 'Tipo de evaluación', value: '3', options: EVALUATION_TYPES }],
        note: 'Con el tipo ordinaria la intersección es grande y el probador tarda varios segundos.' },
      { id: 'C13', title: 'Calificación mínima', what: 'La calificación más baja de un alumno.',
        params: [{ label: 'Número de alumno', value: '1', hint: 'student_id' }] },
      { id: 'C14', title: 'Créditos aprobados', what: 'Suma de los créditos de las unidades que aprobó un alumno.',
        params: [{ label: 'Número de alumno', value: '1', hint: 'student_id' }],
        note: 'El probador arma la prueba del JOIN y de la suma; suele tardar varios segundos.' },
    ],
  },
  {
    group: 'Consulta libre',
    items: [
      { id: 'range', title: 'Rango en una columna', what: 'Filas cuyo valor en una columna entera está dentro de un intervalo. Solo se ofrecen las columnas que tienen un AIT en δ.',
        params: [{ label: 'Columna', kind: 'column' }, { label: 'Desde', value: '1' }, { label: 'Hasta', value: '3' }] },
    ],
  },
];

// Filas por pagina en las tablas de resultados.
const PAGE_SIZE = 50;

// Textos de cada veredicto.
const OUTCOME_TEXT = {
  verified: 'Verificada',
  rejected: 'Rechazada',
  no_answer: 'Sin respuesta del probador',
  unsupported: 'No soportada por δ',
  invalid: 'Consulta mal escrita',
  ui_down: 'Sin conexión con el verificador',
  error: 'Error de la interfaz',
  pending: 'Verificando…',
};

// Estado de la pagina.
const state = {
  status: null,      // ultima respuesta de /api/status
  generation: 0,     // aumenta en cada expediente; descarta respuestas viejas
  current: null,     // consulta seleccionada en el catalogo
  previewTimer: 0,
  stampCount: 0,     // para ids unicos dentro de los sellos
  measurements: [],  // mediciones de esta sesion; no se envian al probador
  benchmark: { running: false, stopRequested: false, validPairs: [] },
  manualQueryRunning: false,
};

// Columnas estables para importar resultados en Python, R o una hoja de calculo.
const MEASUREMENT_FIELDS = [
  'timestamp_utc', 'query', 'catalog_id', 'outcome', 'state_id', 'state_version',
  'prover_address', 'http_status', 'response_bytes', 'result_row_count',
  'client_roundtrip_seconds', 'server_generation_seconds', 'server_encoding_seconds',
  'server_queue_seconds', 'server_handler_seconds', 'network_overhead_estimated_seconds',
  'verification_seconds', 'client_total_seconds', 'browser_request_seconds', 'verification_stages',
  'execution_mode', 'browser_total_seconds', 'browser_render_seconds',
  'server_row_fetch_seconds', 'server_proofgen_without_fetch_seconds',
  'server_ait_seconds', 'server_merkle_seconds', 'server_aso_seconds', 'server_other_seconds',
  'direct_sql_seconds', 'direct_encoding_seconds', 'direct_queue_seconds',
  'benchmark_id', 'benchmark_phase', 'benchmark_pair', 'benchmark_order',
  'benchmark_environment', 'benchmark_pair_valid', 'benchmark_browser_visible',
  'benchmark_state_id', 'benchmark_state_version', 'benchmark_plan',
  'benchmark_tables', 'benchmark_prover_address', 'benchmark_browser_user_agent',
];

/* ---- Utilidades -------------------------------------------------------- */

/*
 * Atajo para document.getElementById.
 */
function $(id) {
  return document.getElementById(id);
}

/*
 * Crea un elemento con clase, texto y atributos opcionales.
 *   props.className  clase
 *   props.text       texto (con textContent)
 *   props.attrs      atributos adicionales
 * Los hijos pueden ser nodos o textos.
 */
function el(tag, props = {}, ...children) {
  const node = document.createElement(tag);
  if (props.className) node.className = props.className;
  if (props.text !== undefined) node.textContent = props.text;
  for (const [name, value] of Object.entries(props.attrs || {})) node.setAttribute(name, value);
  for (const child of children) {
    if (child === null || child === undefined) continue;
    node.append(child instanceof Node ? child : document.createTextNode(String(child)));
  }
  return node;
}

/*
 * Quita todos los hijos de un nodo.
 */
function clear(node) {
  while (node.firstChild) node.removeChild(node.firstChild);
}

/*
 * Tiempo legible: milisegundos debajo de un segundo.
 */
function formatSeconds(seconds) {
  if (typeof seconds !== 'number' || !Number.isFinite(seconds)) return 'No disponible';
  if (seconds < 0.001) return 'menos de 1 ms';
  if (seconds < 1) return `${Math.round(seconds * 1000)} ms`;
  return `${seconds.toFixed(2)} s`;
}

/*
 * Tamano legible en B, KB o MB (las mismas unidades que la terminal).
 */
function formatBytes(bytes) {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

/*
 * Periodo escolar: 20212 se muestra como 2021-2.
 */
function formatTerm(code) {
  if (typeof code !== 'number') return String(code);
  return `${Math.floor(code / 10)}-${code % 10}`;
}

/*
 * Creditos guardados en decimas: 75 se muestra como 7.5.
 */
function formatCredits(tenths) {
  return (tenths / 10).toFixed(1);
}

/*
 * Numero entero con separador de miles.
 */
function formatCount(n) {
  return Number(n).toLocaleString('es-MX');
}

/*
 * Primeros 8 digitos del id de delta, como se muestran en la pagina.
 */
function shortId(hex) {
  return (hex || '').slice(0, 8);
}

/*
 * Convierte una fila del resultado en objeto {columna: valor}.
 */
function rowObject(result, index) {
  const row = {};
  result.columns.forEach((name, i) => { row[name] = result.rows[index][i]; });
  return row;
}

/*
 * El veredicto que se usa para los estilos (los errores de la interfaz se
 * pintan como "sin respuesta").
 */
function styleOutcome(outcome) {
  if (outcome === 'ui_down' || outcome === 'error') return 'no_answer';
  return outcome;
}

/*
 * Peor veredicto de una lista: rechazada, luego cualquier falta de
 * respuesta, luego verificada.
 */
function worstOutcome(outcomes) {
  if (outcomes.includes('rejected')) return 'rejected';
  if (outcomes.some((o) => o !== 'verified')) return 'no_answer';
  return 'verified';
}

/* ---- Registro local de mediciones -------------------------------------- */

/*
 * Actualiza los controles de exportacion. Las mediciones se conservan solo
 * en memoria y se pierden al recargar la pagina.
 */
function updateMeasurementsToolbar() {
  const count = state.measurements.length;
  $('measurements-count').textContent = `${count} ${count === 1 ? 'consulta registrada' : 'consultas registradas'}`;
  $('measurements-export').disabled = count === 0 || state.benchmark.running;
  $('measurements-clear').disabled = count === 0 || state.benchmark.running;
}

/*
 * Conserva los tiempos y el veredicto, no las filas devueltas por Verify.
 * Se limita a 1000 ejecuciones para no acumular datos sin limite en memoria.
 */
function saveMeasurement(query, reply, browserSeconds, experiment = {}) {
  const prover = reply.prover || {};
  const direct = reply.direct || {};
  const times = prover.timings || {};
  const verification = reply.verification || {};
  const row = {
    timestamp_utc: new Date().toISOString(),
    query,
    catalog_id: reply.catalog_id || '',
    outcome: reply.outcome || 'error',
    state_id: reply.state ? reply.state.id : '',
    state_version: reply.state ? reply.state.version : '',
    prover_address: prover.address || (state.status && state.status.prover ? state.status.prover.address : ''),
    http_status: prover.status === undefined ? (reply.outcome === 'direct' ? 200 : '') : prover.status,
    response_bytes: prover.bytes === undefined ? direct.bytes : prover.bytes,
    result_row_count: (reply.outcome === 'verified' || reply.outcome === 'direct') && reply.result
      ? reply.result.row_count : '',
    client_roundtrip_seconds: reply.outcome === 'direct' ? direct.roundtrip_seconds : prover.seconds,
    server_generation_seconds: times.generation_seconds,
    server_encoding_seconds: times.encoding_seconds,
    server_queue_seconds: reply.outcome === 'direct' ? direct.queue_seconds : times.queue_seconds,
    server_handler_seconds: reply.outcome === 'direct' ? direct.handler_seconds : times.handler_seconds,
    network_overhead_estimated_seconds: reply.outcome === 'direct' ? direct.network_overhead_seconds
      : prover.network_overhead_seconds,
    verification_seconds: verification.seconds,
    client_total_seconds: reply.client_total_seconds,
    browser_request_seconds: browserSeconds,
    verification_stages: verification.stages ? JSON.stringify(verification.stages) : '',
    execution_mode: reply.execution_mode || (reply.outcome === 'direct' ? 'sql_direct' : 'verified_path'),
    browser_total_seconds: reply.browser_total_seconds,
    browser_render_seconds: reply.browser_render_seconds,
    server_row_fetch_seconds: times.row_fetch_seconds,
    server_ait_seconds: times.stages ? times.stages.ait_seconds : null,
    server_merkle_seconds: times.stages ? times.stages.merkle_seconds : null,
    server_aso_seconds: times.stages ? times.stages.aso_seconds : null,
    server_other_seconds: times.stages ? times.stages.other_seconds : null,
    server_proofgen_without_fetch_seconds: typeof times.generation_seconds === 'number' &&
      typeof times.row_fetch_seconds === 'number'
      ? Math.max(0, times.generation_seconds - times.row_fetch_seconds) : null,
    direct_sql_seconds: reply.direct ? reply.direct.sql_seconds : null,
    direct_encoding_seconds: reply.direct ? reply.direct.encoding_seconds : null,
    direct_queue_seconds: reply.direct ? reply.direct.queue_seconds : null,
    benchmark_id: experiment.id || '',
    benchmark_phase: experiment.phase || '',
    benchmark_pair: experiment.pair || '',
    benchmark_order: experiment.order || '',
    benchmark_environment: experiment.environment || '',
    benchmark_pair_valid: experiment.valid === undefined ? '' : experiment.valid,
    benchmark_browser_visible: experiment.visible === undefined ? '' : experiment.visible,
    benchmark_state_id: experiment.snapshot ? experiment.snapshot.stateId : '',
    benchmark_state_version: experiment.snapshot ? experiment.snapshot.stateVersion : '',
    benchmark_plan: experiment.snapshot ? experiment.snapshot.plan : '',
    benchmark_tables: experiment.snapshot ? experiment.snapshot.tables : '',
    benchmark_prover_address: experiment.snapshot ? experiment.snapshot.proverAddress : '',
    benchmark_browser_user_agent: experiment.snapshot ? experiment.snapshot.browserUserAgent : '',
  };
  state.measurements.push(row);
  if (state.measurements.length > 1000) state.measurements.shift();
  updateMeasurementsToolbar();
  return row;
}

/*
 * Escapa un valor para CSV, incluidas comillas y posibles formulas al abrir
 * el archivo con una hoja de calculo. Los decimales usan punto.
 */
function csvValue(value) {
  if (value === null || value === undefined || value === '') return '';
  if (typeof value === 'number') return Number.isFinite(value) ? String(value) : '';
  let text = String(value);
  if (/^[=+@-]/.test(text) || /^[\t\r]/.test(text)) text = `'${text}`;
  return `"${text.replace(/"/g, '""')}"`;
}

/*
 * Descarga el registro local con encabezados estables y UTF-8 compatible
 * con Excel. No se incluye ningun resultado SQL en el CSV.
 */
function exportMeasurements() {
  if (!state.measurements.length) return;
  const rows = [MEASUREMENT_FIELDS.join(',')];
  for (const entry of state.measurements) {
    rows.push(MEASUREMENT_FIELDS.map((field) => csvValue(entry[field])).join(','));
  }
  const csv = `\uFEFF${rows.join('\r\n')}\r\n`;
  const url = URL.createObjectURL(new Blob([csv], { type: 'text/csv;charset=utf-8' }));
  const link = document.createElement('a');
  link.href = url;
  link.download = `tt_metricas_${new Date().toISOString().replace(/[:.]/g, '-')}.csv`;
  document.body.append(link);
  link.click();
  link.remove();
  setTimeout(() => URL.revokeObjectURL(url), 0);
}

/* ---- Llamadas al verificador ------------------------------------------- */

/*
 * Llama a la interfaz del verificador. Con cuerpo es POST (texto de la
 * consulta) y lleva la cabecera X-TT-UI que exige el servidor. Siempre
 * devuelve un objeto con "outcome".
 */
async function callApi(path, body, deferMeasurement = false) {
  const options = body === undefined
    ? { method: 'GET', cache: 'no-store' }
    : { method: 'POST', headers: { 'X-TT-UI': '1', 'Content-Type': 'text/plain' }, body };
  const start = performance.now();
  let reply;
  let response;
  try {
    response = await fetch(path, options);
  } catch (e) {
    reply = { outcome: 'ui_down', message: 'No hay conexión con tt_verifier ui; revisa que siga en ejecución.' };
  }
  if (response) {
    try {
      reply = await response.json();
      if (!reply || typeof reply !== 'object') throw new Error('JSON inesperado');
    } catch (e) {
      reply = { outcome: 'error', message: `La interfaz respondió algo inesperado (código ${response.status}).` };
    }
  }
  // Solo la ruta directa incluye tiempos en cabeceras locales del
  // verificador. Los anunciados por el servidor NO son autenticados.
  if (path === '/api/direct' && response && response.ok && reply && reply.outcome === 'direct' &&
      reply.unverified === true && reply.result && Array.isArray(reply.result.columns) &&
      Array.isArray(reply.result.rows)) {
    const metric = (header) => {
      const text = response.headers.get(header);
      if (text === null || !/^\d+(?:\.\d+)?$/.test(text)) return null;
      const value = Number(text);
      return Number.isFinite(value) && value >= 0 ? value : null;
    };
    reply.catalog_id = body.split(' ')[0];
    reply.direct = {
      roundtrip_seconds: metric('X-TT-Direct-Roundtrip-Seconds'),
      sql_seconds: metric('X-TT-Direct-Sql-Seconds'),
      encoding_seconds: metric('X-TT-Direct-Encoding-Seconds'),
      queue_seconds: metric('X-TT-Direct-Queue-Seconds'),
      handler_seconds: metric('X-TT-Direct-Handler-Seconds'),
      bytes: metric('X-TT-Direct-Bytes'),
      client_seconds: metric('X-TT-Direct-Client-Seconds'),
    };
    const direct = reply.direct;
    direct.network_overhead_seconds = direct.roundtrip_seconds !== null && direct.handler_seconds !== null &&
      direct.roundtrip_seconds >= direct.handler_seconds
      ? direct.roundtrip_seconds - direct.handler_seconds : null;
  } else if (path === '/api/direct' && response && response.ok) {
    reply = { outcome: 'error', message: 'El servidor devolvió un resultado SQL con formato inesperado.' };
  }
  // Tiempo de lectura del JSON: excluye la actualizacion y el pintado de DOM.
  if (path === '/api/query' || path === '/api/direct') {
    reply.execution_mode = path === '/api/direct' ? 'sql_direct' : 'verified_path';
    reply.browser_request_seconds = (performance.now() - start) / 1000;
    if (!deferMeasurement) saveMeasurement(body, reply, reply.browser_request_seconds);
  }
  return reply;
}

/* ---- Estado del probador ----------------------------------------------- */

/*
 * Pide /api/status y actualiza el aviso de la banda superior y el pie.
 */
async function refreshStatus() {
  if (state.benchmark.running) return;
  const status = await callApi('/api/status');
  const box = $('prover-status');
  const title = $('prover-status-title');
  const detail = $('prover-status-detail');
  if (!status.client) {
    box.dataset.state = 'down';
    title.textContent = 'Sin conexión con el verificador';
    detail.textContent = status.message || '';
    return;
  }
  const firstLoad = state.status === null;
  state.status = status;
  $('query-direct').disabled = state.benchmark.running || !status.client.direct_benchmark_enabled ||
    !state.current || state.current.id === 'range';
  const p = status.prover;
  if (!p.reachable) {
    box.dataset.state = 'down';
    title.textContent = `El probador en ${p.address} no responde`;
    detail.textContent = 'Las consultas no tendrán respuesta hasta que vuelva.';
  } else if (!p.matches) {
    box.dataset.state = 'mismatch';
    title.textContent = `El probador en ${p.address} usa otro estado`;
    detail.textContent = `Su δ es ${shortId(p.state_id)} y el tuyo ${shortId(status.client.state_id)}: sus respuestas se rechazarán.`;
  } else {
    box.dataset.state = 'ok';
    title.textContent = `Probador en ${p.address}`;
    detail.textContent = `Responde con el mismo δ que tienes (${shortId(p.state_id)}).`;
  }
  const c = status.client;
  const rows = c.tables.map((t) => `${t.name} ${formatCount(t.rows)}`).join(', ');
  $('local-state').textContent = `Tu δ: ${shortId(c.state_id)}, versión ${c.state_version}, plan ${c.plan}. Filas autenticadas: ${rows}.`;
  if (firstLoad && state.current && state.current.id === 'range') selectQuery('range');
}

/* ---- Sello del verificador --------------------------------------------- */

/*
 * Dibuja el sello del veredicto (SVG). El texto superior recorre el anillo
 * y el inferior muestra el id de delta contra el que se verifico.
 *   outcome  verified, rejected, no_answer o pending
 *   top      texto del anillo superior
 *   idHex    id de delta (64 digitos)
 *   press    true para animar la llegada del veredicto
 */
function makeStamp(outcome, top, idHex, press) {
  const ns = 'http://www.w3.org/2000/svg';
  const n = ++state.stampCount;
  const svgEl = (tag, attrs) => {
    const node = document.createElementNS(ns, tag);
    for (const [k, v] of Object.entries(attrs)) node.setAttribute(k, v);
    return node;
  };
  const svg = svgEl('svg', { viewBox: '0 0 120 120', 'aria-hidden': 'true' });
  const defs = svgEl('defs', {});
  defs.append(
    svgEl('path', { id: `arc-top-${n}`, d: 'M 17,60 a 43,43 0 1,1 86,0' }),
    svgEl('path', { id: `arc-bottom-${n}`, d: 'M 12,60 a 48,48 0 0,0 96,0' }),
  );
  svg.append(defs);
  svg.append(svgEl('circle', { class: 'ring-outer', cx: 60, cy: 60, r: 57, fill: 'none', stroke: 'currentColor', 'stroke-width': 3 }));
  svg.append(svgEl('circle', { cx: 60, cy: 60, r: 52, fill: 'none', stroke: 'currentColor', 'stroke-width': 1 }));
  svg.append(svgEl('circle', { cx: 60, cy: 60, r: 31, fill: 'none', stroke: 'currentColor', 'stroke-width': 1.2 }));

  const topText = svgEl('text', {});
  const topPath = svgEl('textPath', { href: `#arc-top-${n}`, startOffset: '50%', 'text-anchor': 'middle' });
  topPath.textContent = top;
  topText.append(topPath);
  svg.append(topText);

  const bottomText = svgEl('text', { class: 'stamp-id', dy: '3' });
  const bottomPath = svgEl('textPath', { href: `#arc-bottom-${n}`, startOffset: '50%', 'text-anchor': 'middle' });
  bottomPath.textContent = idHex ? `δ ${shortId(idHex)}` : 'δ';
  bottomText.append(bottomPath);
  svg.append(bottomText);

  const mark = { fill: 'none', stroke: 'currentColor', 'stroke-width': 6, 'stroke-linecap': 'round', 'stroke-linejoin': 'round' };
  if (outcome === 'verified') {
    svg.append(svgEl('path', { d: 'M45 61 L56 72 L76 49', ...mark }));
  } else if (outcome === 'rejected') {
    svg.append(svgEl('path', { d: 'M48 48 L72 72 M72 48 L48 72', ...mark }));
  } else if (outcome === 'no_answer') {
    svg.append(svgEl('path', { d: 'M60 45 L60 64', ...mark }));
    svg.append(svgEl('circle', { cx: 60, cy: 75, r: 3.6, fill: 'currentColor' }));
  } else {
    for (const x of [48, 60, 72]) svg.append(svgEl('circle', { cx: x, cy: 60, r: 3.2, fill: 'currentColor' }));
  }

  const stamp = el('div', { className: press ? 'stamp pressed' : 'stamp', attrs: { 'data-outcome': outcome } });
  stamp.append(svg);
  return stamp;
}

/* ---- Pestanas ---------------------------------------------------------- */

/*
 * Muestra una de las dos vistas y la recuerda en la direccion (#catalogo).
 */
function showView(name, updateHash = true) {
  if (state.benchmark.running && name !== 'catalog') return;
  for (const tab of document.querySelectorAll('.tab')) {
    tab.setAttribute('aria-selected', String(tab.dataset.view === name));
  }
  $('view-record').hidden = name !== 'record';
  $('view-catalog').hidden = name !== 'catalog';
  if (updateHash) {
    const hash = name === 'catalog' ? `#catalogo/${state.current ? state.current.id : 'C1'}` : '#expediente';
    history.replaceState(null, '', hash);
  }
}

/* ---- Vista del expediente ---------------------------------------------- */

// Indicadores del expediente. "order" es el orden en que se piden: primero
// los rapidos y al final C14, que es el que mas tarda el probador.
const INDICATORS = [
  { key: 'avg', id: 'C10', label: 'Promedio general', order: 1, query: (s) => `C10 ${s}`,
    show: (v) => (v === null ? null : v.toFixed(2)), empty: 'Sin registros' },
  { key: 'credits', id: 'C14', label: 'Créditos aprobados', order: 6, query: (s) => `C14 ${s}`,
    show: (v) => (v === null ? null : formatCredits(v)), empty: 'Ninguno' },
  { key: 'min', id: 'C13', label: 'Calificación mínima', order: 3, query: (s) => `C13 ${s}`,
    show: (v) => (v === null ? null : String(v)), empty: 'Sin registros' },
  { key: 'failed', id: 'C11', label: 'Evaluaciones no acreditadas', order: 2, query: (s) => `C11 ${s}`,
    show: (v) => String(v), empty: '' },
  { key: 'extra', id: 'C12', label: 'Evaluaciones extraordinarias', order: 4, query: (s) => `C12 ${s} 2`,
    show: (v) => String(v), empty: '' },
  { key: 'ets', id: 'C12', label: 'Evaluaciones a título de suficiencia', order: 5, query: (s) => `C12 ${s} 3`,
    show: (v) => String(v), empty: '' },
];

/*
 * Escribe en un parrafo .part-status el veredicto de una o varias consultas:
 * la marca (Verificada, Rechazada...), los ids del catalogo y el tiempo.
 */
function setPartStatus(node, replies, label) {
  clear(node);
  if (!replies.length) return;
  const pending = replies.some((r) => r.outcome === 'pending');
  const outcome = pending ? 'pending' : worstOutcome(replies.map((r) => r.outcome));
  node.dataset.outcome = outcome;
  if (pending) {
    node.append(el('span', { className: 'spinner', attrs: { 'aria-hidden': 'true' } }), el('span', { text: 'Verificando…' }));
    return;
  }
  const word = outcome === 'verified' ? 'Verificada' : outcome === 'rejected' ? 'Rechazada' : 'Sin verificar';
  node.append(el('span', { className: 'mark', text: word }));
  if (label) node.append(el('span', { text: label }));
  const seconds = replies.reduce((t, r) => t + (r.prover ? r.prover.seconds : 0), 0);
  if (seconds > 0) node.append(el('span', { text: `en ${formatSeconds(seconds)}` }));
}

/*
 * Agrega a la bitacora una consulta en curso y devuelve una funcion que la
 * completa con la respuesta del verificador.
 */
function addLedgerEntry(text, title) {
  const li = el('li', { attrs: { 'data-outcome': 'pending' } });
  const id = text.split(' ')[0];
  li.append(el('div', { className: 'ledger-title' }, el('span', { className: 'query-id', text: id === 'range' ? 'rango' : id }),
    el('strong', { text: title })));
  const outcomeLine = el('p', { className: 'ledger-outcome' }, el('span', { className: 'spinner', attrs: { 'aria-hidden': 'true' } }), ' Esperando al probador…');
  li.append(outcomeLine);
  $('ledger-list').append(li);
  $('ledger-note').hidden = true;

  return (reply) => {
    li.dataset.outcome = styleOutcome(reply.outcome);
    clear(outcomeLine);
    let line = OUTCOME_TEXT[reply.outcome] || reply.outcome;
    if (reply.outcome === 'verified' && reply.result) {
      const n = reply.result.row_count;
      const scalar = n === 1 && reply.result.columns.length === 1 && /\(/.test(reply.result.columns[0]);
      if (!scalar) line += `: ${formatCount(n)} ${n === 1 ? 'fila' : 'filas'}`;
    }
    outcomeLine.textContent = line;
    if (reply.prover) {
      const verifier = reply.verification ? `verificación ${formatSeconds(reply.verification.seconds)}` : '';
      const timing = reply.prover.timings;
      const detail = timing
        ? `Generación ${formatSeconds(timing.generation_seconds)}, red/HTTP estimada ${formatSeconds(reply.prover.network_overhead_seconds)}`
        : `Obtención HTTP ${formatSeconds(reply.prover.seconds)}`;
      li.append(el('p', { className: 'ledger-times',
        text: `${detail} (${formatBytes(reply.prover.bytes)}), ${verifier || 'sin verificación'}` }));
    }
    if (reply.outcome !== 'verified' && reply.message) li.append(el('p', { className: 'ledger-reason', text: reply.message }));
    const details = el('details', {}, el('summary', { text: 'Consulta y pruebas' }));
    if (reply.sql) details.append(el('code', { className: 'sql', text: reply.sql }));
    if (reply.trees && reply.trees.length) details.append(el('p', { text: `AIT: ${reply.trees.join(', ')}` }));
    if (reply.detail) details.append(el('p', { text: `Verificador: ${reply.detail}` }));
    li.append(details);
  };
}

/*
 * Envia una consulta del expediente y la registra en la bitacora.
 */
async function runLogged(text, title) {
  const finish = addLedgerEntry(text, title);
  const reply = await callApi('/api/query', text);
  finish(reply);
  return reply;
}

/*
 * Prepara las celdas de los indicadores en estado "verificando".
 */
function resetIndicators() {
  const box = $('indicators');
  clear(box);
  for (const ind of INDICATORS) {
    const cell = el('div', { className: 'indicator', attrs: { id: `ind-${ind.key}` } },
      el('div', { className: 'value pending', text: '…' }),
      el('div', { className: 'label', text: ind.label }),
      el('p', { className: 'part-status' }));
    setPartStatus(cell.querySelector('.part-status'), [{ outcome: 'pending' }]);
    box.append(cell);
  }
}

/*
 * Llena la celda de un indicador con su respuesta verificada (o el motivo
 * por el que no hay valor).
 */
function fillIndicator(ind, reply) {
  const cell = $(`ind-${ind.key}`);
  const value = cell.querySelector('.value');
  value.className = 'value';
  if (reply.outcome === 'verified' && reply.result && reply.result.rows.length) {
    const shown = ind.show(reply.result.rows[0][0]);
    if (shown === null) {
      value.classList.add('void');
      value.textContent = ind.empty;
    } else {
      value.textContent = shown;
    }
  } else {
    value.classList.add('void');
    value.textContent = reply.outcome === 'rejected' ? 'No se muestra' : 'Sin dato';
  }
  setPartStatus(cell.querySelector('.part-status'), [reply], ind.id);
}

/*
 * Marca las partes que no se consultaron porque fallo un paso anterior.
 */
function skipRest(message) {
  for (const ind of INDICATORS) {
    const cell = $(`ind-${ind.key}`);
    if (!cell) continue;
    const value = cell.querySelector('.value');
    if (!value.classList.contains('pending')) continue;
    value.className = 'value void';
    value.textContent = 'No consultado';
    const status = cell.querySelector('.part-status');
    clear(status);
    status.dataset.outcome = '';
  }
  const kardexStatus = $('kardex-status');
  if (kardexStatus.dataset.outcome === 'pending') {
    clear(kardexStatus);
    kardexStatus.dataset.outcome = '';
  }
  const body = $('kardex').querySelector('tbody');
  if (!body.children.length) body.append(el('tr', {}, el('td', { className: 'table-empty', text: message, attrs: { colspan: '6' } })));
}

/*
 * Pone el sello general del expediente.
 */
function setRecordStamp(outcome, press) {
  const slot = $('record-stamp');
  clear(slot);
  const top = {
    pending: 'Verificando expediente',
    verified: 'Expediente verificado',
    rejected: 'Expediente con rechazos',
    no_answer: 'Expediente incompleto',
  }[outcome];
  const id = state.status ? state.status.client.state_id : '';
  slot.append(makeStamp(outcome, top, id, press));
  slot.setAttribute('aria-label', top);
}

/*
 * Llena el historial academico con las filas verificadas de C2 y, si
 * llegaron, los nombres y creditos verificados de las unidades.
 */
function renderKardex(records, courses) {
  const body = $('kardex').querySelector('tbody');
  clear(body);
  const byId = new Map();
  if (courses && courses.outcome === 'verified') {
    courses.result.rows.forEach((_, i) => {
      const c = rowObject(courses.result, i);
      byId.set(c.course_id, c);
    });
  }
  const rows = records.result.rows.map((_, i) => rowObject(records.result, i));
  rows.sort((a, b) => a.term_code - b.term_code || a.course_id - b.course_id || a.evaluation_type - b.evaluation_type);
  if (!rows.length) {
    body.append(el('tr', {}, el('td', { className: 'table-empty', text: 'El alumno no tiene evaluaciones registradas. Que la lista esté vacía también se verificó.', attrs: { colspan: '6' } })));
    return;
  }
  let lastTerm = null;
  for (const r of rows) {
    const course = byId.get(r.course_id);
    const tr = el('tr', { className: r.grade < 6 ? 'failed' : '' });
    if (r.term_code !== lastTerm) tr.classList.add('term-start');
    lastTerm = r.term_code;
    tr.append(
      el('td', { className: 'term', text: formatTerm(r.term_code) }),
      el('td', { text: course ? course.course_code : `#${r.course_id}` }),
      el('td', { className: 'wrap', text: course ? course.course_name : 'Unidad sin verificar' }),
      el('td', { className: 'num', text: course ? formatCredits(course.credits_tenths) : '' }),
      el('td', { text: EVALUATION_TYPES[r.evaluation_type] || String(r.evaluation_type) }),
      el('td', { className: 'num grade', text: String(r.grade) }),
    );
    body.append(tr);
  }
}

/*
 * Arma el expediente de un alumno. Cada parte es una consulta distinta, en
 * este orden: C1 (ficha), rango sobre program (nombre del programa), C2
 * (historial), rango sobre course (nombres de las unidades) y los
 * indicadores C10, C11, C13, C12 y C14. Si el usuario pide otro expediente
 * a la mitad, las respuestas del anterior se descartan.
 */
async function loadRecord(enrollment) {
  const generation = ++state.generation;
  const stale = () => generation !== state.generation;
  const outcomes = [];

  $('record-empty').hidden = true;
  $('record-sheet').hidden = false;
  $('indicators-section').hidden = false;
  $('kardex-section').hidden = false;
  clear($('ledger-list'));
  $('student-name').textContent = 'Consultando…';
  for (const id of ['student-enrollment', 'student-program', 'student-admission']) $(id).textContent = '—';
  setPartStatus($('identity-status'), [{ outcome: 'pending' }]);
  setRecordStamp('pending', false);
  resetIndicators();
  clear($('kardex').querySelector('tbody'));
  setPartStatus($('kardex-status'), [{ outcome: 'pending' }]);

  const finish = () => {
    if (stale()) return;
    setRecordStamp(worstOutcome(outcomes), true);
  };

  // Ficha del alumno (C1)
  const student = await runLogged(`C1 ${enrollment}`, 'Ficha del alumno');
  if (stale()) return;
  outcomes.push(student.outcome);
  if (student.outcome !== 'verified') {
    $('student-name').textContent = 'No se pudo obtener la ficha';
    setPartStatus($('identity-status'), [student], 'C1');
    skipRest('No se consultó porque la ficha no se verificó.');
    finish();
    return;
  }
  if (student.result.row_count === 0) {
    $('student-name').textContent = `No hay alumno con la boleta ${enrollment}`;
    $('student-enrollment').textContent = enrollment;
    setPartStatus($('identity-status'), [student], 'C1: el probador demostró que ninguna fila tiene esa boleta');
    $('indicators-section').hidden = true;
    $('kardex-section').hidden = true;
    finish();
    return;
  }
  const s = rowObject(student.result, 0);
  $('student-name').textContent = `${s.first_name} ${s.paternal_surname} ${s.maternal_surname}`;
  $('student-enrollment').textContent = String(s.enrollment_number);
  $('student-admission').textContent = formatTerm(s.admission_term);

  // Programa academico (rango de un valor sobre program_id)
  const program = await runLogged(`range program program_id ${s.program_id} ${s.program_id}`, 'Programa académico');
  if (stale()) return;
  outcomes.push(program.outcome);
  if (program.outcome === 'verified' && program.result.rows.length) {
    const p = rowObject(program.result, 0);
    $('student-program').textContent = `${p.program_code}, ${p.program_name}`;
  } else {
    $('student-program').textContent = `Programa ${s.program_id} (sin verificar)`;
  }
  setPartStatus($('identity-status'), [student, program], 'C1 y programa');

  // Historial (C2) y unidades de aprendizaje (rango sobre course_id)
  const records = await runLogged(`C2 ${s.student_id}`, 'Historial académico');
  if (stale()) return;
  outcomes.push(records.outcome);
  if (records.outcome === 'verified') {
    let courses = null;
    const ids = records.result.rows.map((_, i) => rowObject(records.result, i).course_id);
    if (ids.length) {
      courses = await runLogged(`range course course_id ${Math.min(...ids)} ${Math.max(...ids)}`, 'Unidades de aprendizaje');
      if (stale()) return;
      outcomes.push(courses.outcome);
    }
    renderKardex(records, courses);
    const parts = courses ? [records, courses] : [records];
    setPartStatus($('kardex-status'), parts, `C2, ${formatCount(records.result.row_count)} registros`);
  } else {
    setPartStatus($('kardex-status'), [records], 'C2');
    const body = $('kardex').querySelector('tbody');
    const why = records.outcome === 'rejected'
      ? `El historial no se muestra porque la respuesta del probador no pasó la verificación: ${records.message}`
      : `El historial no se pudo consultar: ${records.message || OUTCOME_TEXT[records.outcome]}`;
    body.append(el('tr', {}, el('td', { className: `table-empty ${records.outcome}`, text: why, attrs: { colspan: '6' } })));
  }

  // Indicadores, del mas rapido al mas lento
  for (const ind of [...INDICATORS].sort((a, b) => a.order - b.order)) {
    const reply = await runLogged(ind.query(s.student_id), ind.label);
    if (stale()) return;
    outcomes.push(reply.outcome);
    fillIndicator(ind, reply);
  }
  finish();
}

/* ---- Vista del catalogo ------------------------------------------------ */

/*
 * Busca la definicion de una consulta del catalogo por su id.
 */
function findQuery(id) {
  for (const group of CATALOG) {
    for (const item of group.items) if (item.id === id) return item;
  }
  return null;
}

/*
 * Dibuja la lista de consultas agrupadas.
 */
function renderCatalogList() {
  const nav = $('catalog-list');
  clear(nav);
  for (const group of CATALOG) {
    nav.append(el('h2', { text: group.group }));
    for (const item of group.items) {
      const button = el('button', { attrs: { type: 'button', 'data-id': item.id } },
        el('span', { className: 'query-id', text: item.id === 'range' ? 'rango' : item.id }),
        el('span', { text: item.title }));
      button.addEventListener('click', () => {
        selectQuery(item.id);
        history.replaceState(null, '', `#catalogo/${item.id}`);
      });
      nav.append(button);
    }
  }
}

/*
 * Crea el campo de un parametro: lista de opciones o numero entero.
 */
function makeParam(param, index) {
  const id = `param-${index}`;
  const box = el('div', { className: param.kind === 'column' ? 'param wide' : 'param' },
    el('label', { text: param.label, attrs: { for: id } }));
  let input;
  if (param.kind === 'column') {
    input = el('select', { attrs: { id } });
    const columns = state.status ? state.status.client.range_columns : [];
    for (const c of columns) {
      const value = `${c.table} ${c.column}`;
      input.append(el('option', { text: `${c.table}.${c.column}`, attrs: { value } }));
    }
    const preferred = columns.findIndex((c) => c.table === 'academic_record' && c.column === 'evaluation_type');
    if (preferred >= 0) input.selectedIndex = preferred;
    if (!columns.length) input.append(el('option', { text: 'Cargando columnas…', attrs: { value: '' } }));
  } else if (param.options) {
    input = el('select', { attrs: { id } });
    for (const [value, text] of Object.entries(param.options)) {
      input.append(el('option', { text: `${value}, ${text}`, attrs: { value } }));
    }
    input.value = param.value;
  } else {
    input = el('input', { attrs: { id, inputmode: 'numeric', pattern: '-?[0-9]{1,18}', maxlength: '19', required: '' } });
    input.value = param.value || '';
  }
  input.addEventListener('input', schedulePreview);
  box.append(input);
  if (param.hint) box.append(el('small', { text: param.hint }));
  return box;
}

/*
 * Muestra una consulta del catalogo con sus parametros.
 */
function selectQuery(id) {
  if (state.benchmark.running) return;
  const item = findQuery(id) || findQuery('C1');
  state.current = item;
  for (const button of $('catalog-list').querySelectorAll('button')) {
    button.setAttribute('aria-current', String(button.dataset.id === item.id));
  }
  $('query-id').textContent = item.id === 'range' ? 'rango' : item.id;
  $('query-title').textContent = item.title;
  $('query-what').textContent = item.what;
  const params = $('query-params');
  clear(params);
  item.params.forEach((p, i) => params.append(makeParam(p, i)));
  $('query-note').textContent = item.note || '';
  $('query-note').hidden = !item.note;
  $('query-result').hidden = true;
  $('query-direct').disabled = !state.status || !state.status.client.direct_benchmark_enabled || item.id === 'range';
  updatePreview();
}

/*
 * Texto de la consulta con los valores de los campos, o null si falta un
 * valor o no es un entero.
 */
function currentQueryText() {
  const item = state.current;
  const words = [item.id];
  const fields = $('query-params').querySelectorAll('input, select');
  for (const field of fields) {
    const value = field.value.trim();
    if (!value) return null;
    if (field.tagName === 'INPUT' && !/^-?[0-9]{1,18}$/.test(value)) return null;
    words.push(value);
  }
  return words.join(' ');
}

/*
 * Pide la vista previa poco despues de que el usuario deja de escribir.
 */
function schedulePreview() {
  clearTimeout(state.previewTimer);
  state.previewTimer = setTimeout(updatePreview, 250);
}

/*
 * Muestra el SQL y los AIT de la consulta actual (los decide el verificador
 * con el mismo parse_query que usa para verificar).
 */
async function updatePreview() {
  if (state.benchmark.running) return;
  const box = $('query-preview');
  const text = currentQueryText();
  if (text === null) {
    box.dataset.outcome = 'invalid';
    $('query-sql').textContent = '';
    $('query-trees').textContent = 'Escribe un número entero en cada campo.';
    $('query-submit').disabled = true;
    return;
  }
  const reply = await callApi('/api/preview', text);
  if (text !== currentQueryText()) return;  // el usuario siguio escribiendo
  box.dataset.outcome = reply.outcome;
  $('query-sql').textContent = reply.sql || text;
  if (reply.outcome === 'ready') {
    $('query-trees').textContent = `Se verifica con ${reply.trees.length === 1 ? 'el AIT' : 'los AIT'} ${reply.trees.join(', ')}.`;
    $('query-submit').disabled = state.benchmark.running;
  } else {
    $('query-trees').textContent = reply.message || OUTCOME_TEXT[reply.outcome] || '';
    $('query-submit').disabled = true;
  }
}

/*
 * Ejecuta una consulta completa y mide hasta el pintado aproximado del
 * resultado. Esta ruta es compartida por los botones y el experimento para
 * que los dos modos incluyan el mismo trabajo de presentacion.
 */
async function executeCatalogSample(text, direct) {
  const started = performance.now();
  const section = $('query-result');
  section.hidden = false;
  renderVerdict({ outcome: 'pending' });
  clear($('result-timing'));
  clear($('result-body'));
  const reply = await callApi(direct ? '/api/direct' : '/api/query', text, true);
  renderVerdict(reply);
  renderTiming(reply);
  renderResultBody(reply);

  // Dos cuadros permiten que el navegador presente el resultado. Para
  // comparar los modos, la pestana debe seguir visible durante las series.
  await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
  reply.browser_total_seconds = (performance.now() - started) / 1000;
  reply.browser_render_seconds = Math.max(0, reply.browser_total_seconds - (reply.browser_request_seconds || 0));
  renderTiming(reply);
  return reply;
}

/*
 * Envia una consulta manual y conserva su medicion en el registro local.
 */
async function runCatalogQuery(event, direct = false) {
  event.preventDefault();
  if (state.benchmark.running || state.manualQueryRunning) return;
  const text = currentQueryText();
  if (text === null || (direct && (!state.status || !state.status.client.direct_benchmark_enabled))) return;
  const verifiedButton = $('query-submit');
  const directButton = $('query-direct');
  verifiedButton.disabled = true;
  directButton.disabled = true;
  state.manualQueryRunning = true;
  $('benchmark-run').disabled = true;
  try {
    const reply = await executeCatalogSample(text, direct);
    if (text === currentQueryText()) saveMeasurement(text, reply, reply.browser_request_seconds);
  } finally {
    state.manualQueryRunning = false;
    $('benchmark-run').disabled = false;
    verifiedButton.disabled = $('query-preview').dataset.outcome !== 'ready';
    directButton.disabled = !state.status || !state.status.client.direct_benchmark_enabled ||
      state.current.id === 'range';
  }
}

/* ---- Experimentos pareados de rendimiento ------------------------------ */

/*
 * Compara la respuesta completa como multiconjunto, sin depender del orden
 * de MySQL. Los valores de AVG (C10) admiten el redondeo decimal de MySQL.
 * Esta comprobacion se hace DESPUES del cronometro del navegador.
 */
function equivalentResults(verified, direct, catalogId) {
  if (!verified || !direct || verified.outcome !== 'verified' || direct.outcome !== 'direct' ||
      direct.unverified !== true || !verified.result || !direct.result) return false;
  const a = verified.result;
  const b = direct.result;
  if (a.truncated || b.truncated || a.row_count !== b.row_count ||
      !Array.isArray(a.rows) || !Array.isArray(b.rows) ||
      a.rows.length !== a.row_count || b.rows.length !== b.row_count ||
      JSON.stringify(a.columns) !== JSON.stringify(b.columns)) return false;
  if (catalogId === 'C10') {
    if (a.rows.length !== 1 || b.rows.length !== 1) return false;
    const x = a.rows[0][0];
    const y = b.rows[0][0];
    return (x === null && y === null) || (typeof x === 'number' && typeof y === 'number' &&
      Number.isFinite(x) && Number.isFinite(y) && Math.abs(x - y) <= 0.0001);
  }
  const key = (row) => JSON.stringify(row.map((value) => {
    if (typeof value === 'number') return ['n', Number.isFinite(value) ? value.toFixed(9) : 'nonfinite'];
    return [value === null ? 'null' : 'text', value];
  }));
  const counts = new Map();
  for (const row of a.rows) {
    const k = key(row);
    counts.set(k, (counts.get(k) || 0) + 1);
  }
  for (const row of b.rows) {
    const k = key(row);
    const count = counts.get(k);
    if (!count) return false;
    if (count === 1) counts.delete(k);
    else counts.set(k, count - 1);
  }
  return counts.size === 0;
}

/*
 * Resumen de muestras independientes (no incluye calentamientos).
 * p95 utiliza el rango mas cercano; la desviacion es muestral (n-1).
 */
function benchmarkStatistics(samples) {
  if (!samples.length || samples.some((v) => !Number.isFinite(v) || v <= 0)) return null;
  const sorted = [...samples].sort((a, b) => a - b);
  const n = sorted.length;
  const mean = sorted.reduce((sum, value) => sum + value, 0) / n;
  const median = n % 2 === 1 ? sorted[(n - 1) / 2] :
    (sorted[n / 2 - 1] + sorted[n / 2]) / 2;
  const variance = n > 1 ? sorted.reduce((sum, v) => sum + (v - mean) ** 2, 0) / (n - 1) : 0;
  return { n, mean, median, deviation: Math.sqrt(variance), p95: sorted[Math.ceil(0.95 * n) - 1] };
}

/*
 * Muestra el resumen de pares comprobados. Ninguna ejecucion fallida entra
 * en los estadisticos y cada valor individual queda en el CSV.
 */
function renderBenchmarkSummary() {
  const box = $('benchmark-summary');
  clear(box);
  const pairs = state.benchmark.validPairs;
  if (!pairs.length) {
    box.append(el('p', { className: 'hint', text: 'Todavía no hay pares válidos en esta serie.' }));
    return;
  }
  const verified = benchmarkStatistics(pairs.map((p) => p.verified));
  const direct = benchmarkStatistics(pairs.map((p) => p.direct));
  box.append(el('h3', { text: `Resumen de ${pairs.length} pares válidos (sin calentamientos)` }));
  const table = el('table', { className: 'benchmark-table' });
  const header = el('tr');
  for (const name of ['Modo', 'Mediana', 'Media', 'Desviación', 'p95']) header.append(el('th', { text: name }));
  table.append(el('thead', {}, header));
  for (const [label, stats] of [['Con verificación', verified], ['SQL directo', direct]]) {
    const tr = el('tr');
    for (const value of [label, formatSeconds(stats.median), formatSeconds(stats.mean),
      formatSeconds(stats.deviation), formatSeconds(stats.p95)]) tr.append(el('td', { text: value }));
    table.append(el('tbody', {}, tr));
  }
  box.append(el('div', { className: 'table-wrap' }, table));
  const difference = verified.median - direct.median;
  const signedDifference = difference < 0 ? `−${formatSeconds(-difference)}` :
    `+${formatSeconds(difference)}`;
  box.append(el('p', { className: 'benchmark-comparison', text:
    `Diferencia de medianas (verificado − directo): ${signedDifference}; ` +
    `factor de tiempo: ${(verified.median / direct.median).toFixed(2)}×.` }));
  box.append(el('p', { className: 'hint', text:
    'Mediciones del navegador hasta el pintado aproximado; las cabeceras del probador son informativas.' }));
}

/*
 * Congela seleccion y parametros durante el experimento para no mezclar
 * consultas ni permitir ejecuciones manuales que contiendan por el servidor.
 */
function setBenchmarkControls(running) {
  for (const node of $('catalog-list').querySelectorAll('button')) node.disabled = running;
  for (const node of $('query-params').querySelectorAll('input, select')) node.disabled = running;
  $('query-submit').disabled = running || $('query-preview').dataset.outcome !== 'ready';
  $('query-direct').disabled = running || !state.status || !state.status.client.direct_benchmark_enabled ||
    state.current.id === 'range';
  $('benchmark-run').disabled = running || state.manualQueryRunning;
  $('benchmark-stop').disabled = !running;
  $('benchmark-warmups').disabled = running;
  $('benchmark-repetitions').disabled = running;
  $('benchmark-environment').disabled = running;
  for (const node of document.querySelectorAll('.tab')) node.disabled = running;
  updateMeasurementsToolbar();
}

/*
 * Comprueba el estado inmediatamente antes del lote. Los cronometros de
 * consulta comienzan despues de esta peticion, de modo que la preparacion
 * no contamina los tiempos de cada modalidad.
 */
function benchmarkPreflight(status) {
  if (!status || !status.client || !status.client.direct_benchmark_enabled ||
      !status.prover || !status.prover.reachable || !status.prover.matches ||
      !status.client.state_id || !Array.isArray(status.client.tables)) {
    return null;
  }
  return {
    stateId: status.client.state_id,
    stateVersion: status.client.state_version,
    plan: status.client.plan || '',
    tables: JSON.stringify(status.client.tables.map((table) => ({
      name: table.name, rows: table.rows,
    }))),
    proverAddress: status.prover.address || '',
    browserUserAgent: typeof navigator !== 'undefined' && navigator.userAgent
      ? navigator.userAgent : '',
  };
}

/*
 * Ejecuta pares secuenciales con orden alternado: V-D, D-V. Los primeros
 * pares son calentamientos; solo las muestras completas y equivalentes
 * participan en los estadisticos. Detener termina el par en curso.
 */
async function runBenchmark() {
  if (state.benchmark.running || state.manualQueryRunning) return;
  const query = currentQueryText();
  const catalogId = state.current ? state.current.id : '';
  const warmups = Number($('benchmark-warmups').value);
  const repetitions = Number($('benchmark-repetitions').value);
  const environment = $('benchmark-environment').value.trim();
  const enabled = state.status && state.status.client.direct_benchmark_enabled;
  const progress = $('benchmark-progress');
  if (!enabled || !query || $('query-preview').dataset.outcome !== 'ready' || catalogId === 'range' ||
      !Number.isInteger(warmups) || warmups < 0 || warmups > 10 ||
      !Number.isInteger(repetitions) || repetitions < 1 || repetitions > 100 ||
      !environment || environment.length > 80) {
    progress.textContent = 'Selecciona C1–C14, activa SQL directo e indica entorno, calentamientos (0–10) y pares (1–100).';
    return;
  }
  if (document.visibilityState && document.visibilityState !== 'visible') {
    progress.textContent = 'Mantén la pestaña visible para medir hasta el pintado.';
    return;
  }
  if (state.measurements.length + 2 * (warmups + repetitions) > 1000) {
    progress.textContent = 'El registro de 1000 mediciones se llenaría. Exporta el CSV y limpia el registro antes de comenzar.';
    return;
  }
  // Bloquea nuevas solicitudes mientras se obtiene una instantanea del
  // entorno. Esta comprobacion nunca entra en el tiempo de consulta.
  state.benchmark = { running: true, stopRequested: false, validPairs: [] };
  setBenchmarkControls(true);
  progress.textContent = 'Comprobando el estado del probador y del verificador…';
  const freshStatus = await callApi('/api/status');
  const snapshot = benchmarkPreflight(freshStatus);
  if (!snapshot || state.benchmark.stopRequested) {
    state.benchmark.running = false;
    setBenchmarkControls(false);
    progress.textContent = snapshot
      ? 'Se canceló la preparación del experimento.'
      : 'No se inició la serie: el probador no responde, usa otro delta o SQL directo no está habilitado.';
    return;
  }
  state.status = freshStatus;
  const runId = new Date().toISOString().replace(/[:.]/g, '-') + '-' + (state.measurements.length + 1);
  state.benchmark.id = runId;
  state.benchmark.snapshot = snapshot;
  renderBenchmarkSummary();
  let failure = '';
  try {
    for (const [phase, count] of [['warmup', warmups], ['sample', repetitions]]) {
      for (let index = 0; index < count; index++) {
        if (state.benchmark.stopRequested) break;
        if (document.visibilityState && document.visibilityState !== 'visible') {
          throw new Error('La pestaña dejó de estar visible; la serie se detuvo.');
        }
        const pair = index + 1;
        const modes = index % 2 === 0 ? ['verified', 'direct'] : ['direct', 'verified'];
        const order = modes.join('-');
        const replies = {};
        const rows = {};
        progress.textContent = `${phase === 'warmup' ? 'Calentamiento' : 'Muestra'} ${pair}/${count}: ${query} (${order}).`;
        for (const mode of modes) {
          const reply = await executeCatalogSample(query, mode === 'direct');
          replies[mode] = reply;
          rows[mode] = saveMeasurement(query, reply, reply.browser_request_seconds, {
            id: runId, phase, pair, order, environment, snapshot,
            visible: !document.visibilityState || document.visibilityState === 'visible',
          });
        }
        const valid = equivalentResults(replies.verified, replies.direct, catalogId) &&
          rows.verified.benchmark_browser_visible && rows.direct.benchmark_browser_visible &&
          Number.isFinite(replies.verified.browser_total_seconds) &&
          Number.isFinite(replies.direct.browser_total_seconds) &&
          replies.verified.browser_total_seconds > 0 && replies.direct.browser_total_seconds > 0;
        rows.verified.benchmark_pair_valid = valid;
        rows.direct.benchmark_pair_valid = valid;
        if (!valid) throw new Error(`El par ${pair} (${phase}) tuvo resultados distintos, fallo o pestaña oculta.`);
        if (phase === 'sample') {
          state.benchmark.validPairs.push({
            verified: replies.verified.browser_total_seconds,
            direct: replies.direct.browser_total_seconds,
          });
          renderBenchmarkSummary();
        }
      }
      if (state.benchmark.stopRequested) break;
    }
  } catch (error) {
    failure = error && error.message ? error.message : 'Error inesperado durante la medición.';
  } finally {
    const count = state.benchmark.validPairs.length;
    const stopped = state.benchmark.stopRequested;
    state.benchmark.running = false;
    setBenchmarkControls(false);
    progress.textContent = failure ? `Serie detenida: ${failure} Exporta el CSV para revisar las muestras.` :
      stopped ? `Detenido por el usuario: ${count} pares válidos. Exporta el CSV.` :
        `Serie finalizada: ${count} pares válidos. Exporta el CSV antes de recargar.`;
  }
}

/*
 * Sello y explicacion del veredicto de una consulta del catalogo.
 */
function renderVerdict(reply) {
  const box = $('result-verdict');
  clear(box);
  const outcome = reply.outcome === 'pending' ? 'pending' : styleOutcome(reply.outcome);
  box.dataset.outcome = reply.outcome;
  if (reply.outcome === 'direct') {
    box.append(el('div', { className: 'direct-banner', text:
      'SQL DIRECTO · RESULTADO NO VERIFICADO. Sin garantías de integridad criptográfica.' }));
    return;
  }
  const id = reply.state ? reply.state.id : state.status ? state.status.client.state_id : '';
  const top = {
    pending: 'Esperando respuesta',
    verified: 'Respuesta verificada',
    rejected: 'Respuesta rechazada',
    no_answer: 'Sin respuesta',
  }[outcome];
  box.append(makeStamp(outcome, top, id, outcome !== 'pending'));
  const text = el('div');
  if (reply.outcome === 'pending') {
    text.append(el('h2', { text: 'Esperando al probador…' }),
      el('p', { text: 'El probador arma la respuesta y su prueba; después el verificador la revisa contra δ.' }));
  } else if (reply.outcome === 'verified') {
    text.append(el('h2', { text: 'Respuesta verificada' }),
      el('p', { text: `El resultado coincide con la base que autenticó el propietario: ${reply.detail}.` }));
  } else if (reply.outcome === 'rejected') {
    text.append(el('h2', { text: 'Respuesta rechazada' }),
      el('p', { className: 'reason', text: reply.message }),
      el('p', { text: 'El resultado no se muestra porque no se pudo comprobar.' }));
  } else {
    text.append(el('h2', { text: OUTCOME_TEXT[reply.outcome] || 'Sin respuesta' }), el('p', { text: reply.message || '' }));
  }
  box.append(text);
}

/*
 * Dibuja las mediciones de C++ y de HTTP con sus fronteras correctas.
 * Los tiempos anidados no se suman (handler contiene ProofGen, por ejemplo).
 */
function renderTiming(reply) {
  const box = $('result-timing');
  clear(box);
  const block = (title, value, note) => el('div', {}, el('dt', { text: title }), el('dd', { text: value }), el('small', { text: note }));
  if (reply.outcome === 'direct' && reply.direct) {
    const d = reply.direct;
    box.append(block('Tiempo total del navegador', formatSeconds(reply.browser_total_seconds),
      'Desde la acción del usuario hasta el primer pintado de resultados'));
    box.append(block('SQL sin verificación', formatSeconds(d.sql_seconds),
      'Conexión MySQL, ejecución SQL y lectura de todas las filas'));
    box.append(block('Codificación JSON', formatSeconds(d.encoding_seconds), 'Preparación del resultado SQL'));
    box.append(block('Ida y vuelta HTTP', formatSeconds(d.roundtrip_seconds),
      `${formatBytes(d.bytes || 0)} JSON SQL devuelto por el servidor`));
    box.append(block('Procesamiento del servidor', formatSeconds(d.handler_seconds), 'Incluye SQL, cola y JSON'));
    box.append(block('Espera en cola', formatSeconds(d.queue_seconds), 'Candado compartido con ProofGen'));
    box.append(block('Red/HTTP (estimada)', formatSeconds(d.network_overhead_seconds),
      'Diferencia entre ida/vuelta y procesamiento; no mide latencia física pura'));
    box.append(block('Solicitud del navegador', formatSeconds(reply.browser_request_seconds),
      'Hasta recibir y leer JSON; no incluye pintar los resultados'));
    box.append(el('p', { className: 'timing-direct-note', text:
      'SQL directo es el referente de rendimiento, NO una respuesta autenticada.' }));
    return;
  }
  if (!reply.prover || reply.prover.status !== 200) return;
  box.append(block('Obtención HTTP', formatSeconds(reply.prover.seconds),
    `Ida y vuelta al probador, ${formatBytes(reply.prover.bytes)} (R, π)`));
  if (reply.verification) {
    const stages = (reply.verification.stages || []).map((st) => `${st.name} ${formatSeconds(st.seconds)}`).join(', ');
    box.append(block('Verificación C++', formatSeconds(reply.verification.seconds), stages || 'Verify en tt_verifier'));
  }
  if (typeof reply.client_total_seconds === 'number') {
    box.append(block('Flujo en verificador', formatSeconds(reply.client_total_seconds),
      'Obtención, espera y verificación, sin construir el JSON'));
  }
  const detail = el('div', { className: 'timing-breakdown' });
  detail.append(el('h3', { text: 'Desglose de tiempos' }));
  const rows = el('dl', { className: 'timing-rows' });
  const add = (label, seconds, note) => rows.append(el('div', { className: 'timing-row' },
    el('dt', { text: label }), el('dd', { text: formatSeconds(seconds) }), el('small', { text: note })));
  if (reply.prover.timings) {
    const t = reply.prover.timings;
    add('Generación en probador', t.generation_seconds, 'Incluye acceso a datos; no es CPU criptográfica aislada');
    if (typeof t.row_fetch_seconds === 'number') {
      add('Recuperación de filas D', t.row_fetch_seconds,
        'Incluye conexión y lecturas de RowStore; no es SQL directo');
    }
    if (t.stages) {
      add('AIT: rangos', t.stages.ait_seconds, 'Operaciones AIT instrumentadas');
      add('Merkle: hashes e inclusión', t.stages.merkle_seconds,
        'Hash y prueba de inclusión, no incluye el acceso a MySQL');
      add('ASO: Split y Bézout', t.stages.aso_seconds,
        'Incluye conversiones de elementos del campo para los testigos');
      add('Otros costos de ProofGen', t.stages.other_seconds,
        'Preparación, cruces, estructuras auxiliares y otras operaciones no clasificadas');
    } else if (typeof t.row_fetch_seconds === 'number') {
      add('Resto de ProofGen', Math.max(0, t.generation_seconds - t.row_fetch_seconds),
        'Incluye criptografía y trabajo auxiliar; no hay desglose detallado');
    }
    add('Codificación TTRSP', t.encoding_seconds, 'Construcción de los bytes de (R, π)');
    add('Espera en cola', t.queue_seconds, 'Espera para obtener el candado del probador');
    add('Procesamiento servidor', t.handler_seconds, 'Incluye cola, generación y codificación; excluye envío TCP');
    add('Sobrecarga de red/HTTP (estimada)', reply.prover.network_overhead_seconds,
      'Diferencia entre ida/vuelta y servidor; incluye HTTP y planificación, no es latencia de red pura');
  } else {
    detail.append(el('p', { className: 'timing-warning',
      text: 'El probador no proporcionó métricas válidas; se conserva el tiempo HTTP medido por el verificador.' }));
  }
  add('Ida y vuelta HTTP', reply.prover.seconds, 'Medida por el cliente C++ del verificador');
  if (reply.verification) {
    add('Verify total', reply.verification.seconds,
      'Incluye decodificación, verificación y construcción del resultado');
    for (const stage of reply.verification.stages || []) {
      add(`Verify: ${stage.name}`, stage.seconds,
        'Tiempo de una etapa interna; las etapas no se suman de nuevo al total');
    }
  }
  add('Solicitud del navegador', reply.browser_request_seconds, 'Incluye la API del verificador y lectura de JSON; no el renderizado');
  if (typeof reply.browser_total_seconds === 'number') {
    add('Tiempo total del navegador', reply.browser_total_seconds,
      'Desde la acción del usuario hasta pintar el resultado (incluye interfaz)');
  }
  detail.append(rows, el('p', { className: 'timing-note',
    text: 'Los tiempos declarados por el probador son informativos y no están autenticados; Verify decide con (R, π), δ y la clave pública.' }));
  box.append(detail);
}

/*
 * Resultado verificado: un valor (AVG, COUNT, MIN, SUM) o una tabla
 * paginada.
 */
function renderResultBody(reply) {
  const box = $('result-body');
  clear(box);
  if (!['verified', 'direct'].includes(reply.outcome) || !reply.result) return;
  const result = reply.result;
  const isScalar = result.columns.length === 1 && result.rows.length === 1 && /\(/.test(result.columns[0]);
  if (isScalar) {
    const v = result.rows[0][0];
    let note = '';
    if (v === null) note = 'NULL: no hay filas que cumplan la condición.';
    else if (reply.catalog_id === 'C14') note = `${formatCredits(v)} créditos`;
    else if (reply.catalog_id === 'C10') note = `Promedio ${v.toFixed(2)}`;
    box.append(el('div', { className: 'scalar' },
      el('span', { className: 'scalar-name', text: result.columns[0] }),
      el('span', { className: 'scalar-value', text: v === null ? 'NULL' : String(v) }),
      note ? el('span', { className: 'scalar-note', text: note }) : null));
    return;
  }
  renderTablePage(box, result, 0, reply.outcome === 'verified');
}

/*
 * Dibuja una pagina de la tabla de resultados con sus botones.
 */
function renderTablePage(box, result, page, verified = true) {
  clear(box);
  if (!result.rows.length) {
    box.append(el('p', { className: 'table-empty', text: verified
      ? 'Ninguna fila cumple la condición. El resultado vacío también se verificó.'
      : 'MySQL devolvió cero filas. Este resultado NO fue verificado.' }));
    return;
  }
  const pages = Math.ceil(result.rows.length / PAGE_SIZE);
  const start = page * PAGE_SIZE;
  const end = Math.min(start + PAGE_SIZE, result.rows.length);
  const table = el('table', { className: 'data' });
  const head = el('tr');
  const numeric = result.columns.map((_, c) => result.rows.slice(0, 50).every((r) => r[c] === null || typeof r[c] === 'number'));
  result.columns.forEach((name, c) => head.append(el('th', { text: name, className: numeric[c] ? 'num' : '', attrs: { scope: 'col' } })));
  table.append(el('thead', {}, head));
  const body = el('tbody');
  for (let r = start; r < end; r++) {
    const tr = el('tr');
    result.rows[r].forEach((v, c) => {
      if (v === null) tr.append(el('td', { className: 'null', text: 'NULL' }));
      else tr.append(el('td', { className: numeric[c] ? 'num' : '', text: String(v) }));
    });
    body.append(tr);
  }
  table.append(body);
  box.append(el('div', { className: 'table-wrap' }, table));

  const total = result.truncated ? `${formatCount(result.rows.length)} mostradas de ${formatCount(result.row_count)}` : formatCount(result.row_count);
  const prev = el('button', { className: 'plain', text: 'Anterior', attrs: { type: 'button' } });
  const next = el('button', { className: 'plain', text: 'Siguiente', attrs: { type: 'button' } });
  prev.disabled = page === 0;
  next.disabled = page >= pages - 1;
  prev.addEventListener('click', () => renderTablePage(box, result, page - 1, verified));
  next.addEventListener('click', () => renderTablePage(box, result, page + 1, verified));
  box.append(el('div', { className: 'pager' },
    el('span', { text: `Filas ${formatCount(start + 1)} a ${formatCount(end)} de ${total}` }),
    el('div', {}, prev, next)));
}

/* ---- Inicio ------------------------------------------------------------ */

/*
 * Conecta los eventos, elige la vista segun la direccion y empieza a revisar
 * el estado del probador cada 15 segundos.
 */
function start() {
  for (const tab of document.querySelectorAll('.tab')) {
    tab.addEventListener('click', () => showView(tab.dataset.view));
  }
  $('record-form').addEventListener('submit', (event) => {
    event.preventDefault();
    const value = $('record-enrollment').value.trim();
    if (!/^[0-9]{1,18}$/.test(value)) return;
    loadRecord(value);
  });
  for (const sample of document.querySelectorAll('.sample')) {
    sample.addEventListener('click', () => {
      $('record-enrollment').value = sample.dataset.enrollment;
      loadRecord(sample.dataset.enrollment);
    });
  }
  $('query-form').addEventListener('submit', (event) => runCatalogQuery(event, false));
  $('query-direct').addEventListener('click', (event) => runCatalogQuery(event, true));
  $('benchmark-run').addEventListener('click', runBenchmark);
  $('benchmark-stop').addEventListener('click', () => {
    state.benchmark.stopRequested = true;
    $('benchmark-progress').textContent = 'Deteniendo después del par en curso…';
  });
  $('measurements-export').addEventListener('click', exportMeasurements);
  $('measurements-clear').addEventListener('click', () => {
    state.measurements = [];
    updateMeasurementsToolbar();
  });
  updateMeasurementsToolbar();
  renderCatalogList();

  const hash = decodeURIComponent(location.hash.slice(1));
  const [view, id] = hash.split('/');
  selectQuery(view === 'catalogo' && id ? id : 'C1');
  showView(view === 'catalogo' ? 'catalog' : 'record', false);

  // La direccion tambien cambia con los botones de atras y adelante.
  window.addEventListener('hashchange', () => {
    const [v, q] = decodeURIComponent(location.hash.slice(1)).split('/');
    if (v === 'catalogo') {
      if (q && (!state.current || state.current.id !== q)) selectQuery(q);
      showView('catalog', false);
    } else {
      showView('record', false);
    }
  });

  refreshStatus();
  setInterval(refreshStatus, 15000);
}

start();
