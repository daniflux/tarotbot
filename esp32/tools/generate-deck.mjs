import fs from "node:fs/promises";
import path from "node:path";
import vm from "node:vm";
import { fileURLToPath } from "node:url";
import sharp from "sharp";
import twemoji from "@twemoji/api";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const sourceUrl =
  "https://raw.githubusercontent.com/daniflux/tarotbot/main/decks/emoji/deck.js";
const iconSize = 48;
const cream = { r: 255, g: 243, b: 230 };

const response = await fetch(sourceUrl);
if (!response.ok) throw new Error(`Deck download failed: ${response.status}`);
const source = await response.text();
const context = { window: {} };
vm.runInNewContext(source, context, { filename: "deck.js" });
const cards = context.window.deckData?.cards;
if (!Array.isArray(cards) || cards.length !== 78) {
  throw new Error(`Expected 78 cards, received ${cards?.length ?? "none"}`);
}

const twemojiRoot = path.join(root, "node_modules", "@twemoji", "svg");
const arrays = [];
const iconNames = new Map();

function cppString(value) {
  return JSON.stringify(value)
    .replaceAll("\\u2026", "...")
    .replaceAll("’", "'")
    .replaceAll("—", "-");
}

function identifier(codepoint) {
  return `emoji_${codepoint.replaceAll("-", "_")}`;
}

async function findSvg(symbol) {
  const codepoint = twemoji.convert.toCodePoint(symbol);
  const candidates = [codepoint, codepoint.replaceAll("-fe0f", "")];
  for (const candidate of candidates) {
    const svg = path.join(twemojiRoot, `${candidate}.svg`);
    try {
      await fs.access(svg);
      return { svg, codepoint: candidate };
    } catch {}
  }
  throw new Error(`No Twemoji SVG for ${symbol} (${codepoint})`);
}

async function convertIcon(symbol) {
  const { svg, codepoint } = await findSvg(symbol);
  if (iconNames.has(codepoint)) return iconNames.get(codepoint);

  const { data, info } = await sharp(svg)
    .resize(iconSize, iconSize, { fit: "contain" })
    .ensureAlpha()
    .raw()
    .toBuffer({ resolveWithObject: true });
  const pixels = [];
  for (let i = 0; i < info.width * info.height; ++i) {
    const offset = i * 4;
    const alpha = data[offset + 3] / 255;
    const r = Math.round(data[offset] * alpha + cream.r * (1 - alpha));
    const g = Math.round(data[offset + 1] * alpha + cream.g * (1 - alpha));
    const b = Math.round(data[offset + 2] * alpha + cream.b * (1 - alpha));
    const rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    pixels.push(`0x${rgb565.toString(16).padStart(4, "0")}`);
  }

  const name = identifier(codepoint);
  const rows = [];
  for (let i = 0; i < pixels.length; i += 16) {
    rows.push(`  ${pixels.slice(i, i + 16).join(", ")}`);
  }
  arrays.push(`static const uint16_t ${name}[] PROGMEM = {\n${rows.join(",\n")}\n};`);
  iconNames.set(codepoint, name);
  return name;
}

const cardRows = [];
for (const card of cards) {
  const icon = await convertIcon(card.symbol);
  cardRows.push(
    `  {${cppString(card.name)}, ${cppString(card.meaning)}, ${cppString(card.interpretation)}, ${icon}}`
  );
}

const header = `// Generated from ${sourceUrl}\n// Emoji artwork: Twemoji (CC-BY 4.0) https://github.com/jdecked/twemoji\n#pragma once\n#include <Arduino.h>\n\nconstexpr uint8_t EMOJI_SIZE = ${iconSize};\n\nstruct Card {\n  const char *name;\n  const char *meaning;\n  const char *interpretation;\n  const uint16_t *emoji;\n};\n\n${arrays.join("\n\n")}\n\nstatic const Card CARDS[] = {\n${cardRows.join(",\n")}\n};\n\nconstexpr uint8_t CARD_COUNT = sizeof(CARDS) / sizeof(CARDS[0]);\n`;

await fs.writeFile(path.join(root, "src", "cards_generated.h"), header);
console.log(`Generated ${cards.length} cards with ${arrays.length} unique emoji.`);
