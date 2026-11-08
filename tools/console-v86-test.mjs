#!/usr/bin/env node
/** Optional isolated guest test, independent of web application code.
 * Build separately; supply v86@0.5.473 and SeaBIOS/VGA BIOS, no downloads here.
 * tools/console-v86-test.sh --assets DIR --puppeteer /path/to/puppeteer-core
 * Assets may be flat web/dist, web, or build/console-v86 containing
 * node_modules/v86/build and bios/{seabios,vgabios}.bin.
 * Environment: V86_ASSETS, PUPPETEER_PATH, CHROME_PATH, V86_KERNEL, V86_REPORT.
 * No real disks or active desktop session; tests PS/2 and actual screen adapter.
 * Web application's focus/fullscreen/download/restart/failure UX is out of scope.
 */
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { createRequire } from 'node:module';
import { createHash } from 'node:crypto';
import { access, mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath, pathToFileURL } from 'node:url';
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const options = {};
for (let i = 2; i < process.argv.length; i++) {
    const arg = process.argv[i];
    if (arg === '--help' || arg === '-h') {
        console.log('Optional v86 test. Build kernel separately. No automatic installs.\n' +
            'Usage: tools/console-v86-test.sh [--assets DIR] [--puppeteer PATH]\n' +
            '       [--chrome FILE] [--kernel FILE] [--report FILE]\n' +
            'Requires v86@0.5.473, BIOS files, puppeteer-core and Chrome.');
        process.exit(0);
    }
    if (!['--assets', '--puppeteer', '--chrome', '--kernel', '--report'].includes(arg) ||
        !process.argv[i + 1] || process.argv[i + 1].startsWith('--'))
        throw new Error(`Unknown option or missing value: ${arg}`);
    options[arg.slice(2)] = process.argv[++i];
}
async function first(paths, description) {
    for (const path of paths) { try { await access(path); return path; } catch {} }
    throw new Error(`Missing ${description}. Looked in: ${paths.join(', ')}`);
}
const assetRoots = (options.assets || process.env.V86_ASSETS) ? [resolve(options.assets || process.env.V86_ASSETS)] :
    ['web/dist', 'web', 'build/console-v86'].map(p => join(root, p));
const asset = (name, dirs) => first(assetRoots.flatMap(base => dirs.map(dir => join(base, dir, name))), name);
const files = new Map([
    ['/libv86.mjs', await asset('libv86.mjs', ['', 'build', 'node_modules/v86/build'])],
    ['/v86.wasm', await asset('v86.wasm', ['', 'build', 'node_modules/v86/build'])],
    ['/seabios.bin', await asset('seabios.bin', ['', 'bios', 'vendor/bios'])],
    ['/vgabios.bin', await asset('vgabios.bin', ['', 'bios', 'vendor/bios'])],
    ['/ravenos.kernel', await first([resolve(options.kernel || process.env.V86_KERNEL || join(root, 'build/ravenos.kernel'))], 'built kernel')],
]);
const require = createRequire(import.meta.url);
const ppPath = options.puppeteer || process.env.PUPPETEER_PATH;
let entry;
try { entry = ppPath ? require.resolve(resolve(ppPath)) : require.resolve('puppeteer-core', { paths: [...assetRoots, root] }); }
catch { throw new Error('Missing puppeteer-core. Supply --puppeteer /path/to/node_modules/puppeteer-core.'); }
const { default: puppeteer } = await import(pathToFileURL(entry).href);
const reportPath = resolve(options.report || process.env.V86_REPORT || join(root, 'build/console-v86/report.json'));
const report = { started: new Date().toISOString(), assets: Object.fromEntries(files), tests: [], errors: [],
    limitations: ['Guest only; no web application UX, disks or persistent state.',
        'Multiboot flags also read at pinned v86 0.5.473 loader metadata address 0x7c00.',
        'Headless Chrome, temporary profile, no active graphical session.'] };
// Capture the exact ELF once so a separate later rebuild cannot change this run.
const kernelImage=await readFile(files.get('/ravenos.kernel'));
report.kernelSha256=createHash('sha256').update(kernelImage).digest('hex');
const html = `<!doctype html><meta charset="utf-8"><title>RavenOS guest test</title>
<div id="screen"><div style="white-space:pre;font:14px monospace;line-height:14px"></div><canvas></canvas></div>
<script type="module">
import { V86 } from '/libv86.mjs';
window.emulator = new V86({wasm_path:'/v86.wasm',memory_size:128*1024*1024,
 vga_memory_size:8*1024*1024,screen:{container:document.querySelector('#screen')},
 bios:{url:'/seabios.bin'},vga_bios:{url:'/vgabios.bin'},
 multiboot:{url:'/ravenos.kernel'},disable_mouse:true,autostart:false});
window.observed={cursor:null,scanline:null,cells:{},cursorCalls:0,scanlineCalls:0};
emulator.add_listener('emulator-ready',()=>{
const screen = emulator.screen_adapter;
for(const name of ['update_cursor','update_cursor_scanline','put_char']) {
 const original=screen[name];
 screen[name]=function(...args) {
  if(name==='update_cursor'){observed.cursor=args;observed.cursorCalls++;}
  if(name==='update_cursor_scanline'){observed.scanline=args;observed.scanlineCalls++;}
  if(name==='put_char')observed.cells[args[0]*80+args[1]]=args;
  return original.apply(this,args);
 };
}
emulator.run();
});
</script>`;
const server = createServer(async (req, res) => {
    const path = new URL(req.url, 'http://127.0.0.1').pathname;
    try {
        if (path === '/') { res.setHeader('Content-Type', 'text/html'); res.end(html); return; }
        if (!files.has(path)) { res.writeHead(404); res.end(); return; }
        res.setHeader('Content-Type', path.endsWith('.mjs') ? 'text/javascript' : path.endsWith('.wasm') ? 'application/wasm' : 'application/octet-stream');
        res.end(path==='/ravenos.kernel'?kernelImage:await readFile(files.get(path)));
    } catch (error) { res.writeHead(500); res.end('Fixture read failed'); report.errors.push(String(error)); }
});
let browser, page, profile;
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const snapshot = () => page.evaluate(() => ({rows:emulator.screen_adapter.get_text_screen().map(r=>r.trimEnd()),...observed}));
const text = async () => (await snapshot()).rows.join('\n');
const send = async value => { await page.evaluate(async v => emulator.keyboard_send_text(v, 8), value); await sleep(100); };
// Deliberately omit v86's optional per-character delay to reproduce real paste.
const burst = async value => { await page.evaluate(v => emulator.keyboard_send_text(v), value); await sleep(140); };
const codes = async value => { await page.evaluate(v => emulator.keyboard_send_scancodes(v), value); await sleep(140); };
const key = async code => codes(Array.isArray(code) ? [0xE0, code[0], 0xE0, code[0] | 0x80] : [code, code | 0x80]);
const ctrlU = () => codes([0x1D, 0x16, 0x96, 0x9D]);
const shifted = code => codes([0x2A, 0xE0, code, 0xE0, code | 0x80, 0xAA]);
const waitText = async pattern => {
    await page.waitForFunction(source => window.emulator?.screen_adapter && new RegExp(source).test(emulator.screen_adapter.get_text_screen().join('\n')), {timeout:15000,polling:50}, pattern.source);
    await sleep(150);
};
async function command(value, expected) {
    await ctrlU(); await send(value); await key(0x1C);
    if (expected) await waitText(expected); else await sleep(150);
    const screen = await snapshot();
    assert.equal(screen.rows[screen.cursor[0]], 'raven>', `Idle prompt after ${value}`);
    return screen;
}
function input(screen) {
    const row = screen.rows.findLastIndex(r => r.startsWith('raven>'));
    assert.ok(row >= 0, 'Prompt rendered');
    return {row, value:[screen.rows[row].slice(7), ...screen.rows.slice(row+1)].join('').trimEnd()};
}
async function expectInput(value, offset=value.length) {
    const screen=await snapshot(), prompt=input(screen);
    assert.equal(prompt.value,value,'Rendered input');
    assert.deepEqual(screen.cursor,[prompt.row+Math.floor((7+offset)/80),(7+offset)%80],'Hardware cursor');
    assert.equal(screen.scanline[2],true,'Hardware cursor visible');
    return screen;
}
async function test(name, body) {
    const started=Date.now();report.activeTest=name;await body();
    report.tests.push({name,status:'pass',ms:Date.now()-started}); console.log(`PASS ${name}`);
    delete report.activeTest;
}
try {
    await new Promise(resolve => server.listen(0,'127.0.0.1',resolve));
    profile=await mkdtemp(join(tmpdir(),'ravenos-v86-'));
    browser=await puppeteer.launch({executablePath:options.chrome||process.env.CHROME_PATH||'/usr/bin/google-chrome',
        headless:true,userDataDir:profile,args:['--disable-dev-shm-usage','--disable-background-networking',
            ...(process.getuid?.()===0?['--no-sandbox']:[])]});
    page=await browser.newPage(); page.on('pageerror',error=>report.errors.push(String(error)));
    await page.goto(`http://127.0.0.1:${server.address().port}/`); await waitText(/raven> /);
    await test('direct Multiboot mmap-only boot, PMM and heap',async()=>{
        const flags=await page.evaluate(()=>{const bytes=emulator.read_memory(0x7C00,4);return new DataView(bytes.buffer,bytes.byteOffset,4).getUint32(0,true);});
        assert.equal(flags,0x40,'Pinned v86 mmap-only flags');report.multibootFlags='0x40';
        const boot=(await command('boot',/memory source: validated mmap/)).rows.join('\n');
        assert.match(boot,/flags:\s+0x00000040/);
        const mem=(await command('mem',/kernel_start:/)).rows.join('\n');
        assert.match(mem,/kernel_start:\s+0x00100000/);
        const free=Number(mem.match(/free_frames:\s+(\d+)/)?.[1]);
        assert.ok(free>0,'PMM nonzero free frames');report.freeFrames=free;
        const heap=(await command('heap',/kernel heap/)).rows.join('\n');
        assert.match(heap,/size:\s+4096 bytes/);assert.doesNotMatch(heap,/base:\s+0x00000000/);
        report.bootScreen=heap;
    });
    await test('PS/2 insertion, movement, Delete and Backspace',async()=>{
        await send('hepl');await key([0x47]);await key([0x4D]);await key([0x4D]);
        await key([0x53]);await expectInput('hel',2);
        await send('l');await key([0x4F]);await key(0x0E);await send('p');
        await expectInput('help');await key(0x1C);await waitText(/RavenOS commands/);
    });
    await test('history restores draft and its mid-line cursor',async()=>{
        await command('about',/tiny 32-bit research OS/);
        await send('draft123');await key([0x47]);
        for(let i=0;i<3;i++)await key([0x4D]);
        await key([0x48]);await expectInput('about');
        await key([0x48]);await expectInput('help');
        await key([0x50]);await expectInput('about');
        await key([0x50]);await expectInput('draft123',3);
    });
    await test('Ctrl+U from middle clears the ENTIRE line',async()=>{
        await ctrlU();await expectInput('');await send('about');await expectInput('about');
        await key(0x1C);await waitText(/tiny 32-bit research OS/);
    });
    await test('unique, common-prefix and ambiguous completion',async()=>{
        await send('abo');await key(0x0F);await expectInput('about');await ctrlU();
        await send('sw');await key(0x0F);await expectInput('switchtty');await ctrlU();
        await send('pro');await key(0x0F);await expectInput('proc');await ctrlU();
        await send('he');await key(0x0F);await expectInput('he');
        assert.match(await text(),/help\s+heap\s+health/);await ctrlU();
    });
    await test('wrapped redraw and hardware cursor cross row boundary',async()=>{
        await command('help controls',/Console controls/);
        await send('a'.repeat(100));await expectInput('a'.repeat(100));
        await key([0x47]);await expectInput('a'.repeat(100),0);
        for(let i=0;i<73;i++)await key([0x4D]);
        await expectInput('a'.repeat(100),73);
        await key([0x4B]);await expectInput('a'.repeat(100),72);
        await send('Z');await expectInput('a'.repeat(72)+'Z'+'a'.repeat(28),73);
        await key([0x53]);await key(0x0E);await expectInput('a'.repeat(99),72);
        await ctrlU();await expectInput('');
    });
    await test('unpaced paste preserves 150/159-character lines and Enter',async()=>{
        for (const [length,character] of [[150,'x'],[159,'x'],[159,'X']]) {
            await command('clear');
            const value=character.repeat(length);
            await burst(value+'\n');
            await waitText(/unknown command:/);
            const screen=await snapshot(), prompt=input(screen);
            // Read the wrapped result separately from the echoed input, so
            // the test detects any missing character, not merely lost Enter.
            assert.equal(screen.rows.slice(0,prompt.row).join('').split('unknown command: ')[1],value);
            await expectInput('');
            await key([0x48]);await expectInput(value);await ctrlU();
        }
        assert.match((await command('stats',/keyboard_drops=/)).rows.join('\n'),/keyboard_drops=0\b/);
    });
    await test('help and every topic fit 80x25 viewport',async()=>{
        for(const [topic,header] of [['','RavenOS commands'],['system','System and inspection'],
            ['memory','Memory'],['files','RAM files'],['disk','Disk (scratch'],
            ['net','Prototype loopback'],['runtime','Prototype runtime'],['controls','Console controls']]){
            await command('clear');const screen=await command(`help${topic?' '+topic:''}`);
            assert.ok(screen.rows.some(r=>r.startsWith(header)),`Help header retained: ${topic}`);
            assert.ok(input(screen).row<25,'Help prompt fits');
            assert.ok(screen.rows.every(r=>r.length<=80));
        }
    });
    await test('scrollback retains rendered colors and hides/restores cursor',async()=>{
        for(let i=0;i<12;i++)await command('about');
        const live=await snapshot();await shifted(0x49);const back=await snapshot();
        assert.notDeepEqual(back.rows,live.rows,'PgUp changes viewport');
        assert.equal(back.scanline[2],false,'Cursor hidden in scrollback');
        const row=back.rows.findIndex(r=>r.startsWith('raven>'));
        assert.ok(row>=0,'Colored prompt retained');
        assert.equal(back.cells[row*80][4],0,'Prompt black background');
        const livePrompt=live.rows.findIndex(r=>r.startsWith('raven>'));
        const foreground=back.cells[row*80][5];
        assert.equal(foreground,live.cells[livePrompt*80][5],'Prompt foreground retained across scrollback');
        assert.ok(((foreground>>8)&255)>(foreground&255),'Prompt rendered green');
        await shifted(0x51);const restored=await snapshot();
        assert.deepEqual(restored.rows,live.rows,'Live text restored');
        assert.deepEqual(restored.cells,live.cells,'All rendered cells/colors restored');
        assert.deepEqual(restored.cursor,live.cursor);assert.equal(restored.scanline[2],true);
        await shifted(0x49);await send('x');await expectInput('x');await ctrlU();
    });
    await test('guest Shift+Esc leaves input, screen and cursor unchanged',async()=>{
        await send('draft');await key([0x47]);await key([0x4D]);const before=await snapshot();
        await codes([0x2A,0x01,0x81,0xAA]);const after=await snapshot();
        assert.deepEqual(after.rows,before.rows);assert.deepEqual(after.cursor,before.cursor);
        assert.deepEqual(after.scanline,before.scanline);await expectInput('draft',1);await ctrlU();
    });
    report.finalScreen=await snapshot();
    assert.ok(report.finalScreen.cursorCalls>0&&report.finalScreen.scanlineCalls>0,'Real adapter cursor hooks called');
    assert.deepEqual(report.errors,[],'No browser page errors'); report.status='pass';
} catch(error) {
    report.status='fail';report.failure=error.stack||String(error);process.exitCode=1;
    if(page)try{report.failureScreen=await snapshot();}catch{}
    console.error(report.failure);
} finally {
    if(browser)await browser.close();
    await new Promise(resolve=>server.close(resolve));
    if(profile)await rm(profile,{recursive:true,force:true});
    report.finished=new Date().toISOString();await mkdir(dirname(reportPath),{recursive:true});
    await writeFile(reportPath,JSON.stringify(report,null,2)+'\n');console.log(`Report: ${reportPath}`);
}
