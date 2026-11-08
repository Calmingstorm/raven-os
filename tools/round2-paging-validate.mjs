#!/usr/bin/env node
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
export function validateFatal(mode, text) {
    assert.doesNotMatch(text, /ROUND2 FAIL|ROUND2 PASS all/);
    assert.match(text, /ROUND2 TRIGGER/);
    assert.match(text, /EIP=0x[0-9a-f]+/i);
    assert.equal((text.match(/FATAL exception=/g) || []).length, 1, 'one fatal record, no exception storm');
    assert.match(text, /interrupts disabled; halted/);
    if (mode === 3) {
        assert.match(text, /FATAL exception=0x00000006/); return;
    }
    assert.match(text, /FATAL exception=0x0000000E/);
    assert.match(text, new RegExp(`CR2=0x${mode === 1 ? '00800000' : 'D0000000'}`, 'i'));
    const error = text.match(/error=0x([0-9a-f]+)/i);
    assert.ok(error, 'raw hardware error');
    const raw = Number.parseInt(error[1], 16);
    if (mode === 1) assert.equal(raw, 2, 'not-present supervisor write hardware error');
    else {
        // QEMU Pentium III TCG reports 0xA for the reserved PSE bit21 case.
        // Assert the real RSVD/write/user bits, and require the handler's P
        // decoding to agree with the observed raw bit rather than invent it.
        assert.equal(raw & ~1, 10, 'reserved-bit supervisor write hardware error');
    }
    assert.match(text, new RegExp(`cause=${raw & 1 ? 'protection' : 'not-present'} write supervisor`));
    if (mode === 2) assert.match(text, /reserved-bit/);
    assert.doesNotMatch(text, /instruction-fetch/);
    if (mode === 1) assert.doesNotMatch(text, /reserved-bit/);
}
if (process.argv[1]?.endsWith('round2-paging-validate.mjs'))
    validateFatal(Number(process.argv[2]), await readFile(process.argv[3], 'utf8'));
