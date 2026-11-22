'use strict';

// Convert an absolute in-process address to its module-relative offset.
// Usage:
//   1. Set TARGET_ADDR below to the address from your memory scanner.
//   2. frida -U -f com.tencent.ig -l tools/addr_to_offset.js
// Output prints the owning module, base, and hex offset to paste into CFG.off.

const TARGET_ADDR = ptr(0x0); // FILL: absolute address from GameGuardian / scan

function main() {
  if (TARGET_ADDR.isNull()) {
    console.log('[!] Set TARGET_ADDR first');
    return;
  }
  const mods = Process.enumerateModules();
  for (const m of mods) {
    const start = m.base;
    const end = m.base.add(m.size);
    if (TARGET_ADDR.compare(start) >= 0 && TARGET_ADDR.compare(end) < 0) {
      const off = TARGET_ADDR.sub(m.base);
      console.log('[+] module : ' + m.name);
      console.log('[+] base   : ' + m.base);
      console.log('[+] size   : ' + m.size);
      console.log('[+] offset : 0x' + off.toString(16));
      return;
    }
  }
  console.log('[!] address not inside any loaded module');
}

setImmediate(main);
