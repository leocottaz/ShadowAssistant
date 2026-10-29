// Routines : suites d'étapes définies dans les réglages (ouvrir, volume, musique, attendre),
// lancées depuis les réglages ou par Claude (« lance le mode travail »). L'exécution se fait
// côté natif (pc::RunRoutine) : elle continue quand le dock se range et que la page dort.
import { $, $$, uid, esc, icon, store, toast } from '../core/util.js';
import { ask, send } from '../core/bridge.js';

let routines = store.get('routines', []);
const STEP_TYPES = { open: 'Ouvrir', volume: 'Volume', music: 'Musique', wait: 'Attendre' };
const MUSIC = { play: 'Lecture', pause: 'Pause', next: 'Morceau suivant' };
const DEFAULTS = { open: '', volume: 20, music: 'play', wait: 3 };

const norm = (s) => s.normalize('NFD').replace(/[̀-ͯ]/g, '').toLowerCase().replace(/^(le |la |l'|mode |routine )+/g, '').trim();
let saveTimer;
const save = () => { clearTimeout(saveTimer); saveTimer = setTimeout(() => store.set('routines', routines), 250); };

// Par nom, sans casse ni accents (« mode travail », « travail ») : identique, sinon la seule qui contient.
function findRoutine(name) {
  const q = norm(name || '');
  if (!q) return null;
  const near = routines.filter((r) => norm(r.name).includes(q) || q.includes(norm(r.name)));
  return routines.find((r) => norm(r.name) === q) || (near.length === 1 ? near[0] : null);
}

function runRoutine(r) {
  const lines = r.steps
    .filter((s) => s.type !== 'open' || String(s.value).trim())
    .map((s) => `${s.type}\t${String(s.value).replace(/[\t\r\n]+/g, ' ').trim()}`);
  if (!lines.length) return 0;
  send('routine', lines.join('\n'));
  return lines.length;
}

const routineNames = () => routines.map((r) => r.name.trim()).filter(Boolean);

// ------------------------------------------------------------------ éditeur (Réglages)

function stepHTML(s, i) {
  const opts = (map, cur) => Object.entries(map).map(([k, v]) => `<option value="${k}"${k === cur ? ' selected' : ''}>${v}</option>`).join('');
  let control = '';
  if (s.type === 'open') {
    control = `<input class="step-value" value="${esc(s.value)}" placeholder="Appli, dossier, fichier ou https://…" spellcheck="false">
      <button type="button" class="ghost step-pick" data-act="pick" title="Choisir un fichier">Parcourir…</button>`;
  } else if (s.type === 'volume') {
    control = `<input class="step-value" type="range" min="0" max="100" step="5" value="${+s.value || 0}" style="--v:${+s.value || 0}%"><output>${+s.value || 0} %</output>`;
  } else if (s.type === 'music') {
    control = `<select class="step-value">${opts(MUSIC, s.value)}</select>`;
  } else {
    control = `<input class="step-value step-secs" type="number" min="0" max="60" value="${+s.value || 0}"><span class="hint">secondes</span>`;
  }
  return `<div class="step" data-i="${i}">
    <select class="step-type">${opts(STEP_TYPES, s.type)}</select>
    <div class="step-control">${control}</div>
    <button type="button" class="icon-btn" data-act="del-step" title="Retirer l'étape">${icon('x')}</button>
  </div>`;
}

function renderRoutines() {
  const box = $('#routines');
  box.innerHTML = routines.map((r) => `
    <div class="routine" data-id="${r.id}">
      <div class="routine-head">
        <input class="routine-name" value="${esc(r.name)}" placeholder="Nom de la routine" spellcheck="false">
        <button type="button" class="primary" data-act="run" title="Lancer la routine">${icon('play')}Lancer</button>
        <button type="button" class="icon-btn del" data-act="del" title="Supprimer la routine">${icon('trash')}</button>
      </div>
      <div class="steps">${r.steps.map(stepHTML).join('')}</div>
      <button type="button" class="ghost add-step" data-act="add-step">${icon('plus')}Étape</button>
    </div>`).join('')
    || '<p class="hint routines-empty">Aucune routine pour l’instant.</p>';
}

function setupRoutines() {
  const box = $('#routines');
  const at = (e) => {
    const el = e.target.closest('.routine');
    const r = routines.find((x) => x.id === el?.dataset.id);
    const stepEl = e.target.closest('.step');
    return { el, r, i: stepEl ? +stepEl.dataset.i : -1, stepEl };
  };

  $('#addRoutine').addEventListener('click', () => {
    routines.push({ id: uid(), name: routines.length ? 'Nouvelle routine' : 'Mode travail', steps: [{ type: 'open', value: '' }, { type: 'volume', value: 20 }] });
    save();
    renderRoutines();
    $$('.routine-name', box).pop()?.select();
  });

  box.addEventListener('input', (e) => {
    const { r, i, stepEl } = at(e);
    if (!r) return;
    if (e.target.classList.contains('routine-name')) r.name = e.target.value;
    else if (e.target.classList.contains('step-value') && i >= 0) {
      const s = r.steps[i];
      s.value = s.type === 'open' || s.type === 'music' ? e.target.value : +e.target.value;
      if (s.type === 'volume') {
        e.target.style.setProperty('--v', `${s.value}%`);
        $('output', stepEl).textContent = `${s.value} %`;
      }
    }
    save();
  });

  box.addEventListener('change', (e) => {
    const { r, i } = at(e);
    if (!r || i < 0) return;
    if (e.target.classList.contains('step-type')) {
      r.steps[i] = { type: e.target.value, value: DEFAULTS[e.target.value] };
      save();
      renderRoutines();
    } else if (e.target.tagName === 'SELECT') { // menu « Musique »
      r.steps[i].value = e.target.value;
      save();
    }
  });

  box.addEventListener('click', async (e) => {
    const btn = e.target.closest('[data-act]');
    const { el, r, i } = at(e);
    if (!btn || !r) return;
    const act = btn.dataset.act;
    if (act === 'run') {
      const n = runRoutine(r);
      toast(n ? `Routine « ${r.name} » lancée` : 'Cette routine n’a aucune étape');
    } else if (act === 'del') {
      routines = routines.filter((x) => x !== r);
      save();
      el.classList.add('leaving');
      setTimeout(renderRoutines, document.body.classList.contains('reduce') ? 0 : 300);
    } else if (act === 'add-step') {
      r.steps.push({ type: 'open', value: '' });
      save();
      renderRoutines();
    } else if (act === 'del-step') {
      r.steps.splice(i, 1);
      save();
      renderRoutines();
    } else if (act === 'pick') {
      try {
        const { path } = await ask('pickfile', [], 600000);
        if (!path) return;
        r.steps[i].value = path;
        save();
        renderRoutines();
      } catch (err) {
        toast(err.message);
      }
    }
  });
  renderRoutines();
}

export { findRoutine, runRoutine, routineNames, setupRoutines, renderRoutines };
