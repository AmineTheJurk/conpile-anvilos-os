import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';

const PORT = 3000;
const ROOT = process.cwd();

const FILES = {
  '/anvilos-complete.zip': { file: 'anvilos-complete.zip', mime: 'application/zip' },
  '/anvilos-source.zip':   { file: 'build/anvilos-source.zip', mime: 'application/zip' },
  '/anvilos_usb.bin':      { file: 'build/anvilos_usb.bin', mime: 'application/octet-stream' },
  '/anvilos_usb.img':      { file: 'build/anvilos_usb.img', mime: 'application/octet-stream' },
  '/agcc.exe':             { file: 'build/agcc.exe', mime: 'application/vnd.microsoft.portable-executable' },
  '/mkanvilfs.exe':        { file: 'build/mkanvilfs.exe', mime: 'application/vnd.microsoft.portable-executable' },
  '/mkmod.exe':            { file: 'build/mkmod.exe', mime: 'application/vnd.microsoft.portable-executable' },
};

const server = http.createServer((req, res) => {
  const urlPath = (req.url || '/').split('?')[0];

  if (FILES[urlPath]) {
    const entry = FILES[urlPath];
    const full = path.join(ROOT, entry.file);
    if (!fs.existsSync(full)) {
      res.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' });
      res.end(`Not built yet: ${entry.file}\nRun: make all zip\n`);
      return;
    }
    const stat = fs.statSync(full);
    res.writeHead(200, {
      'Content-Type': entry.mime,
      'Content-Length': stat.size,
      'Content-Disposition': `attachment; filename="${path.basename(entry.file)}"`,
    });
    fs.createReadStream(full).pipe(res);
    return;
  }

  const lines = [
    'AnvilOS Build & Artifact Server (Pure C / x86 ASM — Zero HTML/TS)',
    '==================================================================',
    '',
    'Downloadable Compiled Artifacts:',
    '  GET /anvilos-complete.zip -> Source + 91 OpenBSD .mod drivers + USB image + Windows tools',
    '  GET /anvilos-source.zip   -> Source code + OpenBSD driver modules + GitHub workflow',
    '  GET /anvilos_usb.bin      -> Raw 8MB bootable USB image (flash with PassMark ImageUSB)',
    '  GET /anvilos_usb.img      -> Identical raw 8MB bootable USB image (.img extension)',
    '  GET /agcc.exe             -> Windows x64 AnvilOS C Compiler (.nc -> .anv)',
    '  GET /mkanvilfs.exe        -> Windows x64 AnvilFS & USB Image Packer',
    '  GET /mkmod.exe            -> Windows x64 AKM1 Kernel Module (.mod) Packer',
    '',
    'Status:',
  ];

  for (const [route, meta] of Object.entries(FILES)) {
    const p = path.join(ROOT, meta.file);
    const info = fs.existsSync(p) ? `${fs.statSync(p).size} bytes` : 'missing';
    lines.push(`  ${route.padEnd(24)} [${info}]`);
  }

  res.writeHead(200, { 'Content-Type': 'text/plain; charset=utf-8' });
  res.end(lines.join('\n') + '\n');
});

server.listen(PORT, '0.0.0.0', () => {
  console.log(`AnvilOS binary artifact server listening on http://0.0.0.0:${PORT}`);
});
