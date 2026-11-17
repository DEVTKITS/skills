// Schreibt svg/types.json aus SvgKit.schema/themes/tokens — einzige Quelle für GET /api/svg/types.
// Aufruf: node svg/make_types.mjs
import { createRequire } from 'module'; import fs from 'fs'; import path from 'path'; import { fileURLToPath } from 'url';
const here = path.dirname(fileURLToPath(import.meta.url));
const SK = createRequire(import.meta.url)(path.join(here, '..', 'svgkit.js'));
const tokens = {}; for (const t of SK.themes) tokens[t] = SK.tokens(t);
const out = { ok: true, spec_version: SK.version, types: SK.schema, themes: SK.themes, modes: ['vars', 'static'], tokens };
fs.writeFileSync(path.join(here, 'types.json'), JSON.stringify(out, null, 1) + '\n');
console.log('svg/types.json: ' + Object.keys(SK.schema).length + ' Typen, ' + SK.themes.length + ' Themes');
