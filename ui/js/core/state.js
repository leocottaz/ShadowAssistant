// État partagé de l'interface (état du dock, onglet, connexion).
import { settings } from './settings.js';
import { bridge } from './bridge.js';

const S = {
  st: 'hidden', pinned: false, tab: settings.lastTab, hasKey: false, keyHint: '', keyTest: null,
  cc: { checked: false, found: false, version: '', checking: !!bridge },
  freeKeys: {}, // { gemini: { hasKey, keyHint }, groq: … } (envoyé par le natif)
};

export { S };
