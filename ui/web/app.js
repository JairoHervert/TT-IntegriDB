/*
 * app.js - interfaz del caso de estudio (TT 2026-B143)
 *
 * La pagina habla solo con 'tt_verifier ui' (mismo origen):
 *   GET  /api/status    estado de delta local y del probador
 *   POST /api/preview   SQL y AIT de una consulta, sin enviarla al probador
 *   POST /api/query     veredicto del verificador y resultado verificado
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
};

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
  if (typeof seconds !== 'number') return '';
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

/* ---- Llamadas al verificador ------------------------------------------- */

/*
 * Llama a la interfaz del verificador. Con cuerpo es POST (texto de la
 * consulta) y lleva la cabecera X-TT-UI que exige el servidor. Siempre
 * devuelve un objeto con "outcome".
 */
async function callApi(path, body) {
  const options = body === undefined
    ? { method: 'GET', cache: 'no-store' }
    : { method: 'POST', headers: { 'X-TT-UI': '1', 'Content-Type': 'text/plain' }, body };
  let response;
  try {
    response = await fetch(path, options);
  } catch (e) {
    return { outcome: 'ui_down', message: 'No hay conexión con tt_verifier ui; revisa que siga en ejecución.' };
  }
  try {
    return await response.json();
  } catch (e) {
    return { outcome: 'error', message: `La interfaz respondió algo inesperado (código ${response.status}).` };
  }
}

/* ---- Estado del probador ----------------------------------------------- */

/*
 * Pide /api/status y actualiza el aviso de la banda superior y el pie.
 */
async function refreshStatus() {
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
      const verifier = reply.verification ? `, verificador ${formatSeconds(reply.verification.seconds)}` : '';
      li.append(el('p', { className: 'ledger-times', text: `Probador ${formatSeconds(reply.prover.seconds)} (${formatBytes(reply.prover.bytes)})${verifier}` }));
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
    $('query-submit').disabled = false;
  } else {
    $('query-trees').textContent = reply.message || OUTCOME_TEXT[reply.outcome] || '';
    $('query-submit').disabled = true;
  }
}

/*
 * Envia la consulta del catalogo y muestra el veredicto.
 */
async function runCatalogQuery(event) {
  event.preventDefault();
  const text = currentQueryText();
  if (text === null) return;
  const button = $('query-submit');
  button.disabled = true;
  const section = $('query-result');
  section.hidden = false;
  renderVerdict({ outcome: 'pending' });
  clear($('result-timing'));
  clear($('result-body'));
  const reply = await callApi('/api/query', text);
  button.disabled = false;
  if (text !== currentQueryText()) return;
  renderVerdict(reply);
  renderTiming(reply);
  renderResultBody(reply);
}

/*
 * Sello y explicacion del veredicto de una consulta del catalogo.
 */
function renderVerdict(reply) {
  const box = $('result-verdict');
  clear(box);
  const outcome = reply.outcome === 'pending' ? 'pending' : styleOutcome(reply.outcome);
  box.dataset.outcome = reply.outcome;
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
 * Tiempos del probador y del verificador, lado a lado.
 */
function renderTiming(reply) {
  const box = $('result-timing');
  clear(box);
  if (!reply.prover || reply.prover.status !== 200) return;
  const block = (title, value, note) => el('div', {}, el('dt', { text: title }), el('dd', { text: value }), el('small', { text: note }));
  box.append(block('Probador', formatSeconds(reply.prover.seconds), `ProofGen y red, ${formatBytes(reply.prover.bytes)} recibidos`));
  if (reply.verification) {
    const stages = reply.verification.stages.map((st) => `${st.name} ${formatSeconds(st.seconds)}`).join(', ');
    box.append(block('Verificador', formatSeconds(reply.verification.seconds), stages || 'Verify en tt_verifier'));
  }
  if (reply.state) box.append(block('Estado', shortId(reply.state.id), `δ versión ${reply.state.version}`));
}

/*
 * Resultado verificado: un valor (AVG, COUNT, MIN, SUM) o una tabla
 * paginada.
 */
function renderResultBody(reply) {
  const box = $('result-body');
  clear(box);
  if (reply.outcome !== 'verified' || !reply.result) return;
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
  renderTablePage(box, result, 0);
}

/*
 * Dibuja una pagina de la tabla de resultados con sus botones.
 */
function renderTablePage(box, result, page) {
  clear(box);
  if (!result.rows.length) {
    box.append(el('p', { className: 'table-empty', text: 'Ninguna fila cumple la condición. Que el resultado esté vacío también se verificó.' }));
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
  prev.addEventListener('click', () => renderTablePage(box, result, page - 1));
  next.addEventListener('click', () => renderTablePage(box, result, page + 1));
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
  $('query-form').addEventListener('submit', runCatalogQuery);
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
