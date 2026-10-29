// Réglages persistés (localStorage), appliqués à l'apparence et envoyés au natif.
import { $, $$, store } from './util.js';
import { send } from './bridge.js';

const ACCENTS = {
  violet: ['#8b7cff', '#5ac8fa'],
  bleu: ['#3d8bff', '#46e0d3'],
  rose: ['#ff5fa8', '#ffa45c'],
  vert: ['#2fcf8a', '#9be15d'],
  orange: ['#ff8a3d', '#ffd166'],
  graphite: ['#9aa3b5', '#d6dbe6'],
};

const DEFAULTS = {
  mode: 0, revealDelay: 550, hideDelay: 700, width: 680, panelH: 540,
  theme: 'dark', accent: 'violet', reduceMotion: false, sound: true, autostart: false, lowMemory: true,
  backend: 'pro', ccModel: 'default',
  freeService: 'gemini', freeModel: '', freeFallback: true, // modèle gratuit ('' = automatique)
  musicSource: 'deezer', musicReveal: true, lyricsOpen: true,
  model: 'claude-opus-5-5', effort: 'medium', customInstr: '', lastTab: 'claude',
};
const NUMERIC = new Set(['mode', 'revealDelay', 'hideDelay', 'width', 'panelH']);
const NATIVE_KEYS = new Set(['mode', 'revealDelay', 'hideDelay', 'width', 'panelH', 'theme', 'sound', 'autostart', 'lyricsOpen']);
const MODEL_NAMES = { 'claude-opus-5-5': 'Opus 5.5', 'claude-sonnet-5-5': 'Sonnet 5.5', 'claude-haiku-5-5': 'Haiku 5.5' };
const CC_MODEL_NAMES = { default: 'modèle par défaut', opus: 'Opus', sonnet: 'Sonnet', haiku: 'Haiku' };
const EFFORT_NAMES = { low: 'rapide', medium: 'équilibrée', high: 'approfondie' };
const FREE_NAMES = { gemini: 'Gemini', groq: 'Groq' };

let settings = { ...DEFAULTS, ...store.get('settings', {}) };

function sendConfig() {
  send('config', [
    `mode=${settings.mode}`, `revealDelay=${settings.revealDelay}`, `hideDelay=${settings.hideDelay}`,
    `width=${settings.width}`, `panelH=${settings.panelH}`, `theme=${settings.theme}`,
    `sound=${settings.sound ? 1 : 0}`, `autostart=${settings.autostart ? 1 : 0}`,
    `miniLyrics=${settings.lyricsOpen ? 1 : 0}`, // le natif garde la page éveillée pour le mini-dock
  ].join('\n'));
}

function applyAppearance() {
  const b = document.body;
  b.classList.toggle('theme-dark', settings.theme !== 'light');
  b.classList.toggle('theme-light', settings.theme === 'light');
  b.classList.toggle('reduce', !!settings.reduceMotion);
  const [a, a2] = ACCENTS[settings.accent] || ACCENTS.violet;
  const root = document.documentElement.style;
  root.setProperty('--accent', a);
  root.setProperty('--accent2', a2);
  root.setProperty('--panel-h', settings.panelH + 'px');
  const effort = `réflexion ${EFFORT_NAMES[settings.effort] || settings.effort}`;
  $('#chatModel').textContent = settings.backend === 'pro'
    ? `Abonnement Pro · ${CC_MODEL_NAMES[settings.ccModel] || settings.ccModel} · ${effort}`
    : settings.backend === 'free'
      ? `Gratuit · ${FREE_NAMES[settings.freeService] || settings.freeService}${settings.freeModel ? ` · ${settings.freeModel}` : ''}`
      : `API · ${MODEL_NAMES[settings.model] || settings.model} · ${effort}`;
}

function setSetting(key, value) {
  if (NUMERIC.has(key)) value = Number(value);
  if (settings[key] === value) return;
  settings[key] = value;
  store.set('settings', settings);
  applyAppearance();
  renderSettings();
  if (NATIVE_KEYS.has(key)) sendConfig();
  listeners.forEach((fn) => fn(key, value));
}

// Les autres modules réagissent aux changements sans que settings.js ne dépende d'eux.
const listeners = [];
const onSettingChange = (fn) => listeners.push(fn);

const RANGE_FORMAT = {
  revealDelay: (v) => (v === 0 ? 'instantané' : `${(v / 1000).toFixed(2).replace('.', ',')} s`),
  hideDelay: (v) => `${(v / 1000).toFixed(2).replace('.', ',')} s`,
  width: (v) => `${v} px`,
  panelH: (v) => `${v} px`,
};

function paintRange(input) {
  const v = Number(input.value), min = Number(input.min), max = Number(input.max);
  input.style.setProperty('--v', `${((v - min) / (max - min)) * 100}%`);
  const out = $(`output[data-for="${input.dataset.setting}"]`);
  if (out) out.textContent = RANGE_FORMAT[input.dataset.setting]?.(v) ?? v;
}

function renderSettings() {
  $$('.seg[data-setting], .mode-cards[data-setting]').forEach((seg) => {
    const buttons = $$('button[data-v]', seg);
    const idx = Math.max(0, buttons.findIndex((b) => String(settings[seg.dataset.setting]) === b.dataset.v));
    buttons.forEach((b, i) => b.classList.toggle('on', i === idx));
    const thumb = $('.seg-thumb', seg);
    if (thumb) { thumb.style.setProperty('--i', idx); thumb.style.setProperty('--n', buttons.length); }
  });
  $$('input[type=range][data-setting]').forEach((r) => {
    if (document.activeElement !== r) r.value = settings[r.dataset.setting];
    paintRange(r);
  });
  $$('input[type=checkbox][data-setting]').forEach((c) => { c.checked = !!settings[c.dataset.setting]; });
  $$('#swatches button').forEach((b) => b.classList.toggle('on', b.dataset.accent === settings.accent));
  $('#revealField').style.display = settings.mode === 0 ? '' : 'none';
  $('#proSection').hidden = settings.backend !== 'pro';
  $('#apiSection').hidden = settings.backend !== 'api';
  $('#freeFallbackRow').hidden = settings.backend === 'free'; // le secours n'a de sens qu'avec Claude
  $('#effortField').hidden = settings.backend === 'free';
  if (document.activeElement !== $('#customInstr')) $('#customInstr').value = settings.customInstr;
}

function setupSettings() {
  $$('.seg[data-setting]').forEach((seg) => seg.insertAdjacentHTML('afterbegin', '<span class="seg-thumb"></span>'));
  $$('.seg[data-setting], .mode-cards[data-setting]').forEach((seg) => {
    seg.addEventListener('click', (e) => {
      const b = e.target.closest('button[data-v]');
      if (b) setSetting(seg.dataset.setting, b.dataset.v);
    });
  });
  $$('input[type=range][data-setting]').forEach((r) => {
    r.addEventListener('input', () => paintRange(r));
    r.addEventListener('change', () => setSetting(r.dataset.setting, r.value));
  });
  $$('input[type=checkbox][data-setting]').forEach((c) => {
    c.addEventListener('change', () => setSetting(c.dataset.setting, c.checked));
  });
  $('#swatches').innerHTML = Object.entries(ACCENTS)
    .map(([name, [a, b]]) => `<button type="button" data-accent="${name}" title="${name}" style="--a:${a};--b:${b}"></button>`)
    .join('');
  $('#swatches').addEventListener('click', (e) => {
    const b = e.target.closest('button[data-accent]');
    if (b) setSetting('accent', b.dataset.accent);
  });
  let instrTimer;
  $('#customInstr').addEventListener('input', (e) => {
    clearTimeout(instrTimer);
    instrTimer = setTimeout(() => setSetting('customInstr', e.target.value), 400);
  });

  $('#quitBtn').addEventListener('click', () => send('quit'));
}

export { settings, setSetting, sendConfig, applyAppearance, renderSettings, setupSettings, onSettingChange };
