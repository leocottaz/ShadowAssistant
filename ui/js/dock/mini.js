// Mini-dock : quand le dock est rangé, le natif réduit la fenêtre à une pastille (état « mini »).
// Elle montre les rappels qui ont sonné (cloche + nombre), le temps du minuteur et/ou la ligne de
// paroles en cours. On envoie au natif la largeur voulue en DIP ; 0 = rien à montrer, le dock se
// range comme avant. Les autres modules ne font que lui donner leur contenu (setMiniAlerts,
// setMiniTimer, setMiniLyric).
import { $ } from '../core/util.js';
import { send } from '../core/bridge.js';

const MAX_W = 560;
const state = { alerts: 0, timer: null, lyric: null }; // timer : { text, paused } ; lyric : { text, gap } ou null
let sent = -1;
let ctx = null;

function textWidth(el, text, weight) {
  ctx ??= document.createElement('canvas').getContext('2d');
  const cs = getComputedStyle(el);
  ctx.font = `${weight} ${cs.fontSize} ${cs.fontFamily}`; // le canvas refuse la graisse 650 et l'interligne
  return ctx.measureText(text).width;
}

// Doit suivre les marges de .pill dans css/dock.css.
function wantedWidth() {
  const { alerts, timer, lyric } = state;
  if (!alerts && !timer && !lyric) return 0;
  const chip = alerts ? 8 + 15 + 4 + textWidth($('#pillCount'), String(alerts), 700) + 8 : 0;
  // Chiffres mesurés comme des 0 : la largeur ne doit pas bouger à chaque seconde.
  const time = timer ? 4 + 15 + 5 + textWidth($('#pillTime'), timer.text.replace(/\d/g, '0'), 600) + 4 : 0;
  if (!lyric) return Math.ceil(7 + chip + (chip && time ? 8 : 0) + time + 7); // la petite pastille : cloche et/ou minuteur
  const w = 7 + (chip ? chip + 8 : 0) + (time ? time + 10 : 0) + 26 + 10 + textWidth($('#pillLine'), lyric.text, 600) + 16 + 8;
  return Math.min(MAX_W, Math.max(180, Math.ceil(w / 8) * 8)); // par pas de 8 : moins de redimensionnements
}

function update() {
  const pill = $('#pill');
  pill.classList.toggle('has-alert', state.alerts > 0);
  pill.classList.toggle('has-timer', !!state.timer);
  pill.classList.toggle('has-lyric', !!state.lyric);
  const width = wantedWidth();
  if (width === sent) return;
  sent = width;
  if (width) document.documentElement.style.setProperty('--mini-w', `${width}px`);
  send('mini', String(width));
}

// Nombre de rappels qui ont sonné et attendent « Fait » ou un report.
function setMiniAlerts(count) {
  if (count === state.alerts) return;
  const more = count > state.alerts;
  state.alerts = count;
  $('#pillCount').textContent = count;
  if (more) { // la cloche sonne à chaque nouveau rappel
    const bell = $('#pill .pill-alert');
    bell.classList.remove('ring');
    void bell.offsetWidth;
    bell.classList.add('ring');
  }
  update();
}

// Temps du minuteur ({ text, paused }), ou null quand il n'y a rien à montrer.
function setMiniTimer(timer) {
  if (timer) {
    $('#pillTime').textContent = timer.text;
    $('#pillTimer').classList.toggle('paused', timer.paused);
  }
  state.timer = timer;
  update();
}

// Ligne de paroles en cours ({ text, gap }), ou null quand il n'y en a pas à montrer.
function setMiniLyric(lyric) {
  const box = $('#pillLine');
  if (lyric && box.dataset.text !== lyric.text) {
    box.dataset.text = lyric.text;
    // L'ancienne ligne monte et s'efface pendant que la nouvelle arrive.
    for (const old of box.querySelectorAll('span:not(.out)')) {
      old.classList.add('out');
      setTimeout(() => old.remove(), 400);
    }
    const span = document.createElement('span');
    span.textContent = lyric.text;
    span.classList.toggle('gap', !!lyric.gap);
    box.append(span);
  }
  state.lyric = lyric;
  update();
}

// Onglet à ouvrir quand on clique sur la pastille.
const miniTab = () => (state.alerts ? 'reminders' : state.lyric ? 'music' : 'timer');

export { setMiniAlerts, setMiniTimer, setMiniLyric, miniTab };
