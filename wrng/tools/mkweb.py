#!/usr/bin/env python3
"""Build wrng.html: one web page, with the kit (wrng-kit.zip) and tools/wrng_build.py inside it, that turns the
user's World Rally ROMs into wrng.neo in the browser (Python runs there through Pyodide, loaded from its CDN;
the ROMs never leave the computer).  Open the file, drop wrally.zip on it, press the button.

usage: mkweb.py <wrng-kit.zip> <wrng_build.py> <out.html>
"""
import base64
import sys

PYODIDE = 'https://cdn.jsdelivr.net/pyodide/v0.26.4/full/'

PAGE = r'''<!doctype html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>World Rally NG</title>
<style>
:root { --bg:#f6f5f2; --card:#ffffff; --text:#1d1d1f; --muted:#6b6b70; --line:#d9d7d1; --accent:#c8102e;
        --ok:#1b7f3b; --err:#b3261e; --drop:#fbfaf7; }
@media (prefers-color-scheme: dark) {
  :root { --bg:#141416; --card:#1e1e21; --text:#ececee; --muted:#9a9aa2; --line:#36363b; --accent:#ff4d5e;
          --ok:#4cc27a; --err:#ff6b60; --drop:#19191c; } }
* { box-sizing:border-box; }
body { margin:0; background:var(--bg); color:var(--text); font:16px/1.5 system-ui, sans-serif; }
main { max-width:640px; margin:0 auto; padding:32px 16px 48px; }
h1 { font-size:28px; margin:0 0 4px; } h1 span { color:var(--accent); }
.sub { color:var(--muted); margin:0 0 24px; }
.card { background:var(--card); border:1px solid var(--line); border-radius:12px; padding:20px; margin-bottom:16px; }
.step { font-weight:600; margin:0 0 8px; }
#drop { border:2px dashed var(--line); border-radius:10px; background:var(--drop); padding:28px 12px; text-align:center;
        cursor:pointer; color:var(--muted); }
#drop.over { border-color:var(--accent); color:var(--text); }
#files { margin:10px 0 0; padding:0; list-style:none; font-size:14px; }
#files li { padding:2px 0; overflow-wrap:anywhere; }
label.opt { display:flex; gap:8px; align-items:flex-start; margin:12px 0 0; font-size:14px; color:var(--muted); }
button { font:inherit; font-weight:600; background:var(--accent); color:#fff; border:0; border-radius:8px;
         padding:12px 20px; cursor:pointer; width:100%; }
button:disabled { opacity:.45; cursor:default; }
#status { margin:12px 0 0; min-height:1.5em; }
#status.ok { color:var(--ok); } #status.err { color:var(--err); }
#log { margin:12px 0 0; max-height:180px; overflow:auto; font:12px/1.45 ui-monospace, monospace; color:var(--muted);
       white-space:pre-wrap; display:none; }
#links a { display:block; margin-top:10px; text-align:center; background:var(--ok); color:#fff; padding:12px;
           border-radius:8px; text-decoration:none; font-weight:600; }
.note { font-size:13px; color:var(--muted); }
</style>
</head>
<body>
<main>
<h1>World Rally <span>NG</span></h1>
<p class="sub">Genera el archivo <b>wrng.neo</b> para NeoSD a partir de tu ROM de World Rally de MAME.</p>

<div class="card">
  <p class="step">1. Arrastra aquí tu ROM</p>
  <div id="drop" tabindex="0">Suelta <b>wrally.zip</b> aquí (y <b>wrallyd.zip</b> si lo tienes aparte)<br>
    o haz clic para elegir los archivos</div>
  <input id="pick" type="file" accept=".zip,.bin,.c01,.c03,.c22,.c23,.i07,.i09,.i11,.i13" multiple hidden>
  <ul id="files"></ul>
  <label class="opt"><input id="mame" type="checkbox"> Generar también el set para MAME (wrng.zip + neogeo.xml)</label>
</div>

<div class="card">
  <p class="step">2. Genera el archivo</p>
  <button id="go" disabled>Generar wrng.neo</button>
  <div id="status"></div>
  <div id="links"></div>
  <pre id="log"></pre>
</div>

<p class="note">Todo ocurre en tu navegador: los archivos no se suben a ningún sitio. La primera vez descarga unos
15 MB (Python para el navegador) y tarda uno o dos minutos. No hace falta la BIOS de Neo Geo: el NeoSD usa la de
la consola. Copia <b>wrng.neo</b> a la tarjeta SD del NeoSD.</p>
</main>

<script id="kit" type="application/octet-stream">@KIT@</script>
<script id="tool" type="text/plain">@TOOL@</script>
<script id="worker" type="text/plain">
self.onmessage = async (e) => {
  const { base, kit, tool, files, mame } = e.data;
  const log = (s) => self.postMessage({ log: s });
  try {
    importScripts(base + 'pyodide.js');
    log('Cargando Python...');
    const py = await loadPyodide({ indexURL: base, stdout: log, stderr: log });
    py.FS.mkdirTree('/w/in');
    py.FS.writeFile('/w/wrng-kit.zip', kit);
    py.FS.writeFile('/w/wrng_build.py', tool);
    files.forEach((f, i) => py.FS.writeFile('/w/in/' + i + '_' + f.name.replace(/[\/\\]/g, '_'), f.data));
    py.globals.set('want_mame', mame);
    const rc = py.runPython(`
import sys, runpy
sys.argv = ['wrng_build.py', '/w/wrng-kit.zip', '/w/in', '-o', '/w/wrng.neo'] + (['--mame', '/w/wrng.zip'] if want_mame else [])
rc = ''
try:
    runpy.run_path('/w/wrng_build.py', run_name='__main__')
except SystemExit as e:
    if e.code not in (None, 0):
        rc = str(e.code)
rc`);
    if (rc) { self.postMessage({ error: rc }); return; }
    const out = { neo: py.FS.readFile('/w/wrng.neo') };
    if (mame) { out.zip = py.FS.readFile('/w/wrng.zip'); out.xml = py.FS.readFile('/w/neogeo.xml'); }
    self.postMessage({ done: out });
  } catch (err) {
    self.postMessage({ error: String(err && err.message || err) });
  }
};
</script>
<script>
const params = new URLSearchParams(location.search);
const BASE = params.get('pyodide') || '@PYODIDE@';
const $ = (id) => document.getElementById(id);
let chosen = [];

function b64(id) {
  const s = atob($(id).textContent.trim());
  const a = new Uint8Array(s.length);
  for (let i = 0; i < s.length; i++) a[i] = s.charCodeAt(i);
  return a;
}
function showFiles() {
  $('files').innerHTML = '';
  chosen.forEach((f) => { const li = document.createElement('li'); li.textContent = '• ' + f.name; $('files').appendChild(li); });
  $('go').disabled = chosen.length === 0;
}
function add(list) {
  for (const f of list) if (!chosen.some((c) => c.name === f.name && c.size === f.size)) chosen.push(f);
  showFiles();
}
function status(text, cls) { $('status').textContent = text; $('status').className = cls || ''; }
function link(name, data, type) {
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([data], { type }));
  a.download = name;
  a.textContent = 'Descargar ' + name;
  $('links').appendChild(a);
  return a;
}

const drop = $('drop');
drop.onclick = () => $('pick').click();
drop.onkeydown = (e) => { if (e.key === 'Enter' || e.key === ' ') $('pick').click(); };
$('pick').onchange = (e) => add(e.target.files);
drop.ondragover = (e) => { e.preventDefault(); drop.classList.add('over'); };
drop.ondragleave = () => drop.classList.remove('over');
drop.ondrop = (e) => { e.preventDefault(); drop.classList.remove('over'); add(e.dataTransfer.files); };

$('go').onclick = async () => {
  $('go').disabled = true;
  $('links').innerHTML = '';
  $('log').textContent = '';
  $('log').style.display = 'block';
  status('Trabajando... (puede tardar uno o dos minutos)');
  const files = await Promise.all(chosen.map(async (f) => ({ name: f.name, data: new Uint8Array(await f.arrayBuffer()) })));
  const src = $('worker').textContent;
  const w = new Worker(URL.createObjectURL(new Blob([src], { type: 'text/javascript' })));
  w.onmessage = (e) => {
    const m = e.data;
    if (m.log !== undefined) { $('log').textContent += m.log + '\n'; $('log').scrollTop = 1e9; return; }
    w.terminate();
    $('go').disabled = false;
    if (m.error) {
      status('No se ha podido generar: ' + m.error.trim().split('\n').pop(), 'err');
      return;
    }
    status('¡Listo! Copia wrng.neo a la tarjeta SD de tu NeoSD.', 'ok');
    link('wrng.neo', m.done.neo, 'application/octet-stream').click();
    if (m.done.zip) { link('wrng.zip', m.done.zip, 'application/zip'); link('neogeo.xml', m.done.xml, 'text/xml'); }
  };
  w.onerror = (e) => { $('go').disabled = false; status('Error: ' + (e.message || 'no se pudo iniciar'), 'err'); };
  w.postMessage({ base: BASE, kit: b64('kit'), tool: $('tool').textContent, files, mame: $('mame').checked });
};
</script>
</body>
</html>
'''


def main(kit, tool, out):
    html = (PAGE.replace('@KIT@', base64.b64encode(open(kit, 'rb').read()).decode())
                .replace('@TOOL@', open(tool).read().replace('</script', '<\\/script'))
                .replace('@PYODIDE@', PYODIDE))
    open(out, 'w', encoding='utf-8').write(html)
    print('wrote', out)


if __name__ == '__main__':
    main(*sys.argv[1:4])
