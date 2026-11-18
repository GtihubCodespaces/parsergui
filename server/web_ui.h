// Single-file studio UI. No build step, no external requests, no telemetry.
#pragma once

namespace pgr {

const char* kWebUiHtml =
R"HTML(<!doctype html>
<html lang="fr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>parsergui — screenshot vers ImGui</title>
<style>
  :root{
    --bg:#0d1015; --panel:#12161d; --panel-2:#161b24; --line:#242b36;
    --text:#e7eaf1; --dim:#8b94a6; --faint:#5d6675;
    --accent:#5e81f4; --accent-dim:#3f5cc0; --ok:#4ade80; --warn:#fbbf24; --err:#f87171;
    --radius:8px; --mono:ui-monospace,SFMono-Regular,"JetBrains Mono",Menlo,Consolas,monospace;
  }
  *{box-sizing:border-box}
  html,body{margin:0;height:100%}
  body{background:var(--bg);color:var(--text);font:13px/1.5 -apple-system,BlinkMacSystemFont,"Inter","Segoe UI",Roboto,sans-serif;-webkit-font-smoothing:antialiased}
  button,input,select,textarea{font:inherit;color:inherit}
  button{background:var(--panel-2);border:1px solid var(--line);border-radius:6px;padding:6px 11px;cursor:pointer}
  button:hover{border-color:#354154;background:#1a2029}
  button.primary{background:var(--accent);border-color:var(--accent);color:#fff}
  button.primary:hover{background:#6d8df8}
  button:disabled{opacity:.45;cursor:not-allowed}
  input,select,textarea{background:#0f131a;border:1px solid var(--line);border-radius:6px;padding:7px 9px;outline:none}
  input:focus,textarea:focus,select:focus{border-color:var(--accent)}
  a{color:var(--accent);text-decoration:none}
  code,pre{font-family:var(--mono);font-size:12px}
  header{height:50px;display:flex;align-items:center;gap:18px;padding:0 18px;border-bottom:1px solid var(--line);background:#0b0e13;position:sticky;top:0;z-index:10}
  .logo{display:flex;align-items:center;gap:9px;font-weight:600;letter-spacing:-.01em}
  .logo .mark{width:22px;height:22px;border-radius:5px;background:linear-gradient(180deg,#6d8df8,#3f5cc0);position:relative}
  .logo .mark::after{content:"";position:absolute;inset:6px 6px auto 6px;height:3px;background:#fff;opacity:.9;border-radius:2px}
  .logo .mark::before{content:"";position:absolute;inset:12px 8px 6px 6px;background:#fff;opacity:.55;border-radius:2px}
  .tag{font-size:11px;color:var(--dim);border:1px solid var(--line);padding:2px 7px;border-radius:20px}
  nav{display:flex;gap:4px;margin-left:auto}
  nav button{background:transparent;border:1px solid transparent;color:var(--dim);padding:5px 11px}
  nav button.active{color:var(--text);border-color:var(--line);background:var(--panel-2)}
  .health{display:flex;gap:14px;font-size:11px;color:var(--dim);margin-left:18px}
  .health b{color:var(--text);font-weight:500}
  main{padding:18px;max-width:1560px;margin:0 auto}
  .view{display:none}
  .view.active{display:block}
  .studio{display:grid;grid-template-columns:360px minmax(0,1fr);gap:18px;align-items:start}
  .col{display:flex;flex-direction:column;gap:14px;min-width:0}
  .card{background:var(--panel);border:1px solid var(--line);border-radius:var(--radius)}
  .card h2{font-size:11px;text-transform:uppercase;letter-spacing:.08em;color:var(--dim);margin:0;padding:12px 14px;border-bottom:1px solid var(--line);font-weight:600}
  .card .body{padding:14px}
  #drop{border:1px dashed #33405a;border-radius:var(--radius);padding:26px 16px;text-align:center;cursor:pointer;transition:border-color .15s,background .15s;background:#0f141b}
  #drop:hover,#drop.over{border-color:var(--accent);background:#111827}
  #drop .big{font-size:13px;font-weight:500;margin-bottom:4px}
  #drop .small{font-size:11px;color:var(--dim)}
  .options{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin:12px 0 0}
  .options label{display:flex;align-items:center;gap:6px;font-size:11px;color:var(--dim)}
  .options input[type=number]{width:62px;padding:3px 6px}
  .actions{display:flex;gap:8px;margin-top:12px}
  .actions button{flex:1}
  .queue{max-height:340px;overflow:auto}
  .job{display:block;width:100%;text-align:left;background:transparent;border:0;border-bottom:1px solid var(--line);border-radius:0;padding:10px 14px;cursor:pointer}
  .job:hover{background:#151a22}
  .job.active{background:#161d28;box-shadow:inset 2px 0 0 var(--accent)}
  .job .row{display:flex;align-items:center;gap:8px}
  .job .name{font-size:12px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;flex:1}
  .dot{width:6px;height:6px;border-radius:50%;background:var(--faint);flex:none}
  .dot.queued{background:var(--faint)}
  .dot.running{background:var(--accent);animation:pulse 1.1s infinite}
  .dot.done{background:var(--ok)}
  .dot.error{background:var(--err)}
  @keyframes pulse{50%{opacity:.35}}
  .bar{height:2px;background:#1d232d;margin-top:7px;border-radius:2px;overflow:hidden}
  .bar i{display:block;height:100%;background:var(--accent);width:0;transition:width .3s}
  .job .meta{font-size:10px;color:var(--faint);margin-top:4px;font-family:var(--mono)}
  .empty{padding:22px 14px;color:var(--faint);font-size:12px;text-align:center}
  .result-head{display:flex;align-items:center;gap:10px;flex-wrap:wrap;padding:12px 14px;border-bottom:1px solid var(--line)}
  .result-head .title{font-weight:600;margin-right:auto;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
  .chip{font-size:10px;border:1px solid var(--line);border-radius:20px;padding:2px 8px;color:var(--dim);font-family:var(--mono)}
  .chip.ok{color:var(--ok);border-color:#1f4632;background:#0f1d16}
  .chip.warn{color:var(--warn);border-color:#4a3a12;background:#1c1708}
  .tabs{display:flex;gap:2px;padding:0 10px;border-bottom:1px solid var(--line);background:#0f131a}
  .tabs button{background:transparent;border:0;border-bottom:2px solid transparent;border-radius:0;color:var(--dim);padding:9px 12px}
  .tabs button.active{color:var(--text);border-bottom-color:var(--accent)}
  .pane{display:none;padding:14px}
  .pane.active{display:block}
  .shots{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px}
  .shot{border:1px solid var(--line);border-radius:6px;overflow:hidden;background:#0a0d12}
  .shot .label{font-size:10px;color:var(--dim);padding:6px 8px;border-bottom:1px solid var(--line);font-family:var(--mono)}
  .shot img{display:block;width:100%;height:auto;image-rendering:pixelated}
  pre.code{margin:0;padding:12px;background:#0a0d12;border:1px solid var(--line);border-radius:6px;max-height:520px;overflow:auto;white-space:pre;line-height:1.45;color:#c8d0de}
  .table{width:100%;border-collapse:collapse;font-size:11px}
  .table th{text-align:left;color:var(--dim);font-weight:500;padding:6px;border-bottom:1px solid var(--line);position:sticky;top:0;background:var(--panel)}
  .table td{padding:5px 6px;border-bottom:1px solid #1b212b;font-family:var(--mono);white-space:nowrap}
  .table td.wrap{white-space:normal}
  .scroll{max-height:520px;overflow:auto}
  .metrics{display:grid;grid-template-columns:repeat(5,minmax(0,1fr));gap:8px;margin-bottom:14px}
  .metric{border:1px solid var(--line);border-radius:6px;padding:9px 10px;background:#0f131a}
  .metric .k{font-size:10px;color:var(--faint);text-transform:uppercase;letter-spacing:.06em}
)HTML"
R"HTML(  .metric .v{font-family:var(--mono);font-size:16px;margin-top:2px}
  .warnings{margin:0 0 12px;padding:9px 12px;border:1px solid #4a3a12;background:#1a1508;border-radius:6px;color:#e6cf8a;font-size:11px}
  .warnings div+div{margin-top:4px}
  .grid2{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:18px}
  .prow{display:flex;align-items:center;gap:10px;padding:8px 12px;border-bottom:1px solid var(--line)}
  .prow:last-child{border-bottom:0}
  .prow .pname{font-size:12px;min-width:170px}
  .prow .pcat{font-size:10px;color:var(--faint);font-family:var(--mono);min-width:90px}
  .prow .purl{font-size:10px;color:var(--faint);font-family:var(--mono);flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
  .prow .badge{font-size:9px;padding:1px 6px;border-radius:20px;border:1px solid #1f4632;color:var(--ok);background:#0f1d16}
  .prow .badge.key{color:var(--accent);border-color:#26365f;background:#0f1522}
  .filters{display:flex;gap:8px;align-items:center;padding:12px 14px;border-bottom:1px solid var(--line)}
  .filters input[type=search]{flex:1}
  .agent-form{display:grid;grid-template-columns:1fr 1fr;gap:10px}
  .agent-form .full{grid-column:1/-1}
  .agent-form label{display:block;font-size:10px;color:var(--faint);text-transform:uppercase;letter-spacing:.06em;margin-bottom:4px}
  .agent-form input,.agent-form select,.agent-form textarea{width:100%}
  .agent-form textarea{min-height:64px;resize:vertical}
  .log{background:#0a0d12;border:1px solid var(--line);border-radius:6px;padding:10px;height:260px;overflow:auto;font-family:var(--mono);font-size:11px;color:#a9b4c6;white-space:pre-wrap}
  .log div{border-bottom:1px solid #141922;padding:2px 0}
  .toast{position:fixed;right:16px;bottom:16px;background:#1b2130;border:1px solid #354154;padding:10px 14px;border-radius:8px;font-size:12px;max-width:420px;z-index:50;box-shadow:0 8px 24px rgba(0,0,0,.45)}
  .toast.err{border-color:#5a2626;background:#1d1214;color:#f3b4b4}
  .hint{font-size:11px;color:var(--faint);margin-top:6px}
  .linkbox{display:flex;gap:6px;align-items:center;margin-top:8px}
  .linkbox input{flex:1;font-family:var(--mono);font-size:11px}
  @media (max-width:1080px){.studio{grid-template-columns:1fr}.metrics{grid-template-columns:repeat(2,1fr)}.shots{grid-template-columns:1fr}.grid2{grid-template-columns:1fr}}
</style>
</head>
<body>
<header>
  <div class="logo"><span class="mark"></span>parsergui<span class="tag" id="version">v1.0</span></div>
  <nav>
    <button data-view="studio" class="active">Studio</button>
    <button data-view="providers">Providers</button>
    <button data-view="agent">Agent</button>
  </nav>
  <div class="health">
    <span><b id="h-jobs">0</b> jobs</span>
    <span><b id="h-providers">0</b>/500 providers</span>
    <span><b id="h-agents">0</b> CLI</span>
  </div>
</header>

<main>
  <section class="view active" id="view-studio">
    <div class="studio">
      <div class="col">
        <div class="card">
          <h2>Captures</h2>
          <div class="body">
            <div id="drop" tabindex="0">
              <div class="big">Déposez des captures</div>
              <div class="small">PNG, JPEG, WebP, BMP, TGA — illimité. Chaque image devient un job ; cochez Animations pour traiter la sélection comme les frames d'un même écran.</div>
              <input type="file" id="file" accept="image/*" multiple hidden>
            </div>
            <div class="options">
              <label>Couleurs <input type="number" id="opt-colors" min="4" max="64" value="24"></label>
              <label><input type="checkbox" id="opt-icons" checked> Icônes</label>
              <label><input type="checkbox" id="opt-auto-gui" checked> GUI auto</label>
              <label><input type="checkbox" id="opt-sequence"> Animations (frames liées)</label>
              <label><input type="checkbox" id="opt-strict"> Strict</label>
            </div>
            <div class="actions">
              <button class="primary" id="btn-upload" disabled>Analyser</button>
              <button id="btn-clear">Vider</button>
            </div>
            <div class="hint" id="drop-hint">0 fichier sélectionné</div>
          </div>
        </div>
        <div class="card">
          <h2>File d'attente</h2>
          <div class="queue" id="queue"><div class="empty">Aucun job.</div></div>
        </div>
      </div>
      <div class="col">
        <div class="card" id="result-card">
          <div class="result-head">
            <span class="title" id="r-title">Aucun résultat</span>
            <span class="chip" id="r-fidelity">—</span>
            <span class="chip" id="r-elements">—</span>
            <span class="chip" id="r-time">—</span>
            <span class="chip" id="r-grid">—</span>
          </div>
          <div class="tabs" id="result-tabs">
            <button data-pane="preview" class="active">Aperçu</button>
            <button data-pane="code">Code ImGui</button>
            <button data-pane="report">Rapport</button>
            <button data-pane="layout">Layout</button>
            <button data-pane="anim">Animations</button>
          </div>
          <div class="pane active" id="pane-preview"><div class="empty">Lancez une analyse.</div></div>
          <div class="pane" id="pane-code"><div class="empty">Pas de code.</div></div>
          <div class="pane" id="pane-report"><div class="empty">Pas de rapport.</div></div>
          <div class="pane" id="pane-layout"><div class="empty">Pas de layout.</div></div>
          <div class="pane" id="pane-anim"><div class="empty">Pas d'animations.</div></div>
        </div>
      </div>
    </div>
  </section>

  <section class="view" id="view-providers">
    <div class="card">
      <div class="filters">
        <input type="search" id="p-search" placeholder="Rechercher un provider, un modèle, une catégorie…">
        <label style="font-size:11px;color:var(--dim);display:flex;align-items:center;gap:6px"><input type="checkbox" id="p-free"> Free uniquement</label>
        <span class="chip" id="p-count">0</span>
      </div>
      <div class="scroll" id="p-list"><div class="empty">Chargement…</div></div>
    </div>
  </section>

  <section class="view" id="view-agent">
    <div class="grid2">
      <div class="card">
        <h2>Session agent</h2>
        <div class="body">
          <div class="agent-form">
            <div class="full">
              <label>Job</label>
              <select id="a-job"></select>
            </div>
            <div>
              <label>Agent</label>
              <select id="a-kind">
                <option value="claude">Claude Code (CLI)</option>
                <option value="codex">Codex CLI</option>
                <option value="opencode">OpenCode</option>
                <option value="provider">Provider HTTP</option>
                <option value="plan">Plan seulement</option>
              </select>
            </div>
            <div>
              <label>Provider</label>
)HTML"
R"HTML(              <select id="a-provider"></select>
            </div>
            <div>
              <label>Modèle</label>
              <input id="a-model" placeholder="vide = premier du provider">
            </div>
            <div>
              <label>Budget d'éditions</label>
              <input id="a-budget" type="number" min="10" max="500" value="150">
            </div>
            <div class="full">
              <label>Instruction</label>
              <textarea id="a-instructions" placeholder="Ex : garde la palette mesurée, reconstruis la sidebar en widgets enfants."></textarea>
            </div>
          </div>
          <div class="actions">
            <button id="a-plan">Planifier</button>
            <button class="primary" id="a-start">Lancer l'agent</button>
          </div>
          <div class="hint" id="a-hint">L'agent écrit uniquement parsed_ui.cpp. Budget et durée estimée sont affichés dans le journal.</div>
        </div>
      </div>
      <div class="card">
        <h2>Journal</h2>
        <div class="body">
          <div class="log" id="a-log"><div>Aucune session.</div></div>
          <div class="hint" id="a-status"></div>
        </div>
      </div>
    </div>
    <div class="card" style="margin-top:18px">
      <h2>Skills fournis aux agents</h2>
      <div class="scroll" id="skills"><div class="empty">Chargement…</div></div>
    </div>
  </section>
</main>

<script>
const $ = (id) => document.getElementById(id);
const state = { jobs: [], current: null, files: [], providers: [], agents: null, session: null, poll: null };
const esc = (s) => String(s ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));

async function api(path, options) {
  const res = await fetch(path, options);
  const type = res.headers.get('content-type') || '';
  const data = type.includes('json') ? await res.json() : await res.text();
  if (!res.ok) throw new Error((data && data.error) || data || res.statusText);
  return data;
}
function toast(msg, err) {
  const el = document.createElement('div');
  el.className = 'toast' + (err ? ' err' : '');
  el.textContent = msg;
  document.body.appendChild(el);
  setTimeout(() => el.remove(), 5200);
}
function fmtBytes(n) {
  if (!n) return '0 o';
  const u = ['o','Ko','Mo']; let i = 0; while (n >= 1024 && i < u.length-1) { n /= 1024; i++; }
  return n.toFixed(i ? 1 : 0) + ' ' + u[i];
}
function statusClass(s) {
  if (s === 'done') return 'done';
  if (s === 'error') return 'error';
  if (s === 'queued') return 'queued';
  return 'running';
}

/* ---------- navigation ---------- */
document.querySelectorAll('nav button').forEach(b => b.onclick = () => {
  document.querySelectorAll('nav button').forEach(x => x.classList.toggle('active', x === b));
  document.querySelectorAll('.view').forEach(v => v.classList.toggle('active', v.id === 'view-' + b.dataset.view));
  if (b.dataset.view === 'providers') loadProviders();
  if (b.dataset.view === 'agent') { loadAgents(); fillJobSelect(); }
});
document.querySelectorAll('#result-tabs button').forEach(b => b.onclick = () => {
  document.querySelectorAll('#result-tabs button').forEach(x => x.classList.toggle('active', x === b));
  document.querySelectorAll('.pane').forEach(p => p.classList.toggle('active', p.id === 'pane-' + b.dataset.pane));
});

/* ---------- upload ---------- */
const drop = $('drop');
drop.onclick = () => $('file').click();
drop.onkeydown = (e) => { if (e.key === 'Enter' || e.key === ' ') $('file').click(); };
drop.ondragover = (e) => { e.preventDefault(); drop.classList.add('over'); };
drop.ondragleave = () => drop.classList.remove('over');
drop.ondrop = (e) => {
  e.preventDefault(); drop.classList.remove('over');
  addFiles([...e.dataTransfer.files].filter(f => f.type.startsWith('image/')));
};
$('file').onchange = (e) => addFiles([...e.target.files]);
function addFiles(files) {
  state.files.push(...files);
  $('drop-hint').textContent = state.files.length + ' fichier' + (state.files.length > 1 ? 's' : '') + ' sélectionné' + (state.files.length > 1 ? 's' : '') + ' · ' + fmtBytes(state.files.reduce((a,f)=>a+f.size,0));
  $('btn-upload').disabled = state.files.length === 0;
}
$('btn-clear').onclick = () => { state.files = []; $('file').value = ''; $('drop-hint').textContent = '0 fichier sélectionné'; $('btn-upload').disabled = true; };
$('btn-upload').onclick = async () => {
  if (!state.files.length) return;
  const options = {
    max_colors: +$('opt-colors').value || 24,
    icons: $('opt-icons').checked,
    auto_gui: $('opt-auto-gui').checked,
    sequence: $('opt-sequence').checked
  };
  // Animations checked: one job holding every frame. Otherwise one job per image,
  // so an unlimited pile of unrelated screenshots is processed independently.
  const batches = options.sequence ? [state.files] : state.files.map(f => [f]);
  $('btn-upload').disabled = true;
  try {
    const ids = [];
    for (const batch of batches) {
      const fd = new FormData();
      batch.forEach(f => fd.append('files', f, f.name));
      fd.append('options', JSON.stringify(options));
      const res = await api('/api/jobs', { method: 'POST', body: fd });
      ids.push(res.id);
    }
    toast(ids.length > 1 ? ids.length + ' jobs lancés' : 'Job lancé : ' + ids[0]);
    state.files = []; $('file').value = ''; $('drop-hint').textContent = '0 fichier sélectionné'; $('btn-upload').disabled = true;
    await refreshJobs();
    if (ids[0]) selectJob(ids[0]);
  } catch (e) { toast('Upload : ' + e.message, true); $('btn-upload').disabled = false; }
};

/* ---------- jobs ---------- */
async function refreshJobs() {
  try {
    const data = await api('/api/jobs');
    state.jobs = data.jobs || [];
    $('h-jobs').textContent = state.jobs.length;
    renderQueue();
    fillJobSelect();
  } catch (e) { /* server restarting */ }
}
function renderQueue() {
  const q = $('queue');
  if (!state.jobs.length) { q.innerHTML = '<div class="empty">Aucun job.</div>'; return; }
  q.innerHTML = state.jobs.map(j => `
    <button class="job ${state.current && state.current.id === j.id ? 'active' : ''}" data-id="${esc(j.id)}">
      <div class="row"><span class="dot ${statusClass(j.status)}"></span><span class="name">${esc(j.name)}</span>
        <span class="meta">${j.status === 'done' ? Math.round(j.fidelity) + '/100' : esc(j.status)}</span></div>
      <div class="bar"><i style="width:${j.progress || 0}%"></i></div>
      <div class="meta">${esc(j.id)} · ${j.frames || 1} frame(s)${j.eta_seconds ? ' · ~' + j.eta_seconds + 's' : ''}</div>
    </button>`).join('');
  q.querySelectorAll('.job').forEach(b => b.onclick = () => selectJob(b.dataset.id));
}
async function selectJob(id) {
  try {
    const job = await api('/api/jobs/' + id);
    state.current = job;
    renderQueue();
    renderResult(job);
    if (job.status !== 'done' && job.status !== 'error') {
      clearTimeout(state.poll);
)HTML"
R"HTML(      state.poll = setTimeout(async () => { await refreshJobs(); selectJob(id); }, 700);
    }
  } catch (e) { toast('Job : ' + e.message, true); }
}
function renderResult(job) {
  $('r-title').textContent = job.name || job.id;
  const rep = job.report && job.report.report ? job.report.report : {};
  $('r-fidelity').textContent = job.status === 'done' ? 'fidélité ' + (rep.fidelity ?? 0).toFixed(1) + '/100' : esc(job.status);
  $('r-fidelity').className = 'chip ' + (rep.fidelity >= 90 ? 'ok' : rep.fidelity >= 70 ? 'warn' : '');
  $('r-elements').textContent = job.status === 'done' ? (rep.elements || 0) + ' éléments' : '·';
  $('r-time').textContent = job.status === 'done' ? Math.round(rep.elapsed_ms || 0) + ' ms' : '·';
  $('r-grid').textContent = job.status === 'done' && job.report.metrics ? 'grille ' + job.report.metrics.base_unit + 'px' : '·';

  if (job.status === 'error') {
    $('pane-preview').innerHTML = `<div class="warnings"><div>${esc(job.error || 'Erreur inconnue')}</div></div>`;
    return;
  }
  if (job.status !== 'done') {
    $('pane-preview').innerHTML = `<div class="empty">Analyse en cours… ${job.progress || 0}%</div>`;
    return;
  }
  const a = job.artifacts || {};
  $('pane-preview').innerHTML = `
    ${(job.warnings || []).length ? `<div class="warnings">${job.warnings.map(w => `<div>${esc(w)}</div>`).join('')}</div>` : ''}
    <div class="metrics">
      <div class="metric"><div class="k">Fidélité</div><div class="v">${(rep.fidelity ?? 0).toFixed(1)}</div></div>
      <div class="metric"><div class="k">Typographie</div><div class="v">${(rep.typography ?? 0).toFixed(1)}</div></div>
      <div class="metric"><div class="k">Éléments</div><div class="v">${rep.elements || 0}</div></div>
      <div class="metric"><div class="k">Textes</div><div class="v">${rep.text_runs || 0}</div></div>
      <div class="metric"><div class="k">Icônes</div><div class="v">${rep.icons || 0}</div></div>
    </div>
    <div class="shots">
      <div class="shot"><div class="label">source</div><img src="${esc(a.source)}" alt="source"></div>
      <div class="shot"><div class="label">reconstruction</div><img src="${esc(a.reconstruction)}" alt="reconstruction"></div>
      <div class="shot"><div class="label">diff</div><img src="${esc(a.diff)}" alt="diff"></div>
    </div>
    <div class="linkbox">
      <input readonly value="${location.origin}/api/jobs/${esc(job.id)}/artifact/parsed_ui.cpp" id="code-url">
      <button onclick="navigator.clipboard.writeText($('code-url').value);toast('URL copiée')">Copier l'URL</button>
    </div>`;

  renderCode(job);
  renderReport(job);
  renderLayout(job);
  renderAnimations(job);
}
async function renderCode(job) {
  try {
    const code = await api('/api/jobs/' + job.id + '/artifact/parsed_ui.cpp');
    $('pane-code').innerHTML = `<div class="actions" style="margin:0 0 10px"><button id="copy-code">Copier</button><a href="${esc(job.artifacts.code)}" download><button>Télécharger</button></a><a href="${esc(job.artifacts.main)}" download><button>main.cpp</button></a></div><pre class="code">${esc(code)}</pre>`;
    const btn = $('copy-code');
    if (btn) btn.onclick = () => navigator.clipboard.writeText(code).then(() => toast('Code copié'));
  } catch (e) { $('pane-code').innerHTML = `<div class="empty">${esc(e.message)}</div>`; }
}
function renderReport(job) {
  const rep = job.report || {};
  const report = rep.report || {};
  const palette = rep.palette || [];
  const elements = rep.elements || [];
  const rows = elements.filter(e => e.type !== 'Window').map(e => `
    <tr><td>${e.id}</td><td>${esc(e.type)}</td><td>${e.rect.join(',')}</td><td>${e.radius || '-'}</td>
    <td>${e.border && (e.border.left + e.border.top + e.border.right + e.border.bottom) ? e.border.top : '-'}</td>
    <td class="wrap">${esc(e.text_style ? e.text_style.text : '') || (e.icon ? esc(e.icon.name) : '') || '-'}</td>
    <td>${Math.round((e.confidence || 0) * 100)}%</td></tr>`).join('');
  $('pane-report').innerHTML = `
    <div class="metrics">
      <div class="metric"><div class="k">Panels</div><div class="v">${report.panels || 0}</div></div>
      <div class="metric"><div class="k">Boutons</div><div class="v">${report.buttons || 0}</div></div>
      <div class="metric"><div class="k">Inputs</div><div class="v">${report.inputs || 0}</div></div>
      <div class="metric"><div class="k">Bordures</div><div class="v">${report.borders || 0}</div></div>
      <div class="metric"><div class="k">Ombres</div><div class="v">${report.shadows || 0}</div></div>
    </div>
    <h3 style="font-size:11px;color:var(--dim);text-transform:uppercase;letter-spacing:.08em">Palette</h3>
    <div style="display:flex;flex-wrap:wrap;gap:6px;margin:8px 0 16px">
      ${palette.slice(0, 16).map(p => `<span class="chip" title="${esc(p.role)} · ${p.pixels} px"><span style="display:inline-block;width:9px;height:9px;border-radius:2px;background:${esc(p.color)};margin-right:5px"></span>${esc(p.color)} ${esc(p.role)}</span>`).join('')}
    </div>
    <h3 style="font-size:11px;color:var(--dim);text-transform:uppercase;letter-spacing:.08em">Éléments mesurés</h3>
    <div class="scroll"><table class="table"><thead><tr><th>#</th><th>type</th><th>x,y,w,h</th><th>rayon</th><th>bord</th><th>texte / icône</th><th>conf.</th></tr></thead><tbody>${rows}</tbody></table></div>`;
}
async function renderLayout(job) {
  try {
    const text = await api('/api/jobs/' + job.id + '/artifact/layout.txt');
    $('pane-layout').innerHTML = `<pre class="code">${esc(text)}</pre>`;
  } catch (e) { $('pane-layout').innerHTML = `<div class="empty">${esc(e.message)}</div>`; }
}
function renderAnimations(job) {
  const seq = job.report && job.report.sequence;
  if (!seq || !seq.tracks || !seq.tracks.length) {
    $('pane-anim').innerHTML = `<div class="empty">Aucune transition détectée. Déposez plusieurs frames du même écran, nommées t=&lt;ms&gt; pour mesurer la durée.</div>`;
    return;
  }
  $('pane-anim').innerHTML = `
    <div class="hint" style="margin-bottom:10px">${seq.frames} frames, delta moyen ${(seq.mean_delta*100).toFixed(2)}%</div>
    <table class="table"><thead><tr><th>élément</th><th>propriété</th><th>de</th><th>à</th><th>durée</th><th>easing</th><th>conf.</th></tr></thead><tbody>
    ${seq.tracks.map(t => `<tr><td>${t.element}</td><td>${esc(t.property)}</td><td>${t.from}</td><td>${t.to}</td><td>${t.duration_ms} ms</td><td>${esc(t.easing)}</td><td>${Math.round((t.confidence||0)*100)}%</td></tr>`).join('')}
    </tbody></table>`;
}

/* ---------- providers ---------- */
async function loadProviders() {
  try {
    const free = $('p-free').checked ? '1' : '0';
    const q = encodeURIComponent($('p-search').value || '');
    const data = await api(`/api/providers?free=${free}&q=${q}`);
    state.providers = data.providers || [];
    $('h-providers').textContent = data.count;
    $('p-count').textContent = data.count + ' / ' + data.capacity;
    $('p-list').innerHTML = state.providers.map(p => `
)HTML"
R"HTML(      <div class="prow" data-id="${esc(p.id)}">
        <span class="pname">${esc(p.name)}</span>
        <span class="pcat">${esc(p.category)}</span>
        <span class="purl">${esc(p.base_url)}</span>
        ${p.free_tier ? '<span class="badge">free</span>' : ''}
        ${p.key_present ? '<span class="badge key">key</span>' : ''}
        <span class="chip">${p.reachable ? 'HTTP ' + p.last_status : (p.last_test ? esc(p.last_test) : 'non testé')}</span>
        <button data-test="${esc(p.id)}">Tester</button>
      </div>`).join('') || '<div class="empty">Aucun provider.</div>';
    $('p-list').querySelectorAll('[data-test]').forEach(b => b.onclick = async () => {
      b.disabled = true; b.textContent = '…';
      try { const r = await api('/api/providers/' + encodeURIComponent(b.dataset.test) + '/test', { method: 'POST' }); toast((r.ok ? 'OK ' : 'Échec ') + (r.status || '') + (r.error ? ' · ' + r.error : ''), !r.ok); }
      catch (e) { toast(e.message, true); }
      b.disabled = false; b.textContent = 'Tester';
      loadProviders();
    });
  } catch (e) { $('p-list').innerHTML = `<div class="empty">${esc(e.message)}</div>`; }
}
$('p-search').oninput = () => { clearTimeout(window._pt); window._pt = setTimeout(loadProviders, 180); };
$('p-free').onchange = loadProviders;

/* ---------- agent ---------- */
async function loadAgents() {
  try {
    const data = await api('/api/agents');
    state.agents = data;
    $('h-agents').textContent = (data.cli || []).filter(c => c.installed).length;
    const sel = $('a-kind');
    [...sel.options].forEach(o => {
      if (['claude','codex','opencode'].includes(o.value)) {
        const cli = (data.cli || []).find(c => c.id === o.value);
        o.textContent = o.textContent.replace(/ \(non installé\)$/, '') + (cli && !cli.installed ? ' (non installé)' : '');
      }
    });
    const prov = await api('/api/providers?free=0&q=');
    $('a-provider').innerHTML = (prov.providers || []).map(p => `<option value="${esc(p.id)}">${esc(p.name)}${p.free_tier ? ' · free' : ''}</option>`).join('');
    $('skills').innerHTML = (data.skills || []).map(s => `<div class="prow"><span class="pname">${esc(s.name)}</span><span class="purl">${esc((s.preview || '').slice(0, 140))}</span><span class="chip">${Math.round(s.bytes)} o</span></div>`).join('') || '<div class="empty">Aucun skill dans ./skills.</div>';
  } catch (e) { toast(e.message, true); }
}
function fillJobSelect() {
  const sel = $('a-job');
  const done = state.jobs.filter(j => j.status === 'done');
  const value = sel.value;
  sel.innerHTML = done.map(j => `<option value="${esc(j.id)}">${esc(j.name)} · ${Math.round(j.fidelity)}/100</option>`).join('') || '<option value="">Aucun job terminé</option>';
  if (value && done.some(j => j.id === value)) sel.value = value;
}
async function startAgent(dry) {
  const jobId = $('a-job').value;
  if (!jobId) { toast('Aucun job terminé', true); return; }
  const kind = $('a-kind').value;
  const payload = {
    job_id: jobId, agent: kind, dry_run: !!dry,
    provider_id: $('a-provider').value, model: $('a-model').value,
    budget: +$('a-budget').value || 150,
    instructions: $('a-instructions').value
  };
  try {
    const res = await api('/api/agents/session', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(payload) });
    state.session = res.id;
    $('a-status').textContent = 'session ' + res.id + ' · estimation ' + res.eta_seconds + 's';
    toast('Session ' + res.id + ' lancée');
    pollSession();
  } catch (e) { toast('Agent : ' + e.message, true); }
}
$('a-start').onclick = () => startAgent(false);
$('a-plan').onclick = () => startAgent(true);
const sleep = (ms) => new Promise(r => setTimeout(r, ms));
async function pollSession() {
  if (!state.session) return;
  try {
    const s = await api('/api/agents/session/' + state.session);
    $('a-log').innerHTML = (s.log || []).map(l => `<div>${esc(l)}</div>`).join('') || '<div>En attente…</div>';
    $('a-log').scrollTop = $('a-log').scrollHeight;
    $('a-status').textContent = `${s.status} · ${s.edits}/${s.budget} éditions · ${s.changed_lines} lignes modifiées · estimé ${s.eta_seconds}s`;
    if (s.status === 'running') { await sleep(800); pollSession(); }
    else if (s.status === 'done' || s.status === 'budget_exhausted') {
      toast('Session terminée : ' + s.changed_lines + ' lignes modifiées');
      if (state.current) selectJob(state.current.id);
    } else if (s.status === 'error') toast(s.error || 'Session en erreur', true);
  } catch (e) { /* ignore */ }
}

/* ---------- boot ---------- */
(async () => {
  try {
    const h = await api('/api/health');
    $('version').textContent = h.version || 'v1.0';
    $('h-providers').textContent = h.providers || 0;
    $('h-agents').textContent = h.cli || 0;
  } catch (e) {}
  await refreshJobs();
  setInterval(() => { if (!$('view-studio').classList.contains('active') || !state.current) refreshJobs(); }, 2000);
})();
</script>
</body>
</html>
)HTML";

}  // namespace pgr
