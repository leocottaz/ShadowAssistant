# Mesure la fluidité des changements d'onglet dans le dock lancé avec le port de débogage.
# Pour chaque onglet (sauf Musique, pour ne pas afficher de vraies paroles) : images reçues
# en 700 ms, intervalle maximal et nombre d'images ratées (intervalle > 1,5 × la normale).
param([int]$Port = 9333, [string[]]$Tabs = @('notes', 'settings', 'claude', 'reminders', 'notes', 'settings'))
$list = ($Tabs | ForEach-Object { "'$_'" }) -join ','
$expr = @"
(async () => {
  const dock = await import('./js/dock/dock.js');
  const frames = (ms) => new Promise((ok) => {
    const t = []; const t0 = performance.now();
    const f = (now) => { t.push(now); if (now - t0 < ms) requestAnimationFrame(f); else ok(t); };
    requestAnimationFrame(f);
  });
  const idle = await frames(500);
  const d0 = idle.slice(1).map((v, i) => v - idle[i]).sort((a, b) => a - b);
  const vsync = d0[Math.floor(d0.length / 2)];
  const out = [];
  for (const tab of [$list]) {
    const p = frames(700);
    dock.switchTab(tab);
    const t = await p;
    const d = t.slice(1).map((v, i) => v - t[i]);
    out.push(tab + ': ' + t.length + ' img, max ' + Math.max(...d).toFixed(0) + ' ms, ratees ' + d.filter((x) => x > vsync * 1.5).length);
    await frames(300);
  }
  return 'vsync ' + vsync.toFixed(1) + ' ms\n' + out.join('\n');
})()
"@
$r = & "$PSScriptRoot\cdp.ps1" -Expr $expr -Port $Port | ConvertFrom-Json
$r.result.result.value
