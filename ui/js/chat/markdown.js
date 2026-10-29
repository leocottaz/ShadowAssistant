// Rendu Markdown minimal et sûr (le texte est échappé avant toute mise en forme).
import { esc } from '../core/util.js';

function md(src) {
  return String(src).split('```').map((part, i) => {
    if (i % 2 === 0) return mdBlocks(part);
    const nl = part.indexOf('\n');
    const code = nl >= 0 ? part.slice(nl + 1) : part;
    return `<pre><code>${esc(code.replace(/\n$/, ''))}</code></pre>`;
  }).join('');
}

function mdBlocks(text) {
  let html = '';
  let para = [];
  let list = null;
  const flushPara = () => { if (para.length) { html += `<p>${inline(para.join('\n')).replace(/\n/g, '<br>')}</p>`; para = []; } };
  const flushList = () => { if (list) { html += `<${list.type}>${list.items.map((it) => `<li>${inline(it)}</li>`).join('')}</${list.type}>`; list = null; } };
  for (const line of text.split('\n')) {
    let m;
    if (!line.trim()) { flushPara(); flushList(); continue; }
    if ((m = line.match(/^(#{1,4})\s+(.*)$/))) { flushPara(); flushList(); const l = Math.min(m[1].length + 2, 6); html += `<h${l}>${inline(m[2])}</h${l}>`; continue; }
    if (/^\s*([-*_])(\s*\1){2,}\s*$/.test(line)) { flushPara(); flushList(); html += '<hr>'; continue; }
    if ((m = line.match(/^\s*[-*+•]\s+(.*)$/))) { flushPara(); if (list?.type !== 'ul') { flushList(); list = { type: 'ul', items: [] }; } list.items.push(m[1]); continue; }
    if ((m = line.match(/^\s*\d+[.)]\s+(.*)$/))) { flushPara(); if (list?.type !== 'ol') { flushList(); list = { type: 'ol', items: [] }; } list.items.push(m[1]); continue; }
    if ((m = line.match(/^>\s?(.*)$/))) { flushPara(); flushList(); html += `<blockquote>${inline(m[1])}</blockquote>`; continue; }
    if (list && /^\s{2,}\S/.test(line)) { list.items[list.items.length - 1] += ' ' + line.trim(); continue; }
    flushList();
    para.push(line);
  }
  flushPara();
  flushList();
  return html;
}

function inline(s) {
  const codes = [];
  s = esc(s).replace(/`([^`]+)`/g, (_, c) => { codes.push(c); return `\u0000${codes.length - 1}\u0000`; });
  s = s.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>')
    .replace(/(^|[^*\w])\*([^*\n]+)\*(?!\w)/g, '$1<em>$2</em>')
    .replace(/~~([^~]+)~~/g, '<del>$1</del>')
    .replace(/\[([^\]]+)\]\((https?:\/\/[^\s)]+)\)/g, '<a href="$2">$1</a>')
    .replace(/(^|[\s(])(https?:\/\/[^\s<)]+)/g, '$1<a href="$2">$2</a>');
  return s.replace(/\u0000(\d+)\u0000/g, (_, i) => `<code>${codes[+i]}</code>`);
}

export { md };
