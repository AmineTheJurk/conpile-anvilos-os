#!/usr/bin/env node
import fs from 'node:fs';
import zlib from 'node:zlib';

const IN_PATH = process.argv[2] || 'public/assets/aistudio/AnvilOS_64b249c6.png';
const OUT_PATH = process.argv[3] || 'include/anvilos_icon.h';

function decodePng(buf) {
  if (buf.readUInt32BE(0) !== 0x89504E47 || buf.readUInt32BE(4) !== 0x0D0A1A0A) {
    throw new Error('Not a valid PNG file');
  }
  let off = 8;
  let width = 0, height = 0, bitDepth = 0, colorType = 0;
  const idat = [];
  while (off < buf.length) {
    const len = buf.readUInt32BE(off);
    const type = buf.subarray(off + 4, off + 8).toString('ascii');
    const data = buf.subarray(off + 8, off + 8 + len);
    if (type === 'IHDR') {
      width = data.readUInt32BE(0);
      height = data.readUInt32BE(4);
      bitDepth = data[8];
      colorType = data[9];
    } else if (type === 'IDAT') {
      idat.push(data);
    } else if (type === 'IEND') {
      break;
    }
    off += 12 + len;
  }
  if (bitDepth !== 8 || (colorType !== 2 && colorType !== 6)) {
    throw new Error(`Unsupported PNG format bitDepth=${bitDepth} colorType=${colorType}`);
  }
  const bpp = colorType === 6 ? 4 : 3;
  const stride = width * bpp;
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const rgba = new Uint8Array(width * height * 4);

  const paeth = (a, b, c) => {
    const p = a + b - c;
    const pa = Math.abs(p - a);
    const pb = Math.abs(p - b);
    const pc = Math.abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
  };

  let inOff = 0;
  for (let y = 0; y < height; y++) {
    const filter = raw[inOff++];
    for (let x = 0; x < stride; x++) {
      const val = raw[inOff++];
      const px = Math.floor(x / bpp);
      const ch = x % bpp;
      const a = px > 0 ? rgba[(y * width + px - 1) * 4 + ch] : 0;
      const b = y > 0 ? rgba[((y - 1) * width + px) * 4 + ch] : 0;
      const c = (px > 0 && y > 0) ? rgba[((y - 1) * width + px - 1) * 4 + ch] : 0;
      let rec = val;
      if (filter === 1) rec = (val + a) & 0xFF;
      else if (filter === 2) rec = (val + b) & 0xFF;
      else if (filter === 3) rec = (val + ((a + b) >> 1)) & 0xFF;
      else if (filter === 4) rec = (val + paeth(a, b, c)) & 0xFF;

      const outIdx = (y * width + px) * 4 + ch;
      rgba[outIdx] = rec;
      if (bpp === 3 && ch === 2) rgba[outIdx + 1] = 255;
    }
  }
  return { width, height, rgba };
}

function findContentBounds(img) {
  let minX = img.width, minY = img.height, maxX = -1, maxY = -1;
  for (let y = 0; y < img.height; y++) {
    for (let x = 0; x < img.width; x++) {
      const i = (y * img.width + x) * 4;
      const r = img.rgba[i], g = img.rgba[i + 1], b = img.rgba[i + 2], a = img.rgba[i + 3];
      const isBg = (a < 20) || (r > 235 && g > 235 && b > 235);
      if (!isBg) {
        if (x < minX) minX = x;
        if (y < minY) minY = y;
        if (x > maxX) maxX = x;
        if (y > maxY) maxY = y;
      }
    }
  }
  const cw = maxX - minX + 1;
  const ch = maxY - minY + 1;
  const side = Math.max(cw, ch) + Math.floor(Math.max(cw, ch) * 0.14);
  const cx = Math.floor((minX + maxX) / 2);
  const cy = Math.floor((minY + maxY) / 2);
  return {
    x0: Math.max(0, cx - Math.floor(side / 2)),
    y0: Math.max(0, cy - Math.floor(side / 2)),
    side,
  };
}

function downsampleSquare(img, bounds, outSize) {
  const out = [];
  for (let dy = 0; dy < outSize; dy++) {
    const sy0 = Math.floor(bounds.y0 + (dy * bounds.side) / outSize);
    const sy1 = Math.max(sy0 + 1, Math.floor(bounds.y0 + ((dy + 1) * bounds.side) / outSize));
    for (let dx = 0; dx < outSize; dx++) {
      const sx0 = Math.floor(bounds.x0 + (dx * bounds.side) / outSize);
      const sx1 = Math.max(sx0 + 1, Math.floor(bounds.x0 + ((dx + 1) * bounds.side) / outSize));
      let sr = 0, sg = 0, sb = 0, sa = 0, cnt = 0;
      for (let sy = sy0; sy < sy1 && sy < img.height; sy++) {
        for (let sx = sx0; sx < sx1 && sx < img.width; sx++) {
          const i = (sy * img.width + sx) * 4;
          const r = img.rgba[i], g = img.rgba[i + 1], b = img.rgba[i + 2], a = img.rgba[i + 3];
          /* Convert white canvas background to transparent alpha while preserving saturated blue anvil */
          const whiteness = Math.min(r, g, b);
          const alpha = a < 250 ? a : Math.max(0, Math.min(255, Math.round((245 - whiteness) * (255 / 120))));
          if (alpha > 0) {
            sr += r * alpha;
            sg += g * alpha;
            sb += b * alpha;
            sa += alpha;
          }
          cnt++;
        }
      }
      if (cnt === 0 || sa === 0) {
        out.push(0x00000000);
      } else {
        const avgA = Math.min(255, Math.round(sa / cnt));
        const avgR = Math.min(255, Math.round(sr / sa));
        const avgG = Math.min(255, Math.round(sg / sa));
        const avgB = Math.min(255, Math.round(sb / sa));
        const argb = ((avgA & 0xFF) << 24) | ((avgR & 0xFF) << 16) | ((avgG & 0xFF) << 8) | (avgB & 0xFF);
        out.push(argb >>> 0);
      }
    }
  }
  return out;
}

const png = decodePng(fs.readFileSync(IN_PATH));
const bounds = findContentBounds(png);
const tb22 = downsampleSquare(png, bounds, 22);
const lg32 = downsampleSquare(png, bounds, 32);

const fmtArr = (name, size, arr) => {
  const lines = [`static const uint32_t ${name}[${size * size}] = {`];
  for (let y = 0; y < size; y++) {
    const row = [];
    for (let x = 0; x < size; x++) {
      row.push('0x' + arr[y * size + x].toString(16).padStart(8, '0').toUpperCase());
    }
    lines.push('    ' + row.join(', ') + ',');
  }
  lines.push('};');
  return lines.join('\n');
};

const hdr = `/* Auto-generated from ${IN_PATH} (${png.width}x${png.height}) */
#ifndef ANVIL_ICON_H
#define ANVIL_ICON_H

#ifdef __ANVIL_KERNEL__
#include "types.h"
#else
#include <stdint.h>
#endif

#define ANVIL_ICON_TB_W 22
#define ANVIL_ICON_TB_H 22
#define ANVIL_ICON_LG_W 32
#define ANVIL_ICON_LG_H 32

${fmtArr('anvilos_icon_tb', 22, tb22)}

${fmtArr('anvilos_icon_lg', 32, lg32)}

#endif
`;

fs.writeFileSync(OUT_PATH, hdr);
console.log(`Generated ${OUT_PATH} from ${IN_PATH} (bounds=${bounds.x0},${bounds.y0} ${bounds.side}px)`);
