// Recherche de fichiers pour Claude, dans l'index Windows Search (src/features/pc.cpp).
// Les résultats sont gardés par recherche (carte cliquable dans la conversation). Claude
// ouvre un fichier par son numéro dans la dernière recherche, jamais par un chemin à lui ;
// le natif, lui, n'ouvre que des chemins qu'il a renvoyés.
import { store } from '../core/util.js';
import { ask, send } from '../core/bridge.js';

const MAX_SEARCHES = 20;
let found = store.get('found', { order: [], byKey: {}, at: 0 });

async function searchFiles({ words = '', extension = '', from = '', to = '' }, key) {
  const clean = (s) => String(s || '').replace(/[\r\n]+/g, ' ').trim();
  const r = await ask('search', [words, extension, from, to].map(clean));
  const results = r.results || [];
  found.byKey[key] = results;
  found.order = found.order.filter((k) => k !== key).concat(key);
  while (found.order.length > MAX_SEARCHES) delete found.byKey[found.order.shift()];
  found.at = Date.now();
  store.set('found', found);
  return results;
}

const foundFor = (key) => found.byKey[key] || null;
const lastFound = () => found.byKey[found.order[found.order.length - 1]] || [];

// n : numéro (à partir de 1) dans la dernière recherche. Retourne le fichier, ou null.
function openFound(n) {
  const f = lastFound()[n - 1];
  if (f) send('openfound', f.path);
  return f || null;
}

const openPath = (path) => send('openfound', path);

// Dossier lisible : relatif au dossier de l'utilisateur (« Downloads », « Documents\Factures »).
function folderOf(path) {
  const parts = path.split('\\');
  parts.pop();
  const i = parts.findIndex((p, k) => k > 0 && parts[k - 1].toLowerCase() === 'users');
  return (i >= 0 ? parts.slice(i + 1) : parts).join('\\') || parts.join('\\');
}

const fmtSize = (n) => (n >= 1048576 ? `${(n / 1048576).toFixed(1).replace('.', ',')} Mo` : `${Math.max(1, Math.round(n / 1024))} Ko`);
const fmtDay = (ms) => (ms ? new Date(ms).toLocaleDateString('fr-FR', { day: 'numeric', month: 'short', year: 'numeric' }) : '');

// Liste numérotée pour Claude (résultat d'outil, ou contexte du mode Pro).
function describeFound(results) {
  if (!results.length) return 'Aucun fichier trouvé.';
  return results.map((f, i) => `${i + 1}. ${f.name} — ${folderOf(f.path)} — modifié le ${fmtDay(f.modified)} — ${fmtSize(f.size)}`).join('\n');
}

// Mode Pro : la dernière recherche (moins de 2 h), pour que Claude puisse ouvrir « le 2e ».
function foundContext() {
  const last = lastFound();
  if (!last.length || Date.now() - found.at > 2 * 3600e3) return '';
  return `[Dernière recherche de fichiers :\n${describeFound(last)}]`;
}

export { searchFiles, foundFor, openFound, openPath, folderOf, fmtSize, fmtDay, describeFound, foundContext };
