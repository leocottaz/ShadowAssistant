// Formats de dates et durées en français.

const fmtTime = (d) => d.toLocaleTimeString('fr-FR', { hour: '2-digit', minute: '2-digit' });
const startOfDay = (d) => new Date(d.getFullYear(), d.getMonth(), d.getDate());

function fmtWhen(ts) {
  const d = new Date(ts);
  const days = Math.round((startOfDay(d) - startOfDay(new Date())) / 864e5);
  const t = fmtTime(d);
  if (days === 0) return `aujourd'hui ${t}`;
  if (days === 1) return `demain ${t}`;
  if (days === -1) return `hier ${t}`;
  if (days > 1 && days < 7) return `${d.toLocaleDateString('fr-FR', { weekday: 'long' })} ${t}`;
  return `${d.toLocaleDateString('fr-FR', { weekday: 'short', day: 'numeric', month: 'short' })} ${t}`;
}

function fmtIn(ms) {
  if (ms <= 0) return 'maintenant';
  const m = Math.ceil(ms / 60000);
  if (m < 60) return `dans ${m} min`;
  const h = Math.floor(m / 60), r = m % 60;
  if (h < 24) return `dans ${h} h${r ? ' ' + String(r).padStart(2, '0') : ''}`;
  return `dans ${Math.round(h / 24)} j`;
}

function fmtRel(ts) {
  const s = (Date.now() - ts) / 1000;
  if (s < 60) return "à l'instant";
  if (s < 3600) return `il y a ${Math.floor(s / 60)} min`;
  if (s < 86400) return `il y a ${Math.floor(s / 3600)} h`;
  return new Date(ts).toLocaleDateString('fr-FR', { day: 'numeric', month: 'short' });
}

export { fmtTime, startOfDay, fmtWhen, fmtIn, fmtRel };
