/*
 * ui_metrics_test.js - pruebas de las mediciones de la pagina (sin navegador).
 *
 * Carga app.js en un contexto aislado con un DOM minimo y sin ejecutar
 * start(). Comprueba la presentacion de tiempos, el registro local, la
 * exportacion CSV y la ausencia de datos de resultados en el archivo.
 * No usa bibliotecas externas; se ejecuta con Node.js.
 */
'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

// Nodo minimo para comprobar texto visible sin depender de un navegador.
class FakeNode {
  constructor(tag = 'div') {
    this.tagName = tag;
    this.children = [];
    this.className = '';
    this.textContent = '';
    this.disabled = false;
    this.attributes = {};
    this.dataset = {};
  }
  append(...children) { this.children.push(...children); }
  remove() { this.children = []; }
  removeChild(child) { this.children.splice(this.children.indexOf(child), 1); }
  get firstChild() { return this.children[0] || null; }
  setAttribute(key, value) { this.attributes[key] = value; }
  click() { this.clicked = true; }
  querySelectorAll() { return []; }
}

/*
 * Construye el DOM que necesitan las funciones de la interfaz y carga la
 * misma version de app.js que entrega tt_verifier ui.
 */
function loadApp() {
  const elements = new Map();
  let exportedBlob;
  const links = [];
  const document = {
    getElementById(id) {
      if (!elements.has(id)) elements.set(id, new FakeNode());
      return elements.get(id);
    },
    createElement(tag) {
      const node = new FakeNode(tag);
      if (tag === 'a') links.push(node);
      return node;
    },
    createTextNode(text) {
      const node = new FakeNode('#text');
      node.textContent = text;
      return node;
    },
    body: new FakeNode('body'),
    visibilityState: 'visible',
    querySelectorAll() { return []; },
  };
  const context = vm.createContext({
    document,
    Node: FakeNode,
    Blob,
    Date,
    performance: { now: () => 1000 },
    navigator: { userAgent: 'test-browser/1.0' },
    URL: {
      createObjectURL(blob) { exportedBlob = blob; return 'blob:test'; },
      revokeObjectURL() {},
    },
    setTimeout(fn) { fn(); },
  });
  const filename = path.join(__dirname, '..', 'ui', 'web', 'app.js');
  const source = fs.readFileSync(filename, 'utf8');
  assert.match(source, /\nstart\(\);\s*$/);
  vm.runInContext(source.replace(/\nstart\(\);\s*$/, '\n'), context, { filename });
  return {
    context,
    elements,
    links,
    getBlob: () => exportedBlob,
    run(expression) { return vm.runInContext(expression, context); },
  };
}

function visible(node) {
  return node.textContent + node.children.map(visible).join('');
}

async function main() {
  const app = loadApp();

  /* El desglose no mezcla los tiempos anidados ni inventa la red. */
  app.run(`renderTiming({
    prover: {status: 200, seconds: 1.2, bytes: 400,
      timings: {generation_seconds: 0.8, encoding_seconds: 0.1,
                queue_seconds: 0.05, handler_seconds: 1.0, row_fetch_seconds: 0.12},
      network_overhead_seconds: 0.2},
    verification: {seconds: 0.03, stages: []},
    client_total_seconds: 1.24, browser_request_seconds: 1.3
  })`);
  const breakdown = visible(app.elements.get('result-timing'));
  assert.match(breakdown, /Generación en probador/);
  assert.match(breakdown, /Recuperación de filas D/);
  assert.match(breakdown, /Resto de ProofGen/);
  app.run(`renderTiming({
    prover: {status: 200, seconds: 1, bytes: 100,
      timings: {generation_seconds: 0.8, encoding_seconds: 0.001,
        queue_seconds: 0, handler_seconds: 0.802, row_fetch_seconds: 0.1,
        stages: {ait_seconds: 0.03, merkle_seconds: 0.2, aso_seconds: 0.4,
          other_seconds: 0.07}}, network_overhead_seconds: 0.198},
    verification: {seconds: 0.1, stages: [{name: 'ASO (Split y Bezout)', seconds: 0.05}]}
  })`);
  const breakdownStages = visible(app.elements.get('result-timing'));
  assert.match(breakdownStages, /AIT: rangos/);
  assert.match(breakdownStages, /Merkle: hashes e inclusión/);
  assert.match(breakdownStages, /ASO: Split y Bézout/);
  assert.match(breakdownStages, /Verify: ASO \(Split y Bezout\)/);
  assert.match(breakdown, /Sobrecarga de red\/HTTP \(estimada\)/);
  assert.match(breakdown, /Ida y vuelta HTTP/);
  assert.match(breakdown, /Verificación C\+\+/);
  assert.match(breakdown, /no están autenticados/);

  app.run(`renderTiming({
    prover: {status: 200, seconds: 0.4, bytes: 10, timings: null, network_overhead_seconds: null},
    verification: {seconds: 0.01, stages: []}, browser_request_seconds: 0.5
  })`);
  assert.match(visible(app.elements.get('result-timing')), /no proporcionó métricas válidas/);

  /* La consulta directa nunca dibuja un sello de respuesta verificada. */
  app.run(`renderVerdict({outcome: 'direct', unverified: true, catalog_id: 'C14',
    result: {columns: ['SUM(course.credits_tenths)'], rows: [[3465]], row_count: 1}})`);
  assert.match(visible(app.elements.get('result-verdict')), /RESULTADO NO VERIFICADO/);
  assert.doesNotMatch(visible(app.elements.get('result-verdict')), /Respuesta verificada/);

  app.run(`renderTiming({outcome: 'direct', browser_request_seconds: 0.24,
    browser_total_seconds: 0.29, direct: {sql_seconds: 0.10, encoding_seconds: 0.02,
      roundtrip_seconds: 0.14, handler_seconds: 0.13, queue_seconds: 0.01,
      network_overhead_seconds: 0.01, bytes: 50}})`);
  assert.match(visible(app.elements.get('result-timing')), /SQL sin verificación/);
  assert.match(visible(app.elements.get('result-timing')), /Tiempo total del navegador/);
  const directNotice = app.elements.get('result-timing').children.find(
    (node) => node.className === 'timing-direct-note');
  assert.ok(directNotice);
  assert.match(visible(directNotice), /referente de rendimiento/);

  /* El estilo equipara los botones y alinea el aviso con las mediciones. */
  const styles = fs.readFileSync(path.join(__dirname, '..', 'ui', 'web', 'styles.css'), 'utf8');
  assert.match(styles, /\.query-actions button \{[^}]*height: 42px/s);
  assert.doesNotMatch(styles, /\.query-form \.primary \{ margin-top: 18px; \}/);
  assert.match(styles, /\.timing > \.timing-direct-note \{[^}]*padding: 12px 24px/s);

  /* Los registros guardan los tiempos pero nunca las filas del resultado. */
  app.run(`saveMeasurement('C14 1', {
    catalog_id: 'C14', outcome: 'verified', state: {id: 'abc', version: 1},
    prover: {address: '127.0.0.1:7070', status: 200, bytes: 42931, seconds: 17.2,
      timings: {generation_seconds: 17.1788, encoding_seconds: 0.0003,
                queue_seconds: 0.000001, handler_seconds: 17.1791,
                stages: {ait_seconds: 0.01, merkle_seconds: 0.02,
                  aso_seconds: 17.0, other_seconds: 0.1488}},
      network_overhead_seconds: 0.0209},
    verification: {seconds: 0.03, stages: [{name: 'Merkle', seconds: 0.01}]},
    client_total_seconds: 17.25,
    result: {row_count: 1, rows: [[3465]]}
  }, 17.4)`);
  app.run(`saveMeasurement('=1+1', {
    outcome: 'rejected', prover: {status: 200, seconds: 0.1, timings: null},
    result: {row_count: 400, rows: [['no-exportar']]}
  }, 0.2)`);
  app.run(`saveMeasurement('C14 1', {
    catalog_id: 'C14', outcome: 'direct', unverified: true,
    direct: {sql_seconds: 0.1, encoding_seconds: 0.02, roundtrip_seconds: 0.14,
      handler_seconds: 0.13, queue_seconds: 0.01, network_overhead_seconds: 0.01, bytes: 50},
    browser_total_seconds: 0.29, browser_render_seconds: 0.05,
    result: {row_count: 1, rows: [[3465]]}
  }, 0.24)`);
  assert.equal(app.run('state.measurements[2].execution_mode'), 'sql_direct');
  assert.equal(app.run('state.measurements.length'), 3);
  assert.equal(app.run('state.measurements[1].result_row_count'), '');
  assert.equal(app.elements.get('measurements-export').disabled, false);
  assert.equal(app.elements.get('measurements-count').textContent, '3 consultas registradas');

  app.run('exportMeasurements()');
  assert.equal(app.links.length, 1);
  assert.equal(app.links[0].clicked, true);
  assert.match(app.links[0].download, /^tt_metricas_.*\.csv$/);
  const csv = await app.getBlob().text();
  assert.deepEqual(Array.from(new Uint8Array(await app.getBlob().arrayBuffer()).slice(0, 3)), [239, 187, 191]);
  assert.ok(csv.startsWith('timestamp_utc,query,catalog_id,'));
  assert.match(csv, /server_generation_seconds/);
  assert.match(csv, /server_aso_seconds/);
  assert.equal(app.run('state.measurements[0].server_aso_seconds'), 17.0);
  assert.match(csv, /17\.1788/);
  assert.match(csv, /sql_direct/);
  assert.match(csv, /browser_total_seconds/);
  assert.match(csv, /benchmark_pair_valid/);
  assert.match(csv, /"'=1\+1"/);  // formula neutralizada en Excel
  assert.doesNotMatch(csv, /no-exportar|3465/); // ninguna fila de la base
  assert.equal(csv.trim().split('\r\n').length, 4);

  /* Las filas se comparan sin depender de su orden y respetan duplicados. */
  assert.equal(app.run(`equivalentResults(
    {outcome: 'verified', result: {columns: ['id'], row_count: 3, rows: [[2], [1], [1]]}},
    {outcome: 'direct', unverified: true,
      result: {columns: ['id'], row_count: 3, rows: [[1], [2], [1]]}}, 'C4')`), true);
  assert.equal(app.run(`equivalentResults(
    {outcome: 'verified', result: {columns: ['id'], row_count: 3, rows: [[2], [1], [1]]}},
    {outcome: 'direct', unverified: true,
      result: {columns: ['id'], row_count: 3, rows: [[1], [2], [2]]}}, 'C4')`), false);
  assert.equal(app.run(`equivalentResults(
    {outcome: 'verified', result: {columns: ['AVG'], row_count: 1, rows: [[8.333333333]]}},
    {outcome: 'direct', unverified: true,
      result: {columns: ['AVG'], row_count: 1, rows: [[8.3333]]}}, 'C10')`), true);
  assert.equal(app.run(`equivalentResults(
    {outcome: 'verified', result: {columns: ['id'], row_count: 1, rows: [[1]]}},
    {outcome: 'direct', unverified: true,
      result: {columns: ['id'], row_count: 1, rows: [[1]], truncated: true}}, 'C4')`), false);

  /* Estadisticos de muestras: mediana, media, s muestral y p95. */
  const stats = app.run('benchmarkStatistics([1, 2, 3, 4])');
  assert.equal(stats.n, 4);
  assert.equal(stats.mean, 2.5);
  assert.equal(stats.median, 2.5);
  assert.equal(stats.p95, 4);
  assert.ok(Math.abs(stats.deviation - Math.sqrt(5 / 3)) < 1e-10);
  assert.equal(app.run('benchmarkStatistics([1, -1])'), null);

  /* Un lote incluye pares V-D/D-V; el calentamiento no entra al resumen. */
  app.run(`state.measurements = [];
    var benchmarkStatus = {
      client: {direct_benchmark_enabled: true, state_id: 'delta-1',
        state_version: 2, plan: 'full', tables: [{name: 'student', rows: 500}]},
      prover: {reachable: true, matches: true, address: '127.0.0.1:7070'}
    };
    state.status = benchmarkStatus;
    callApi = async (path) => path === '/api/status' ? benchmarkStatus : {outcome: 'error'};
    state.current = {id: 'C14'};
    $('query-preview').dataset.outcome = 'ready';
    currentQueryText = () => 'C14 1';
    $('benchmark-warmups').value = '2';
    $('benchmark-repetitions').value = '3';
    $('benchmark-environment').value = 'VM local - 500 alumnos';
    var benchmarkCalls = [];
    executeCatalogSample = async (text, direct) => {
      benchmarkCalls.push(direct ? 'D' : 'V');
      return {catalog_id: 'C14', outcome: direct ? 'direct' : 'verified',
        unverified: direct, browser_request_seconds: 0.1,
        browser_total_seconds: direct ? 0.12 : 0.4,
        direct: direct ? {roundtrip_seconds: 0.1, bytes: 42} : undefined,
        result: {columns: ['SUM'], rows: [[3465]], row_count: 1}};
    };`);
  await app.run('runBenchmark()');
  assert.equal(app.run('state.benchmark.validPairs.length'), 3);
  assert.equal(app.run('state.measurements.length'), 10);
  assert.equal(app.run("state.measurements.filter((r) => r.benchmark_phase === 'warmup').length"), 4);
  assert.equal(app.run("state.measurements.filter((r) => r.benchmark_phase === 'sample').length"), 6);
  assert.equal(app.run('state.measurements.every((r) => r.benchmark_pair_valid === true)'), true);
  assert.equal(app.run('state.measurements.every((r) => r.benchmark_state_id === "delta-1")'), true);
  assert.equal(app.run('state.measurements[0].benchmark_state_version'), 2);
  assert.equal(app.run('state.measurements[0].benchmark_plan'), 'full');
  assert.equal(app.run('state.measurements[0].benchmark_tables'), '[{"name":"student","rows":500}]');
  assert.equal(app.run('state.measurements[0].benchmark_browser_user_agent'), 'test-browser/1.0');
  assert.equal(app.run('benchmarkCalls.join("")'), 'VDDVVDDVVD');
  assert.match(visible(app.elements.get('benchmark-summary')), /3 pares válidos/);
  assert.match(visible(app.elements.get('benchmark-summary')), /factor de tiempo/);
  assert.match(app.elements.get('benchmark-progress').textContent, /Serie finalizada/);
  assert.equal(app.elements.get('benchmark-run').disabled, false);

  /* Una discrepancia detiene la serie y nunca se cuenta como par valido. */
  app.run(`state.measurements = [];
    $('benchmark-warmups').value = '0';
    $('benchmark-repetitions').value = '3';
    executeCatalogSample = async (text, direct) => ({
      catalog_id: 'C14', outcome: direct ? 'direct' : 'verified', unverified: direct,
      browser_request_seconds: 0.1, browser_total_seconds: direct ? 0.12 : 0.4,
      result: {columns: ['SUM'], rows: [[direct ? 1000 : 3465]], row_count: 1}
    });`);
  await app.run('runBenchmark()');
  assert.equal(app.run('state.measurements.length'), 2);
  assert.equal(app.run('state.benchmark.validPairs.length'), 0);
  assert.equal(app.run('state.measurements.every((r) => r.benchmark_pair_valid === false)'), true);
  assert.match(app.elements.get('benchmark-progress').textContent, /resultados distintos/);

  /* Detener no deja el ultimo par a medias. */
  app.run(`state.measurements = [];
    executeCatalogSample = async (text, direct) => {
      state.benchmark.stopRequested = true;
      return {catalog_id: 'C14', outcome: direct ? 'direct' : 'verified',
        unverified: direct, browser_request_seconds: 0.1,
        browser_total_seconds: direct ? 0.12 : 0.4,
        result: {columns: ['SUM'], rows: [[3465]], row_count: 1}};
    };`);
  await app.run('runBenchmark()');
  assert.equal(app.run('state.measurements.length'), 2);
  assert.equal(app.run('state.benchmark.validPairs.length'), 1);
  assert.match(app.elements.get('benchmark-progress').textContent, /Detenido por el usuario/);

  /* El navegador oculto no inicia una serie: su pintado no es comparable. */
  app.run(`state.measurements = [];
    document.visibilityState = 'hidden';`);
  await app.run('runBenchmark()');
  assert.equal(app.run('state.measurements.length'), 0);
  assert.match(app.elements.get('benchmark-progress').textContent, /pestaña visible/);
  app.run("document.visibilityState = 'visible';");

  /* Si el probador cambia de delta, no se toman muestras. */
  app.run(`state.measurements = [];
    benchmarkStatus = {
      client: {direct_benchmark_enabled: true, state_id: 'delta-1',
        state_version: 2, plan: 'full', tables: [{name: 'student', rows: 500}]},
      prover: {reachable: true, matches: false, address: '127.0.0.1:7070'}
    };`);
  await app.run('runBenchmark()');
  assert.equal(app.run('state.measurements.length'), 0);
  assert.equal(app.run('state.benchmark.running'), false);
  assert.match(app.elements.get('benchmark-progress').textContent, /No se inició la serie/);
  assert.equal(app.elements.get('benchmark-run').disabled, false);

  app.run('state.measurements = []; updateMeasurementsToolbar()');
  assert.equal(app.elements.get('measurements-export').disabled, true);
  assert.equal(app.elements.get('measurements-count').textContent, '0 consultas registradas');
  process.stdout.write('OK: desglose, compatibilidad, CSV y registro local\n');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
