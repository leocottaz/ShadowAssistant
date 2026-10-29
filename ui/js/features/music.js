// Lecteur : affiche la musique suivie par le natif (src/features/media.cpp, {"t":"media"}),
// la pilote (lecture/pause, piste suivante/précédente, position) et affiche les
// paroles, récupérées à la demande sur LRCLIB et synchronisées quand c'est possible.
import { $, $$, esc, clamp } from '../core/util.js';
import { send } from '../core/bridge.js';
import { settings, setSetting, onSettingChange } from '../core/settings.js';
import { S } from '../core/state.js';
import { setMiniLyric } from '../dock/mini.js';

// ------------------------------------------------------------------ état

const M = {
  active: false, app: '', title: '', artist: '', album: '', status: 'stopped',
  pos: 0, dur: 0, at: 0, canNext: false, canPrev: false, canSeek: false, art: '',
};
const lyrics = { key: '', state: 'idle', lines: [], synced: false, current: -1 };
const lyricsCache = new Map();
let seeking = null; // position (ms) pendant qu'on fait glisser la barre

const trackKey = (m) => (m.active ? `${m.title}\u001f${m.artist}\u001f${m.album}` : '');
const isPlaying = () => M.active && M.status === 'playing';

// Position courante, interpolée depuis la dernière mise à jour de Windows.
function currentPos() {
  if (seeking != null) return seeking;
  let p = M.pos;
  if (isPlaying()) p += Date.now() - M.at;
  return M.dur > 0 ? clamp(p, 0, M.dur) : Math.max(0, p);
}

const fmtDur = (ms) => {
  const s = Math.max(0, Math.floor(ms / 1000));
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
};

function sourceName(app) {
  const a = (app || '').toLowerCase();
  if (a.includes('deezer')) return 'Deezer';
  if (a.includes('spotify')) return 'Spotify';
  if (a.includes('msedge') || a.includes('edge')) return 'Microsoft Edge';
  if (a.includes('chrome')) return 'Google Chrome';
  if (a.includes('firefox')) return 'Firefox';
  return app ? app.split(/[\\!]/).pop().replace(/\.exe$/i, '') : '';
}

// ------------------------------------------------------------------ messages du natif

function onMedia(m) {
  const prevKey = trackKey(M);
  if (!m.active) {
    Object.assign(M, { active: false, status: 'stopped' });
  } else {
    // Windows date sa position ; si l'horodatage est absent ou incohérent, on prend l'heure de réception.
    const at = m.at && Math.abs(Date.now() - m.at) < 6 * 3600e3 ? m.at : Date.now();
    Object.assign(M, { ...m, at, art: 'art' in m ? m.art : M.art });
  }
  const changed = trackKey(M) !== prevKey;
  if (changed) {
    applyArt();
    if (M.active) loadLyrics();
  }
  render();
  // Nouveau morceau (ou musique qui démarre) : la carte musique descend un instant pour le montrer.
  if (changed && isPlaying() && settings.musicReveal) {
    const w = cardWidth();
    document.documentElement.style.setProperty('--mcard-w', `${w}px`);
    send('revealmusic', '4500', String(w));
  }
  ensureLoop();
}

// ------------------------------------------------------------------ rendu

function setText(el, text) {
  if (el.textContent !== text) el.textContent = text;
}

function applyArt() {
  for (const img of [$('#npArt'), $('#mArt'), $('#pillArt'), $('#mcardArt')]) {
    if (M.art) img.src = M.art;
    else img.removeAttribute('src');
    img.hidden = !M.art;
  }
  $('#musicBg').style.backgroundImage = M.art ? `url("${M.art}")` : '';
  tintFromArt();
}

// Couleur dominante de la pochette : teinte le lecteur et le mini-lecteur.
function tintFromArt() {
  const root = document.documentElement.style;
  if (!M.art) { root.removeProperty('--m-tint'); return; }
  const img = new Image();
  img.onload = () => {
    const c = document.createElement('canvas');
    c.width = c.height = 12;
    const g = c.getContext('2d');
    g.drawImage(img, 0, 0, 12, 12);
    const d = g.getImageData(0, 0, 12, 12).data;
    let r = 0, gr = 0, b = 0, w = 0;
    for (let i = 0; i < d.length; i += 4) {
      const max = Math.max(d[i], d[i + 1], d[i + 2]), min = Math.min(d[i], d[i + 1], d[i + 2]);
      const weight = 1 + (max - min) / 32; // privilégie les pixels colorés
      r += d[i] * weight; gr += d[i + 1] * weight; b += d[i + 2] * weight; w += weight;
    }
    root.setProperty('--m-tint', `rgb(${Math.round(r / w)}, ${Math.round(gr / w)}, ${Math.round(b / w)})`);
  };
  img.src = M.art;
}

function render() {
  const active = M.active;
  document.body.classList.toggle('has-music', active);
  document.body.classList.toggle('music-playing', isPlaying());
  $('#np').hidden = !active;
  $('#askInput').placeholder = active ? 'Demander…' : 'Demander à Maya…';

  setText($('#npTitle'), M.title);
  setText($('#npArtist'), M.artist);
  setText($('#mTitle'), M.title);
  setText($('#mArtist'), M.artist);
  setText($('#mAlbum'), M.album);
  setText($('#mcardTitle'), M.title);
  setText($('#mcardArtist'), M.artist);
  setText($('#mDur'), M.dur ? fmtDur(M.dur) : '–:––');
  setText($('#musicSource'), active ? `En lecture sur ${sourceName(M.app)}` : 'En attente de musique');
  $('#musicEmptyText').textContent = settings.musicSource === 'deezer'
    ? 'Lance un titre sur Deezer : le lecteur apparaîtra ici et dans la barre.'
    : 'Lance de la musique dans n’importe quelle appli : le lecteur apparaîtra ici et dans la barre.';

  $('#musicView').classList.toggle('is-empty', !active);
  $('#musicView').classList.toggle('with-lyrics', active && settings.lyricsOpen);
  $('#lyricsBtn').classList.toggle('on', settings.lyricsOpen);
  $('#lyricsBtn').hidden = !active;
  $('#mPrev').disabled = $('#mcardPrev').disabled = !M.canPrev;
  $('#mNext').disabled = $('#mcardNext').disabled = !M.canNext;
  $('#mBar').classList.toggle('disabled', !M.canSeek);
  for (const el of [$('#np'), $('#mPlay'), $('#mcardPlay')]) el.classList.toggle('playing', isPlaying());
  updateMarquee();
  tickPosition();
  updateMini();
}

// Largeur de la carte musique (DIP) : juste assez pour le titre, entre 280 et 380.
// Marges fixes (cf. .mcard dans css/music.css) : 10 + pochette 54 + 11 + 6 + boutons 108 + 12, et 6 de jeu.
let measure = null;
function cardWidth() {
  measure ??= document.createElement('canvas').getContext('2d');
  // (le canvas refuse la graisse 650 avec l'interligne : on compose la police à la main)
  const w = (el, weight, size, text) => {
    measure.font = `${weight} ${size}px ${getComputedStyle(el).fontFamily}`;
    return measure.measureText(text).width;
  };
  const text = Math.max(w($('#mcardTitle'), 650, 14, M.title), w($('#mcardArtist'), 400, 12, M.artist));
  return clamp(Math.ceil((207 + text) / 4) * 4, 280, 380);
}

// Mini-dock (js/dock/mini.js) : ligne en cours quand on écoute avec les paroles activées et synchronisées.
function updateMini() {
  const live = isPlaying() && settings.lyricsOpen && lyrics.state === 'ready' && lyrics.synced;
  const line = live ? lyrics.lines[lyrics.current] : null;
  setMiniLyric(live ? { text: line?.text || '♪', gap: !line?.text } : null);
}

// Titre trop long pour le mini-lecteur : il défile doucement.
function updateMarquee() {
  const box = $('.np-title'), span = $('#npTitle');
  requestAnimationFrame(() => {
    const overflow = span.scrollWidth - box.clientWidth;
    box.classList.toggle('marquee', overflow > 4);
    box.style.setProperty('--dist', `${-Math.max(0, overflow)}px`);
    box.style.setProperty('--dur', `${Math.max(6, overflow / 18)}s`);
  });
}

function tickPosition() {
  const pos = currentPos();
  const frac = M.dur > 0 ? pos / M.dur : 0;
  $('#npFill').style.transform = `scaleX(${frac})`;
  $('#mcardFill').style.transform = `scaleX(${frac})`;
  $('#mFill').style.width = `${frac * 100}%`;
  setText($('#mPos'), fmtDur(pos));
  syncLyrics(pos);
}

// Boucle d'animation, seulement quand le dock est visible et qu'une musique est suivie.
// En mini-dock, seules les paroles comptent : 10 fois par seconde suffit.
let loopId = 0;
function ensureLoop() {
  if (loopId) return;
  const step = () => {
    loopId = 0;
    const visible = S.st === 'bar' || S.st === 'panel' || S.st === 'mini' || S.st === 'music';
    if (!M.active || !visible) return;
    tickPosition();
    loopId = S.st === 'mini' ? setTimeout(step, 100) : requestAnimationFrame(step);
  };
  loopId = requestAnimationFrame(step);
}

// ------------------------------------------------------------------ paroles (LRCLIB)

// Retire ce qui gêne la recherche : « (feat. …) », « - Remastered 2011 », « (Radio Edit) », « [Live] »…
const VARIANT = 'remaster|version|edit|live|mix|mono|stereo|deluxe|bonus|explicit|acousti|extended|instrumental';
function cleanTitle(t) {
  const out = t.replace(/\s*[([](feat|ft|featuring|with|avec)\b\.?[^)\]]*[)\]]/gi, '')
    .replace(/\s+(feat|ft|featuring)\.?\s.*$/i, '')
    .replace(new RegExp(`\\s*[([][^)\\]]*(${VARIANT})[^)\\]]*[)\\]]`, 'gi'), '')
    .replace(new RegExp(`\\s+[-–]\\s+.*(${VARIANT}).*$`, 'i'), '')
    .replace(/\s*\[[^\]]*\]/g, '')
    .trim();
  return out || t.trim();
}

// Premier artiste de « A, B », « A feat. B », « A & B »…
const mainArtist = (a) => a.split(/\s*(?:[,;/]|\s&\s|\sx\s|\s(?:feat|ft|featuring)\.?\s|\set\s|\swith\s)\s*/i)[0].trim() || a;

// Comparaison tolérante : sans accents, casse ni ponctuation.
const norm = (s) => (s || '').normalize('NFD').replace(/\p{M}/gu, '').toLowerCase()
  .replace(/[^\p{L}\p{N}]+/gu, ' ').trim();

// Format LRC : « [mm:ss.xx]texte », éventuellement plusieurs horodatages par ligne.
function parseLrc(text) {
  const out = [];
  for (const raw of text.split(/\r?\n/)) {
    const stamps = [...raw.matchAll(/\[(\d+):(\d+(?:[.:]\d+)?)\]/g)];
    if (!stamps.length) continue;
    const line = raw.replace(/\[[^\]]*\]/g, '').trim();
    for (const s of stamps) out.push({ t: (+s[1] * 60 + parseFloat(s[2].replace(':', '.'))) * 1000, text: line });
  }
  return out.sort((a, b) => a.t - b.t);
}

// LRCLIB répond souvent 503 (« server busy ») : on réessaie un peu avant d'abandonner.
// null = introuvable (404).
async function lrclib(path, params) {
  const url = `https://lrclib.net/api/${path}?${new URLSearchParams(params)}`;
  for (let i = 0; ; i++) {
    const r = await fetch(url);
    if ((r.status === 503 || r.status === 429) && i < 3) {
      await new Promise((ok) => setTimeout(ok, 1200 * (i + 1)));
      continue;
    }
    if (r.status === 404) return null;
    if (!r.ok) throw new Error(`LRCLIB ${r.status}`);
    return r.json();
  }
}

// Plusieurs essais du plus précis au plus large ; on s'arrête dès qu'une version a la
// bonne durée (paroles synchronisées). Sinon, on garde la meilleure version trouvée
// mais sans synchronisation, plutôt que d'annoncer « introuvables ».
async function fetchLyrics(track) {
  const secs = Math.round(track.dur / 1000);
  const title = cleanTitle(track.title);
  const artist = mainArtist(track.artist);
  const hasText = (x) => x && (x.instrumental || x.syncedLyrics || x.plainLyrics);
  const diff = (x) => (secs > 0 ? Math.abs((x.duration || 0) - secs) : 0);
  const nt = norm(title), na = norm(artist);
  const sameTitle = (x) => { const c = norm(cleanTitle(x.trackName || '')); return c === nt || c.startsWith(`${nt} `) || nt.startsWith(`${c} `); };
  const sameArtist = (x) => { const c = norm(x.artistName); return c.includes(na) || (c && na.includes(c)); };

  const attempts = [];
  if (secs > 0) {
    attempts.push(['get', { track_name: track.title, artist_name: track.artist, album_name: track.album, duration: secs }]);
    if (title !== track.title || artist !== track.artist) attempts.push(['get', { track_name: title, artist_name: artist, duration: secs }]);
  }
  attempts.push(['search', { track_name: title, artist_name: track.artist }]);
  if (artist !== track.artist) attempts.push(['search', { track_name: title, artist_name: artist }]);
  attempts.push(['search', { q: `${title} ${artist}` }]);
  attempts.push(['search', { track_name: title }]);

  let fallback = null, failures = 0;
  for (const [path, params] of attempts) {
    if (lyrics.key !== track.key) throw new Error('piste changée'); // pas de mise en cache
    let res;
    try { res = await lrclib(path, params); } catch { failures++; continue; }
    const list = (Array.isArray(res) ? res : res ? [res] : []).filter(hasText);
    // Les recherches larges ramènent d'autres morceaux : on vérifie titre et artiste.
    const ok = (path === 'get' || params.artist_name ? list : list.filter((x) => sameTitle(x) && sameArtist(x)))
      .sort((a, b) => diff(a) - diff(b));
    const best = ok.find((x) => x.syncedLyrics || x.instrumental);
    if (best && diff(best) <= 8) return best;
    if (ok[0] && (!fallback || diff(ok[0]) < diff(fallback))) fallback = ok[0];
  }
  if (fallback) return { ...fallback, syncedLyrics: null, plainLyrics: fallback.plainLyrics || stripLrc(fallback.syncedLyrics) };
  if (failures) throw new Error('LRCLIB injoignable'); // erreur : pas de « introuvable » mis en cache
  return null;
}

const stripLrc = (text) => (text || '').split(/\r?\n/).map((l) => l.replace(/\[[^\]]*\]/g, '').trim()).join('\n');

async function loadLyrics() {
  const key = trackKey(M);
  const track = { key, title: M.title, artist: M.artist, album: M.album, dur: M.dur };
  Object.assign(lyrics, { key, state: 'loading', lines: [], synced: false, current: -1 });
  renderLyrics();
  let result = lyricsCache.get(key);
  if (!result) {
    try {
      const data = await fetchLyrics(track);
      if (!data) result = { state: 'none' };
      else if (data.instrumental) result = { state: 'instrumental' };
      else if (data.syncedLyrics) result = { state: 'ready', synced: true, lines: parseLrc(data.syncedLyrics) };
      else if (data.plainLyrics) result = { state: 'ready', synced: false, lines: data.plainLyrics.split(/\r?\n/).map((text) => ({ t: 0, text })) };
      else result = { state: 'none' };
      lyricsCache.set(key, result);
    } catch {
      result = { state: 'error' };
    }
  }
  if (lyrics.key !== key) return; // la piste a changé entre-temps
  Object.assign(lyrics, { synced: false, lines: [], ...result, current: -1 });
  renderLyrics();
  syncLyrics(currentPos(), true);
}

function renderLyrics() {
  const box = $('#lyricsScroll');
  const msg = (title, text = '') => `<div class="lyrics-msg"><b>${title}</b>${text ? `<p>${text}</p>` : ''}</div>`;
  if (lyrics.state === 'loading') {
    box.innerHTML = '<div class="lyrics-loading">' + '<span></span>'.repeat(6) + '</div>';
  } else if (lyrics.state === 'none') {
    box.innerHTML = msg('Paroles introuvables', 'LRCLIB ne connaît pas encore ce titre.');
  } else if (lyrics.state === 'instrumental') {
    box.innerHTML = msg('♪ Instrumental');
  } else if (lyrics.state === 'error') {
    box.innerHTML = msg('Paroles indisponibles', 'Impossible de joindre LRCLIB pour le moment.');
  } else if (lyrics.state === 'ready') {
    box.classList.toggle('synced', lyrics.synced);
    box.innerHTML = '<div class="lyrics-pad"></div>'
      + lyrics.lines.map((l, i) => `<p data-i="${i}"${l.text ? '' : ' class="gap"'}>${esc(l.text) || '♪'}</p>`).join('')
      + '<div class="lyrics-pad"></div>';
  } else {
    box.innerHTML = '';
  }
  updateMini();
}

// Met en avant la ligne en cours et la garde au centre.
function syncLyrics(pos, instant = false) {
  if (lyrics.state !== 'ready' || !lyrics.synced) return;
  let i = -1;
  for (let k = 0; k < lyrics.lines.length && lyrics.lines[k].t <= pos + 150; k++) i = k;
  if (i === lyrics.current && !instant) return;
  const box = $('#lyricsScroll');
  box.querySelector('p.cur')?.classList.remove('cur');
  $$('p.past', box).forEach((p) => { if (+p.dataset.i >= i) p.classList.remove('past'); });
  for (let k = 0; k < i; k++) box.querySelector(`p[data-i="${k}"]`)?.classList.add('past');
  lyrics.current = i;
  updateMini();
  const el = i >= 0 ? box.querySelector(`p[data-i="${i}"]`) : null;
  if (!el) return;
  el.classList.add('cur');
  if (!box.matches(':hover') || instant) {
    const top = el.offsetTop - box.clientHeight / 2 + el.offsetHeight / 2;
    box.scrollTo({ top, behavior: instant || settings.reduceMotion ? 'auto' : 'smooth' });
  }
}

// ------------------------------------------------------------------ contrôles

function seekTo(ms) {
  if (!M.canSeek) return;
  ms = clamp(Math.round(ms), 0, M.dur || ms);
  Object.assign(M, { pos: ms, at: Date.now() });
  send('media', 'seek', ms);
  tickPosition();
}

function toggle() {
  // Bascule optimiste : l'état réel arrive dans la foulée.
  Object.assign(M, { pos: currentPos(), at: Date.now(), status: isPlaying() ? 'paused' : 'playing' });
  send('media', 'toggle');
  render();
  ensureLoop();
}

function setupSeekBar() {
  const bar = $('#mBar');
  const at = (e) => {
    const r = bar.getBoundingClientRect();
    return clamp((e.clientX - r.left) / r.width, 0, 1) * M.dur;
  };
  bar.addEventListener('pointerdown', (e) => {
    if (!M.canSeek || !M.dur) return;
    bar.setPointerCapture(e.pointerId);
    bar.classList.add('dragging');
    seeking = at(e);
    tickPosition();
  });
  bar.addEventListener('pointermove', (e) => {
    if (seeking == null) return;
    seeking = at(e);
    tickPosition();
  });
  const end = (e) => {
    if (seeking == null) return;
    const target = at(e);
    seeking = null;
    bar.classList.remove('dragging');
    seekTo(target);
  };
  bar.addEventListener('pointerup', end);
  bar.addEventListener('pointercancel', () => { seeking = null; bar.classList.remove('dragging'); });
}

function setupMusic() {
  $('#npPlay').addEventListener('click', toggle);
  $('#mPlay').addEventListener('click', toggle);
  $('#mcardPlay').addEventListener('click', toggle);
  for (const id of ['#mPrev', '#mcardPrev']) $(id).addEventListener('click', () => send('media', 'prev'));
  for (const id of ['#mNext', '#mcardNext']) $(id).addEventListener('click', () => send('media', 'next'));
  $('#lyricsBtn').addEventListener('click', () => { setSetting('lyricsOpen', !settings.lyricsOpen); render(); syncLyrics(currentPos(), true); });
  $('#openDeezer').addEventListener('click', () => send('openurl', 'https://www.deezer.com/'));
  $('.lyrics-credit a').addEventListener('click', (e) => send('openurl', e.target.dataset.url));
  $('#lyricsScroll').addEventListener('click', (e) => {
    const p = e.target.closest('p[data-i]');
    if (p && lyrics.synced) seekTo(lyrics.lines[+p.dataset.i].t);
  });
  setupSeekBar();
  onSettingChange((key, value) => {
    if (key === 'musicSource') { send('media', 'sources', value); render(); }
  });
  render();
}

// À appeler quand la page est prête : choix de la source et état complet (pochette comprise).
function connectMusic() {
  send('media', 'sources', settings.musicSource);
  send('media', 'refresh');
}

// Pour le bloc [Contexte] envoyé à Claude : le morceau suivi, ou null.
function nowPlaying() {
  if (!M.active || !M.title) return null;
  const pos = fmtDur(currentPos());
  return {
    title: M.title, artist: M.artist, album: M.album, app: sourceName(M.app), playing: isPlaying(),
    position: M.dur ? `${pos} / ${fmtDur(M.dur)}` : pos,
  };
}

// Aperçu navigateur uniquement : injecte des paroles factices pour tester l'affichage.
function previewLyrics(lines, synced = true) {
  Object.assign(lyrics, { key: trackKey(M), state: 'ready', synced, lines, current: -1 });
  lyricsCache.set(lyrics.key, { state: 'ready', synced, lines });
  renderLyrics();
  syncLyrics(currentPos(), true);
}

export { onMedia, setupMusic, connectMusic, previewLyrics, nowPlaying, ensureLoop as resumeMusicLoop };
