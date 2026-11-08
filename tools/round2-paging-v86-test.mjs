#!/usr/bin/env node
/* Optional headless browser guest tests. Reuses supplied v86+BIOS+puppeteer
 * assets only; never downloads dependencies or attaches to a desktop browser.
 * Usage: node tools/round2-paging-v86-test.mjs --assets DIR [--playwright DIR | --puppeteer DIR]
 *        [--chrome FILE] [--keep]
 * Builds each small production-linked guest in mktemp, never shared build/.
 * v86 reserved-bit emulation is not asserted: use the QEMU Pentium III test. */
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { createRequire } from 'node:module';
import { createHash } from 'node:crypto';
import { access, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { execFileSync } from 'node:child_process';
import { validateFatal } from './round2-paging-validate.mjs';
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const opts = {};
for (let i = 2; i < process.argv.length; i++) {
    const arg = process.argv[i];
    if (arg === '--keep') { opts.keep = true; continue; }
    if (arg === '--help') {
        console.log('node tools/round2-paging-v86-test.mjs [--assets DIR] [--playwright DIR | --puppeteer DIR] [--chrome FILE] [--keep]');
        process.exit(0);
    }
    assert.ok(['--assets', '--playwright', '--puppeteer', '--chrome'].includes(arg) && process.argv[i + 1], `Bad option ${arg}`);
    opts[arg.slice(2)] = process.argv[++i];
}
async function first(paths, name) {
    for (const path of paths) { try { await access(path); return path; } catch {} }
    throw new Error(`Missing ${name}, supply existing assets. Tried: ${paths.join(', ')}`);
}
const assetRoots = (opts.assets || process.env.V86_ASSETS) ? [resolve(opts.assets || process.env.V86_ASSETS)] :
    ['web/dist', 'web', 'build/console-v86'].map(p => join(root, p));
const asset = name => first(assetRoots.flatMap(base => ['', 'build', 'node_modules/v86/build', 'bios', 'vendor/bios'].map(p => join(base, p, name))), name);
const files = new Map(await Promise.all(['libv86.mjs', 'v86.wasm', 'seabios.bin', 'vgabios.bin'].map(async name => [`/${name}`, await asset(name)])));
const require = createRequire(import.meta.url);
const search = [join(root, 'web'), ...assetRoots, root];
let runtime, kind;
try {
    if (opts.puppeteer || process.env.PUPPETEER_PATH) throw new Error('explicit puppeteer');
    const entry = opts.playwright ? require.resolve(resolve(opts.playwright)) : require.resolve('playwright-core', {paths:search});
    const imported = await import(pathToFileURL(entry).href);
    runtime = imported.chromium ? imported : imported.default; kind = 'playwright';
} catch (error) {
    if (opts.playwright) throw error;
    const pp = opts.puppeteer || process.env.PUPPETEER_PATH;
    const entry = pp ? require.resolve(resolve(pp)) : require.resolve('puppeteer-core', {paths:search});
    runtime = (await import(pathToFileURL(entry).href)).default; kind = 'puppeteer';
}
const out = await mkdtemp(join(tmpdir(), 'raven-round2-paging-v86-'));
const report = { browserRuntime:kind, tests:[], limitations:['No real devices, disks or full kernel; synthetic mmap backed by 128MiB emulator RAM.',
    'QEMU, not v86, verifies reserved-bit hardware page faults.'], errors:[] };
let kernelImage, browser;
const html = `<!doctype html><meta charset="utf-8"><title>Isolated paging fixture</title>
<script type="module">
import { V86 } from '/libv86.mjs';
window.serial='';window.resets=0;
window.emulator=new V86({wasm_path:'/v86.wasm',memory_size:128*1024*1024,
 bios:{url:'/seabios.bin'},vga_bios:{url:'/vgabios.bin'},
 multiboot:{url:'/fixture.kernel'},disable_mouse:true,autostart:false});
emulator.add_listener('serial0-output-byte',byte=>window.serial+=String.fromCharCode(byte));
emulator.add_listener('cpu-reboot',()=>window.resets++);
emulator.add_listener('emulator-ready',()=>emulator.run());
</script>`;
const server = createServer(async (req, res) => {
    const path = new URL(req.url, 'http://127.0.0.1').pathname;
    try {
        if (path === '/') { res.setHeader('Content-Type', 'text/html'); res.end(html); return; }
        if (path === '/fixture.kernel') { res.end(kernelImage); return; }
        if (!files.has(path)) { res.writeHead(404); res.end(); return; }
        res.setHeader('Content-Type', path.endsWith('.mjs') ? 'text/javascript' : path.endsWith('.wasm') ? 'application/wasm' : 'application/octet-stream');
        res.end(await readFile(files.get(path)));
    } catch (error) { report.errors.push(String(error)); res.writeHead(500); res.end(); }
});
try {
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const launch = {executablePath:opts.chrome || process.env.CHROME_PATH || '/usr/bin/google-chrome',
        headless:true,args:['--disable-dev-shm-usage','--disable-background-networking',
            ...(process.getuid?.() === 0 ? ['--no-sandbox'] : [])]};
    browser = kind === 'playwright' ? await runtime.chromium.launchPersistentContext(join(out, 'chrome-profile'), launch) :
        await runtime.launch({...launch,userDataDir:join(out, 'chrome-profile')});
    for (const mode of [0, 1, 3, 4, 5, 6]) {
        const build = execFileSync('bash', [join(root,'tools/round2-paging-build.sh'),out,String(mode)], {encoding:'utf8'});
        process.stdout.write(build);
        kernelImage = await readFile(join(out, `mode-${mode}/round2-paging.kernel`));
        const test = {mode, kernelSha256:createHash('sha256').update(kernelImage).digest('hex'), pageErrors:[]};
        report.tests.push(test);
        const page = await browser.newPage();
        page.on('pageerror', error => test.pageErrors.push(String(error)));
        await page.goto(`http://127.0.0.1:${server.address().port}/`);
        try {
            const ready = () => /ROUND2 FAIL|ROUND2 EXIT port=0x00F4 value=0x10|interrupts disabled; halted/.test(window.serial);
            if (kind === 'playwright') await page.waitForFunction(ready, undefined, {timeout:90000,polling:50});
            else await page.waitForFunction(ready, {timeout:90000,polling:50});
        } finally {
            test.serial = await page.evaluate(() => window.serial);
            await writeFile(join(out, `mode-${mode}/serial.log`), test.serial);
            process.stdout.write(test.serial);
        }
        assert.doesNotMatch(test.serial, /ROUND2 FAIL/);
        if (mode === 1 || mode === 3) {
            validateFatal(mode, test.serial);
            await new Promise(resolve => setTimeout(resolve, 300));
            const state = () => page.evaluate(() => {
                const cpu = emulator.v86.cpu;
                return {halted:cpu.in_hlt[0],eip:cpu.instruction_pointer[0]>>>0,cr2:cpu.cr[2]>>>0,
                    flags:cpu.get_eflags(),instructions:emulator.get_instruction_counter(),serial:window.serial,resets:window.resets};
            });
            const a = await state(); await new Promise(resolve => setTimeout(resolve, 300)); const b = await state();
            assert.equal(a.halted, 1, 'fatal CPU actually in HLT');
            assert.equal(a.flags & 0x200, 0, 'fatal interrupts disabled');
            assert.equal(a.resets, 0, 'fatal guest did not reboot');
            assert.deepEqual(a, b, 'halted CPU and serial remain stable');
            if (mode === 1) assert.equal(a.cr2, 0x800000, 'CPU CR2 matches fatal serial');
            delete a.serial; delete b.serial; test.haltSamples = [a, b];
            assert.deepEqual(test.pageErrors, [], 'fatal guest has no browser exception');
        } else {
            assert.match(test.serial, /ROUND2 PASS all assertions=/);
            assert.match(test.serial, /ROUND2 PASS all assertions=\d+\s+ROUND2 EXIT port=0x00F4 value=0x10/);
            // v86's Multiboot debug-exit write at port0xF4 throws literal
            // HALT. Accept that sentinel only after the guest PASS marker.
            assert.ok(test.pageErrors.every(error => error === 'HALT'), 'no unexpected browser exceptions');
        }
        test.status = 'pass';
        console.log(`round2 paging v86 mode ${mode} passed`);
        // Closing this disposable page terminates the entire guest. Awaiting
        // v86.stop() can hang on guests halted in CLI/HLT, so do not depend on
        // an emulated asynchronous stop acknowledgment for test teardown.
        await page.close();
    }
    assert.deepEqual(report.errors, []);
} catch (error) {
    report.errors.push(String(error)); process.exitCode = 1; console.error(error);
} finally {
    if (browser) await browser.close();
    await new Promise(resolve => server.close(resolve));
    await writeFile(join(out, 'report.json'), JSON.stringify(report, null, 2));
    if (opts.keep) console.log(`evidence: ${out}`); else await rm(out, {recursive:true,force:true});
}
