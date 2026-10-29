// Carnet de notes rapides.
import { $, $$, uid, esc, icon, store, autosize, animateOut } from '../core/util.js';
import { fmtRel } from '../core/format.js';

let notes = store.get('notes', []);
const NOTE_COLORS = { violet: '#8b7cff', cyan: '#3fd0e0', rose: '#ff6fb1', ambre: '#ffb547', vert: '#3ddc84' };

function addNote(text = '', focus = true) {
  const colors = Object.keys(NOTE_COLORS);
  const n = { id: uid(), text, color: colors[notes.length % colors.length], updated: Date.now() };
  notes.unshift(n);
  store.set('notes', notes);
  renderNotes();
  if (focus) setTimeout(() => $(`.note[data-id="${n.id}"] textarea`)?.focus(), 60);
  return n;
}

function renderNotes() {
  const grid = $('#notesGrid');
  if (!notes.length) {
    grid.innerHTML = `<div class="empty" style="grid-column:1/-1">${icon('note')}<b>Aucune note</b><p>Garde ici tes idées, listes et bouts de texte à portée de souris.</p></div>`;
  } else {
    grid.innerHTML = notes.map((n) => `
      <div class="note glass" data-id="${n.id}" style="--nc:${NOTE_COLORS[n.color] || NOTE_COLORS.violet}">
        <textarea placeholder="Écris quelque chose…" spellcheck="false">${esc(n.text)}</textarea>
        <div class="note-foot">
          <span>${fmtRel(n.updated)}</span>
          <div class="cdots">${Object.entries(NOTE_COLORS).map(([c, v]) => `<button type="button" class="cdot ${c === n.color ? 'on' : ''}" data-color="${c}" style="--c:${v}" title="${c}"></button>`).join('')}</div>
          <button type="button" class="icon-btn del" title="Supprimer">${icon('trash')}</button>
        </div>
      </div>`).join('');
    $$('.note textarea', grid).forEach((t) => autosize(t));
  }
  $('#notesSummary').textContent = notes.length ? `${notes.length} note${notes.length > 1 ? 's' : ''}` : 'Ton carnet rapide';
}

function setupNotes() {
  $('#addNote').addEventListener('click', () => addNote());
  const grid = $('#notesGrid');
  let saveTimer;
  grid.addEventListener('input', (e) => {
    const el = e.target.closest('.note');
    const n = notes.find((x) => x.id === el?.dataset.id);
    if (!n) return;
    n.text = e.target.value;
    n.updated = Date.now();
    autosize(e.target);
    clearTimeout(saveTimer);
    saveTimer = setTimeout(() => store.set('notes', notes), 300);
  });
  grid.addEventListener('click', (e) => {
    const el = e.target.closest('.note');
    const n = notes.find((x) => x.id === el?.dataset.id);
    if (!n) return;
    const dot = e.target.closest('.cdot');
    if (dot) {
      n.color = dot.dataset.color;
      el.style.setProperty('--nc', NOTE_COLORS[n.color]);
      $$('.cdot', el).forEach((d) => d.classList.toggle('on', d === dot));
      store.set('notes', notes);
    }
    if (e.target.closest('.del')) {
      animateOut(el, () => {
        notes = notes.filter((x) => x !== n);
        store.set('notes', notes);
        renderNotes();
      });
    }
  });
}

export { addNote, renderNotes, setupNotes };
