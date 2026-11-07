package main

const DisksPage = `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Centipede Disks</title>
<style>
  :root { --bg: #222; --panel: #2c2c2c; --line: #444; --text: #eee; --dim: #999; --accent: #4c9; --warn: #e94; }
  * { box-sizing: border-box; }
  body { margin: 0; padding: 16px; background: var(--bg); color: var(--text); font-family: monospace; }
  h1 { font-size: 1.2em; margin: 0 0 12px; }
  .tabs { display: flex; gap: 6px; margin-bottom: 10px; }
  .tabs button { background: var(--panel); color: var(--text); border: 1px solid var(--line); padding: 6px 14px; border-radius: 4px; font: inherit; cursor: pointer; }
  .tabs button.on { border-color: var(--accent); color: var(--accent); }
  table { border-collapse: collapse; width: 100%; max-width: 900px; }
  td { border-bottom: 1px solid var(--line); padding: 6px 8px; vertical-align: top; }
  tr.slot { cursor: pointer; }
  tr.slot.sel { background: #33443c; }
  .drive { width: 5em; white-space: nowrap; }
  .pending { color: var(--warn); }
  .dim { color: var(--dim); }
  button.small { background: var(--panel); color: var(--text); border: 1px solid var(--line); border-radius: 3px; font: inherit; padding: 2px 8px; cursor: pointer; }
  #apply { margin-top: 10px; background: #533; color: #fff; border: 1px solid var(--warn); border-radius: 4px; font: inherit; padding: 6px 14px; cursor: pointer; }
  #apply:disabled { background: var(--panel); color: var(--dim); border-color: var(--line); cursor: not-allowed; }
  #why { margin-left: 10px; color: var(--warn); }
  .top { display: flex; align-items: baseline; gap: 16px; margin: 0 0 12px; }
  .top h1 { margin: 0; }
  .top a { color: #8cf; }
  #power { padding: 2px 8px; border: 1px solid var(--line); border-radius: 3px; }
  #power.on { color: var(--accent); border-color: var(--accent); }
  #power.off { color: var(--warn); border-color: var(--warn); }
  #status { margin: 10px 0; color: var(--accent); min-height: 1.2em; max-width: 900px; }
  #where { margin: 16px 0 6px; word-break: break-all; }
  #files { max-width: 900px; max-height: 50vh; overflow-y: auto; border: 1px solid var(--line); }
  #files div { padding: 4px 8px; border-bottom: 1px solid #333; cursor: pointer; display: flex; justify-content: space-between; gap: 12px; }
  #files div:hover { background: #333; }
  .dir { color: #8cf; }
  .hint { color: var(--dim); margin: 6px 0; max-width: 900px; }
  .mark { margin-left: 4px; color: var(--dim); cursor: help; }
</style>
</head>
<body>
<div class="top">
  <h1>Centipede Disks</h1>
  <span id="power" class="dim"></span>
  <a href="/">Back to the screen</a>
</div>
<div class="tabs">
  <button id="tabRemote" type="button">Remote (PC)</button>
  <button id="tabOnboard" type="button">On-board (flash)</button>
</div>
<div class="hint" id="hint"></div>
<table id="slots"></table>
<div class="hint" id="legend" hidden><span class="mark">*</span> Already on the flash at startup, so its image name is unknown. Shown by its only or largest file.</div>
<div><button id="apply" type="button" hidden>Copy staged disks to flash</button><span id="why"></span></div>
<div id="status"></div>
<div id="where"></div>
<div id="files"></div>

<script>
let kind = 'remote';
try { kind = localStorage.getItem('diskKind') || 'remote'; } catch (e) {}
let selected = 0;
let dir = '';
let state = null;

const HINTS = {
  remote: 'Pick a drive, then click a disk image. It takes effect at the next disk access, with no restart. Type DIR after swapping.',
  onboard: 'Pick a drive, then click a disk image to stage it. Nothing changes until you copy, which restarts the CoCo.',
  cocoOff: 'The CoCo is off. Turn it on to see or change the on-board drives.'
};
function cocoOff() { return state && state.connected && state.cocoKnown && !state.cocoOn; }

function el(tag, cls, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;
  return e;
}

async function post(url, body) {
  const r = await fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body || {}) });
  if (!r.ok) throw new Error(await r.text());
  return r.json();
}

function render() {
  if (!state) return;
  document.getElementById('tabRemote').classList.toggle('on', kind === 'remote');
  document.getElementById('tabOnboard').classList.toggle('on', kind === 'onboard');
  document.getElementById('hint').textContent = (kind === 'onboard' && cocoOff()) ? HINTS.cocoOff : HINTS[kind];

  const table = document.getElementById('slots');
  table.replaceChildren();
  const rows = state[kind];
  let staged = 0;
  let marked = false;
  rows.forEach((s, i) => {
    const tr = el('tr', 'slot' + (i === selected ? ' sel' : ''));
    tr.addEventListener('click', () => { selected = i; render(); });
    tr.appendChild(el('td', 'drive', 'Drive ' + i));
    const cur = el('td');
    if (kind === 'onboard') {
      if (s.drive && s.drive.size === 0) cur.appendChild(el('div', 'dim', 'empty'));
      else if (s.shown) cur.appendChild(el('div', '', s.shown));
      else if (!s.drive) cur.appendChild(el('div', 'dim', 'not read yet'));
      else {
        const d = el('div', '', s.drive.mainFile || ('unknown disk image, ' + Math.round(s.drive.size / 1024) + 'K'));
        const mark = el('span', 'mark', '*');
        mark.title = 'Already on the flash at startup';
        d.appendChild(mark);
        marked = true;
        cur.appendChild(d);
      }
      if (s.pending) { staged++; cur.appendChild(el('div', 'pending', 'staged: ' + s.pending)); }
    } else {
      cur.appendChild(el('div', '', s.shown));
    }
    tr.appendChild(cur);
    const act = el('td');
    const label = kind === 'onboard' ? 'Unstage' : 'Reset';
    if (kind === 'remote' || s.pending) {
      const b = el('button', 'small', label);
      b.type = 'button';
      b.addEventListener('click', (e) => { e.stopPropagation(); assign(i, ''); });
      act.appendChild(b);
    }
    tr.appendChild(act);
    table.appendChild(tr);
  });

  document.getElementById('legend').hidden = !(kind === 'onboard' && marked);
  const apply = document.getElementById('apply');
  apply.hidden = kind !== 'onboard';
  apply.disabled = staged === 0 || state.busy || !state.connected || !state.cocoOn;
  apply.textContent = staged ? 'Copy ' + staged + ' staged disk' + (staged > 1 ? 's' : '') + ' to flash' : 'Copy staged disks to flash';
  let why = '';
  if (state.busy) why = 'Working...';
  else if (!state.connected) why = 'The board is not connected.';
  else if (!state.cocoOn) why = 'Turn the CoCo on to copy.';
  else if (staged === 0) why = 'Stage a disk first.';
  document.getElementById('why').textContent = kind === 'onboard' ? why : '';

  const power = document.getElementById('power');
  let powerText = 'CoCo: checking...', powerClass = 'dim';
  if (!state.connected) [powerText, powerClass] = ['Board not connected', 'off'];
  else if (state.busy) [powerText, powerClass] = ['CoCo: busy', 'dim'];
  else if (state.cocoKnown) [powerText, powerClass] = state.cocoOn ? ['CoCo is on', 'on'] : ['CoCo is off', 'off'];
  power.textContent = powerText;
  power.className = powerClass;
  let status = state.status || '';
  if (!state.connected) status = 'The board is not connected.';
  document.getElementById('status').textContent = status;
}

async function refresh() {
  try {
    const r = await fetch('/disks/state');
    state = await r.json();
    render();
  } catch (e) {
    document.getElementById('status').textContent = 'Lost contact with the tether.';
  }
}

async function browse(d) {
  try {
    const r = await fetch('/disks/list?dir=' + encodeURIComponent(d));
    if (!r.ok) throw new Error(await r.text());
    const j = await r.json();
    dir = j.dir;
    const where = document.getElementById('where');
    where.textContent = dir;
    const files = document.getElementById('files');
    files.replaceChildren();
    if (dir !== '/') {
      const up = el('div', 'dir');
      up.appendChild(el('span', '', '.. (up)'));
      up.addEventListener('click', () => browse(dir.replace(/\/[^\/]*$/, '') || '/'));
      files.appendChild(up);
    }
    j.entries.forEach(ent => {
      const row = el('div', ent.dir ? 'dir' : '');
      row.appendChild(el('span', '', ent.name + (ent.dir ? '/' : '')));
      if (!ent.dir) row.appendChild(el('span', 'dim', Math.round(ent.size / 1024) + 'K'));
      const path = (dir === '/' ? '' : dir) + '/' + ent.name;
      row.addEventListener('click', () => ent.dir ? browse(path) : assign(selected, path));
      files.appendChild(row);
    });
    try { localStorage.setItem('diskDir', dir); } catch (e) {}
  } catch (e) {
    document.getElementById('status').textContent = 'Cannot open ' + d + ': ' + e.message;
    if (d !== '') browse('');
  }
}

async function assign(slot, path) {
  if (kind === 'onboard' && path && cocoOff()) {
    document.getElementById('status').textContent = HINTS.cocoOff;
    return;
  }
  try {
    state = await post('/disks/assign', { kind: kind, slot: slot, path: path });
    render();
  } catch (e) {
    document.getElementById('status').textContent = e.message;
  }
}

function setKind(k) {
  kind = k;
  try { localStorage.setItem('diskKind', k); } catch (e) {}
  render();
}

document.getElementById('tabRemote').addEventListener('click', () => setKind('remote'));
document.getElementById('tabOnboard').addEventListener('click', () => setKind('onboard'));
document.getElementById('apply').addEventListener('click', async () => {
  const n = state.onboard.filter(s => s.pending).length;
  if (!confirm('This restarts the CoCo, and anything running on it is lost.\n\nCopy ' + n + ' staged disk image(s) into the board\'s flash, then return to the menu?')) return;
  try { state = await post('/disks/apply'); render(); } catch (e) { document.getElementById('status').textContent = e.message; }
});

let startDir = '';
try { startDir = localStorage.getItem('diskDir') || ''; } catch (e) {}
refresh().then(() => browse(startDir));
setInterval(refresh, 1500);
</script>
</body>
</html>
`
