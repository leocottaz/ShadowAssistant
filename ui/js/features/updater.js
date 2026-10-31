// Mises à jour : la page cherche une version plus récente sur GitHub et affiche la bannière ;
// le natif (src/features/updater.*) ne télécharge et n'installe qu'au clic, d'après un simple numéro.
import { $, store, toast } from '../core/util.js';
import { bridge, send } from '../core/bridge.js';
import { settings, setSetting } from '../core/settings.js';

const API = 'https://api.github.com/repos/leocottaz/ShadowAssistant/releases/latest';
const TAG_API = 'https://api.github.com/repos/leocottaz/ShadowAssistant/releases/tags/';
const RELEASES ='https://github.com/leocottaz/ShadowAssistant/releases';
const CHECK_EVERY = 6 * 3600 * 1000;

// version : numéro de l'exécutable ; canUpdate : copie installée par Setup.exe (sinon on renvoie vers GitHub).
const U = { version: '0.0.0', canUpdate: false, latest: null, dismissed: false, busy: false, busyText: '', error: '' };

// « v1.2.3 » → [1, 2, 3] ; null pour tout autre format (préversions comprises).
function parseVersion(text) {
  const m = /^v?(\d{1,5})\.(\d{1,5})\.(\d{1,5})$/.exec(String(text).trim());
  return m ? [Number(m[1]), Number(m[2]), Number(m[3])] : null;
}

function isNewer(candidate, current) {
  const a = parseVersion(candidate), b = parseVersion(current);
  if (!a || !b) return false;
  for (let i = 0; i < 3; i++) if (a[i] !== b[i]) return a[i] > b[i];
  return false;
}

const isDev = () => U.version === '0.0.0';
const releaseUrl = () => (U.latest ? U.latest.url : RELEASES);

function setStatus(text) { $('#updateStatus').textContent = text; }

function render() {
  $('#updateVersion').textContent = isDev() ? 'développement' : U.version;
  const l = U.latest;
  const pending = !!l && !U.dismissed && l.tag !== settings.updateSkipped && isNewer(l.tag, U.version);
  const banner = $('#updateBanner');
  banner.hidden = !pending && !U.busy;
  if (pending || U.busy) {
    $('#updateTitle').textContent = l ? `Version ${l.version} disponible` : 'Mise à jour';
    $('#updateSub').textContent = U.error || (U.busy ? U.busyText : `Tu as la ${U.version}.`);
    $('#updateSub').title = U.error; // message complet quand la bannière le tronque
    $('#updateInstall').textContent = !U.canUpdate ? 'Télécharger' : U.error ? 'Réessayer' : 'Mettre à jour';
    $('#updateInstall').disabled = U.busy;
    $('#updateLater').hidden = U.busy;
    $('#updateSkip').hidden = U.busy;
    $('#updateBar').hidden = !U.busy;
    banner.classList.toggle('error', !!U.error);
  }
}

// --------------------------------------------------------------- vérification

async function check({ manual = false } = {}) {
  if (isDev()) {
    if (manual) setStatus('Version de développement : pas de vérification.');
    return;
  }
  if (manual) setStatus('Recherche en cours…');
  try {
    const res = await fetch(API, { headers: { Accept: 'application/vnd.github+json' }, cache: 'no-store' });
    if (res.status === 404) { if (manual) setStatus('Aucune version publiée pour le moment.'); return; }
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const r = await res.json();
    store.set('updateLastCheck', Date.now());
    if (r.draft || r.prerelease || !parseVersion(r.tag_name)) { if (manual) setStatus('Tu as la dernière version.'); return; }
    const version = r.tag_name.replace(/^v/, '');
    U.latest = { tag: r.tag_name, version, url: r.html_url || RELEASES };
    if (isNewer(r.tag_name, U.version)) {
      if (manual) { U.dismissed = false; setStatus(`La version ${version} est disponible.`); }
      // Une seule notification par version, même si le panneau reste fermé.
      if (store.get('updateNotified', '') !== r.tag_name && r.tag_name !== settings.updateSkipped) {
        store.set('updateNotified', r.tag_name);
        send('notify', 'Mise à jour disponible', `Shadow Assistant ${version} est prête (tu as la ${U.version}).`);
      }
    } else if (manual) {
      setStatus('Tu as la dernière version.');
    }
  } catch (e) {
    if (manual) setStatus('Impossible de joindre GitHub. Vérifie ta connexion.');
  }
  render();
}

function schedule() {
  const last = store.get('updateLastCheck', 0);
  if (settings.updateCheck && !isDev() && Date.now() - last > CHECK_EVERY) setTimeout(() => check(), 8000);
  setInterval(() => { if (settings.updateCheck) check(); }, CHECK_EVERY);
}

// --------------------------------------------------------------- installation (natif)

function install() {
  if (!U.latest) return;
  if (!U.canUpdate || !bridge) { send('openurl', releaseUrl()); return; }
  U.busy = true;
  U.error = '';
  U.busyText = 'Téléchargement…';
  $('#updateFill').style.width = '0%';
  render();
  send('update', U.latest.tag);
}

const STATE_TEXT = {
  downloading: 'Téléchargement…',
  verifying: 'Vérification du fichier…',
  installing: 'Installation…',
  restarting: 'Redémarrage de Shadow Assistant…',
};

// Messages {"t":"update"} du natif.
function onUpdateMessage(m) {
  if (m.state === 'error') {
    U.busy = false;
    U.error = m.msg || 'La mise à jour a échoué.';
    setStatus(U.error);
    render();
    return;
  }
  U.busy = true;
  U.busyText = STATE_TEXT[m.state] || '';
  if (typeof m.pct === 'number') {
    $('#updateFill').style.width = `${m.pct}%`;
    if (m.state === 'downloading') U.busyText = `Téléchargement… ${m.pct} %`;
  }
  render();
  // Le programme d'installation arrête et relance le dock ; on se ferme proprement juste avant.
  if (m.state === 'restarting') setTimeout(() => send('quit'), 700);
}

// Message « init » du natif : numéro de version de l'exécutable et type d'installation.
function onUpdateInit(m) {
  U.version = m.version || '0.0.0';
  U.canUpdate = !!m.canUpdate;
  render();
  schedule();
  announceChanges();
}

// --------------------------------------------------------------- nouveautés

// Les nouveautés sont le texte de la Release GitHub de cette version : une ligne par « - … » (ou « * … »).
// Renvoie null si GitHub est injoignable (on réessaiera au prochain lancement), [] s'il n'y a rien à montrer.
async function fetchNotes(version) {
  try {
    const res = await fetch(`${TAG_API}v${version}`, { headers: { Accept: 'application/vnd.github+json' }, cache: 'no-store' });
    if (res.status === 404) return [];
    if (!res.ok) return null;
    const r = await res.json();
    return String(r.body || '').split('\n').map((l) => l.trim()).filter((l) => /^[-*]\s+\S/.test(l)).map((l) => l.replace(/^[-*]\s+/, ''));
  } catch (e) {
    return null;
  }
}

function showChangelog(version, items) {
  $('#whatsNewTitle').textContent = `Nouveautés de la version ${version}`;
  $('#whatsNewList').replaceChildren(...items.map((text) => Object.assign(document.createElement('li'), { textContent: text })));
  $('#whatsNew').hidden = false;
  send('open'); // le dock est rangé au démarrage : on le déroule pour montrer la carte
}

// Une seule fois par version : rien au premier lancement ni en version de développement.
async function announceChanges() {
  if (isDev()) return;
  const seen = store.get('lastSeenVersion', '');
  if (seen === U.version) return;
  // Pas de version enregistrée : installation neuve (welcomed absent, rien à montrer) ou mise à jour
  // depuis une version qui ne l'enregistrait pas encore (welcomed présent) : on montre les nouveautés.
  const upgraded = seen ? isNewer(U.version, seen) : store.get('welcomed', false);
  if (!upgraded) { store.set('lastSeenVersion', U.version); return; }
  const items = await fetchNotes(U.version);
  if (!items) return; // hors ligne : on réessaie au prochain lancement
  store.set('lastSeenVersion', U.version);
  if (items.length) showChangelog(U.version, items);
}

function setupUpdater() {
  $('#updateInstall').addEventListener('click', install);
  $('#updateLater').addEventListener('click', () => { U.dismissed = true; render(); });
  $('#updateSkip').addEventListener('click', () => {
    if (U.latest) setSetting('updateSkipped', U.latest.tag);
    render();
    toast('Version ignorée');
  });
  $('#whatsNewClose').addEventListener('click', () => { $('#whatsNew').hidden = true; });
  $('#updateCheckNow').addEventListener('click', () => check({ manual: true }));
  $('#updateOpenReleases').addEventListener('click', () => send('openurl', releaseUrl()));
  render();
}

export { setupUpdater, onUpdateInit, showChangelog, onUpdateMessage, parseVersion, isNewer };
