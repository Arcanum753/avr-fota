// tests/core/core_web/js/test_cvt.mjs
// L1-JS: ParseCVT/ApplyCVT из src/core_web/web/GetJson.js (см. §D8 плана).

import { test } from 'node:test';
import assert from 'node:assert';
import fs from 'node:fs';
import vm from 'node:vm';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const srcPath = path.join(here, '../../../../src/core_web/web/GetJson.js');
const src = fs.readFileSync(srcPath, 'utf8');

function makeDoc() {
    const byId = new Map();
    const doc = {
        querySelectorAll(sel) {
            const id = String(sel).replace(/^#/, '');
            return byId.has(id) ? [byId.get(id)] : [];
        },
        getElementById(id) {
            return byId.has(id) ? byId.get(id) : null;
        },
        _make(id) {
            const el = { id, value: '', innerHTML: '', checked: false, style: { display: 'none' } };
            byId.set(id, el);
            return el;
        },
    };
    return doc;
}

function loadModule(textData) {
    const doc = makeDoc();
    const ctx = {
        document: doc,
        console,
        fetch: () => Promise.resolve({
            text: () => Promise.resolve(textData),
            json: () => Promise.resolve(JSON.parse(textData)),
        }),
    };
    vm.createContext(ctx);
    vm.runInContext(src, ctx);
    return { ctx, doc };
}

// Нормализация значений, порождённых в VM-области: у них другой Array.prototype,
// поэтому deepStrictEqual на кросс-realm массивах не проходит (reference-equal).
function plain(x) {
    return JSON.parse(JSON.stringify(x));
}

// ============================================================
// ParseCVT
// ============================================================
test('ParseCVT: разбивает строки на поля', () => {
    const { ctx } = loadModule('');
    const rows = ctx.ParseCVT('a|1|input\nb|2|div\n');
    assert.deepStrictEqual(plain(rows), [['a', '1', 'input'], ['b', '2', 'div']]);
});

test('ParseCVT: пропускает пустые строки', () => {
    const { ctx } = loadModule('');
    const rows = ctx.ParseCVT('\n\nx|y|z\n\n');
    assert.deepStrictEqual(plain(rows), [['x', 'y', 'z']]);
});

test('ParseCVT: сохраняет кириллицу и неполные поля', () => {
    const { ctx } = loadModule('');
    const rows = ctx.ParseCVT('name|Привет|input\nonlytwo|2');
    assert.deepStrictEqual(plain(rows[0]), ['name', 'Привет', 'input']);
    assert.deepStrictEqual(plain(rows[1]), ['onlytwo', '2']);
});

test('ParseCVT: пустой ввод', () => {
    const { ctx } = loadModule('');
    assert.deepStrictEqual(plain(ctx.ParseCVT('')), []);
});

// ============================================================
// ApplyCVT
// ============================================================
test('ApplyCVT: input/div/chk/select', async () => {
    const { ctx, doc } = loadModule('inp|v1|input\nbox|<b>h</b>|div\nchk|1|chk\nsel|opt2|select\n');
    doc._make('inp');
    doc._make('box');
    doc._make('chk');
    doc._make('sel');
    await ctx.ApplyCVT('/x');
    assert.strictEqual(doc.getElementById('inp').value, 'v1');
    assert.strictEqual(doc.getElementById('box').innerHTML, '<b>h</b>');
    assert.strictEqual(doc.getElementById('chk').checked, '1');
    assert.strictEqual(doc.getElementById('sel').value, 'opt2');
});

test('ApplyCVT: prefix применяется к id', async () => {
    const { ctx, doc } = loadModule('f|v|input\n');
    doc._make('pre_f');
    await ctx.ApplyCVT('/x', 'pre_');
    assert.strictEqual(doc.getElementById('pre_f').value, 'v');
});

test('ApplyCVT: ветка devicetype', async () => {
    const { ctx, doc } = loadModule('devicetype|avr|input\n');
    const avr = doc._make('avr');
    doc._make('stm32');
    doc._make('gpio');
    avr.style.display = 'none';
    await ctx.ApplyCVT('/x');
    assert.strictEqual(avr.style.display, '');
});

test('ApplyCVT: возвращает исходные данные', async () => {
    const { ctx } = loadModule('a|1|input\n');
    const out = await ctx.ApplyCVT('/x');
    assert.strictEqual(out, 'a|1|input\n');
});
