'use strict';

// Log loaded modules matching a filter, with base, size and path.
// Usage: frida -U -f com.tencent.ig -l tools/module_info.js

const FILTER = 'ue4'; // case-insensitive substring

function main() {
  const needle = String(FILTER).toLowerCase();
  const mods = Process.enumerateModules().filter(function (m) {
    return m.name.toLowerCase().indexOf(needle) !== -1 ||
           m.path.toLowerCase().indexOf(needle) !== -1;
  });
  for (const m of mods) {
    console.log(m.name + ' base=' + m.base + ' size=' + m.size + ' path=' + m.path);
    const ranges = Process.enumerateRangesSync('r--').filter(function (r) {
      const a = r.base, b = r.base.add(r.size);
      return a.compare(m.base) >= 0 && a.compare(m.base.add(m.size)) < 0;
    });
    for (const r of ranges) {
      console.log('    ' + r.protection + ' ' + r.base + ' +0x' + r.size.toString(16));
    }
  }
  if (!mods.length) console.log('[!] no module matched "' + FILTER + '"');
}

setImmediate(main);
