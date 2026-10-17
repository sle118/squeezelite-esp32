// Helpers to edit the squeezelite command line stored in the `autoexec1` NVS key.

export function tokenize(line) {
  const out = [];
  let cur = '';
  let quote = null;
  for (const ch of line ?? '') {
    if (quote) {
      if (ch === quote) quote = null;
      else cur += ch;
    } else if (ch === '"' || ch === "'") {
      quote = ch;
    } else if (/\s/.test(ch)) {
      if (cur) {
        out.push(cur);
        cur = '';
      }
    } else {
      cur += ch;
    }
  }
  if (cur) out.push(cur);
  return out;
}

function quote(token) {
  return /\s/.test(token) ? `"${token}"` : token;
}

export function join(tokens) {
  return tokens.map(quote).join(' ');
}

export function getOpt(tokens, flag) {
  const i = tokens.indexOf(flag);
  return i >= 0 && i + 1 < tokens.length ? tokens[i + 1] : '';
}

export function setOpt(tokens, flag, value) {
  const i = tokens.indexOf(flag);
  const next = tokens.slice();
  if (!value) {
    if (i >= 0) next.splice(i, 2);
    return next;
  }
  if (i >= 0) next[i + 1] = value;
  else next.push(flag, value);
  return next;
}
