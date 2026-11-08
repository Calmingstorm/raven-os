import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
export function checkFault(text) {
    assert.equal((text.match(/ROUND2 BOOT/g)||[]).length,1,'one boot, no reboot');
    assert.doesNotMatch(text,/ROUND2 FAIL|ROUND2 PASS all/);
    const expected=text.match(/ROUND2 FAULT trigger CR2=0xDEADB000 expected-error=0x00000002 EIP=(0x[0-9A-F]+)/i);
    assert.ok(expected,'fault instruction EIP exported');
    const report=text.slice(text.indexOf(expected[0])+expected[0].length);
    assert.match(report,/CR2\s*[=:]\s*0xDEADB000/i);
    assert.match(report,new RegExp('EIP\\s*[=:]\\s*'+expected[1],'i'));
    assert.match(report,/(?:err(?:or)?(?:_code)?)\s*[=:]\s*0x00000002/i);
    assert.match(report,/not[- ]present/i);assert.match(report,/write/i);
    assert.match(report,/supervisor|kernel/i);assert.match(report,/halt/i);
}
if (process.argv[1]?.endsWith('round2-guest-fault-check.mjs')) {
    checkFault(await readFile(process.argv[2],'utf8'));
    console.log('PASS production fatal PF serial CR2 EIP error decode and halt');
}
