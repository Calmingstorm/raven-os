#!/usr/bin/env node
/** Disposable production-linked guests under headless Chrome. No writable disks,
 * desktop session, downloads or shared build output. v86 is pinned by web/. */
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {createHash} from 'node:crypto';
import {access,mkdir,mkdtemp,readFile,rm,writeFile} from 'node:fs/promises';
import {dirname,join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {checkFault} from './round2-guest-fault-check.mjs';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const options={};
for(let i=2;i<process.argv.length;i++){
    const arg=process.argv[i];
    if(arg==='--help'){
        console.log('round2-v86-test.sh [--assets DIR] [--playwright PATH | --puppeteer PATH] [--chrome FILE] [--report FILE]\n'+
            'mjs requires --kernels DIR with integrated/fault/early-fault.kernel. v86 0.5.473, BIOS, headless Chrome.');
        process.exit(0);
    }
    if(!['--assets','--playwright','--puppeteer','--chrome','--report','--kernels'].includes(arg)||!process.argv[i+1])throw Error(`Invalid option ${arg}`);
    options[arg.slice(2)]=process.argv[++i];
}
async function first(paths,label){for(const p of paths){try{await access(p);return p;}catch{}}throw Error(`Missing ${label}; searched ${paths.join(', ')}`);}
const assetRoots=(options.assets||process.env.V86_ASSETS)?[resolve(options.assets||process.env.V86_ASSETS)]:
    ['web/dist','web','build/console-v86','../raven-os/build/console-v86'].map(p=>resolve(root,p));
const asset=(name,dirs)=>first(assetRoots.flatMap(base=>dirs.map(d=>join(base,d,name))),name);
const files=new Map([
    ['/libv86.mjs',await asset('libv86.mjs',['','build','node_modules/v86/build'])],
    ['/v86.wasm',await asset('v86.wasm',['','build','node_modules/v86/build'])],
    ['/seabios.bin',await asset('seabios.bin',['','bios','vendor/bios'])],
    ['/vgabios.bin',await asset('vgabios.bin',['','bios','vendor/bios'])],
]);
const kernelDir=resolve(options.kernels||process.env.ROUND2_KERNELS||'');
assert.ok(options.kernels||process.env.ROUND2_KERNELS,'--kernels is mandatory');
const images=new Map();
for(const name of ['integrated','fault','early-fault'])images.set(`/${name}.kernel`,await readFile(join(kernelDir,`${name}.kernel`)));
const sha=b=>createHash('sha256').update(b).digest('hex');
const report={started:new Date().toISOString(),tests:[],errors:[],assets:{},kernels:{},
    limitations:['Synthetic 64MiB mmap inside 128MiB machine. No writable disk or device MMIO access.',
        'Uses pinned v86 private CPU fields only for fatal HLT, EIP, CR2 and interrupt-state proof.',
        'Early fault is immediately after paging_init returns, not injected inside production paging_init.']};
for(const [url,p] of files)report.assets[url]={path:p,sha256:sha(await readFile(p))};
report.v86Pin='0.5.473+g315b2b0';
assert.equal(report.assets['/libv86.mjs'].sha256,'f9f3b4cae7dafb882e10e51aae9137ec52d5aed5ffbc9026f9081c73bbaae4c8','pinned v86 JS');
assert.equal(report.assets['/v86.wasm'].sha256,'2f0c4150f266415b754a84a32e6f8e4c975a1d3abb94c78b047fa30027db1ef1','pinned v86 WASM');
for(const [url,b] of images)report.kernels[url]=sha(b);
const require=createRequire(import.meta.url);
let runtime,kind;
const search=[join(root,'web'),...assetRoots,root];
try{
    if(options.puppeteer||process.env.PUPPETEER_PATH)throw Error('explicit puppeteer');
    const entry=options.playwright?require.resolve(resolve(options.playwright)):require.resolve('playwright-core',{paths:search});
    const imported=await import(pathToFileURL(entry).href);
    runtime=imported.chromium?imported:imported.default;kind='playwright';
}catch(error){
    if(options.playwright)throw error;
    const path=options.puppeteer||process.env.PUPPETEER_PATH;
    const entry=path?require.resolve(resolve(path)):require.resolve('puppeteer-core',{paths:search});
    runtime=(await import(pathToFileURL(entry).href)).default;kind='puppeteer';
}
report.browserRuntime=kind;
const html=name=>`<!doctype html><meta charset="utf-8"><title>Round2 guest</title><script type="module">
import {V86} from '/libv86.mjs';
window.serial='';window.resets=0;window.ready=false;
window.emulator=new V86({wasm_path:'/v86.wasm',memory_size:128*1024*1024,vga_memory_size:8*1024*1024,
 bios:{url:'/seabios.bin'},vga_bios:{url:'/vgabios.bin'},multiboot:{url:'/${name}.kernel'},
 disable_mouse:true,disable_keyboard:true,autostart:false});
emulator.add_listener('serial0-output-byte',c=>window.serial+=String.fromCharCode(c));
emulator.add_listener('cpu-reboot',()=>window.resets++);
emulator.add_listener('emulator-ready',()=>{window.ready=true;emulator.run();});
</script>`;
const server=createServer(async(req,res)=>{
    const p=new URL(req.url,'http://127.0.0.1').pathname;
    try{
        if(/^\/(integrated|fault|early-fault)$/.test(p)){res.setHeader('Content-Type','text/html');res.end(html(p.slice(1)));return;}
        if(images.has(p)){res.end(images.get(p));return;}
        if(!files.has(p)){res.writeHead(404);res.end();return;}
        res.setHeader('Content-Type',p.endsWith('.mjs')?'text/javascript':p.endsWith('.wasm')?'application/wasm':'application/octet-stream');
        res.end(await readFile(files.get(p)));
    }catch(e){report.errors.push(String(e));res.writeHead(500);res.end();}
});
let browser,profile,current;
const reportPath=resolve(options.report||process.env.ROUND2_REPORT||join(kernelDir,'v86-report.json'));
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
async function waitFor(page,source,timeout=90000){
    const fn=s=>window.serial && (new RegExp(s).test(window.serial)||window.serial.includes('ROUND2 FAIL'));
    if(kind==='playwright')await page.waitForFunction(fn,source,{timeout,polling:100});
    else await page.waitForFunction(fn,{timeout,polling:100},source);
}
try{
    await new Promise(r=>server.listen(0,'127.0.0.1',r));profile=await mkdtemp(join(tmpdir(),'round2-chrome-'));
    const executablePath=options.chrome||process.env.CHROME_PATH||'/usr/bin/google-chrome';
    const args=['--disable-dev-shm-usage','--disable-background-networking',...(process.getuid?.()===0?['--no-sandbox']:[])];
    browser=kind==='playwright'?await runtime.chromium.launchPersistentContext(profile,{executablePath,headless:true,args}):
        await runtime.launch({executablePath,headless:true,userDataDir:profile,args});
    for(const name of ['integrated','fault','early-fault']){
        const started=Date.now(),page=await browser.newPage();current=page;
        // v86's Multiboot loader owns port0xF4 and throws HALT for the deliberate
        // debug-exit write. Success still requires every serial assertion marker.
        page.on('pageerror',e=>{if(name!=='integrated'||String(e)!=='HALT')report.errors.push(String(e));});
        await page.goto(`http://127.0.0.1:${server.address().port}/${name}`);
        await waitFor(page,name==='integrated'?'ROUND2 PASS all assertions=':'interrupts disabled; halted');
        const text=await page.evaluate(()=>window.serial);console.log(text);
        const record={name,serial:text,ms:Date.now()-started};
        assert.doesNotMatch(text,/ROUND2 FAIL/);
        if(name==='integrated'){
            for(const stage of ['RAM','heap','MMIO','timeline','all assertions='])assert.ok(text.includes(`ROUND2 PASS ${stage}`),stage);
        }else{
            checkFault(text);await sleep(300);
            const state=()=>page.evaluate(()=>{
                const c=emulator.v86.cpu;
                return {hlt:c.in_hlt[0],eip:c.instruction_pointer[0]>>>0,cr2:c.cr[2]>>>0,cr3:c.cr[3]>>>0,
                    flags:c.get_eflags?c.get_eflags():c.flags[0],instructions:emulator.get_instruction_counter(),serial:window.serial,resets:window.resets};
            });
            const a=await state();await sleep(700);const b=await state();
            assert.equal(a.hlt,1,'CPU actually halted');assert.equal(b.hlt,1,'CPU remains halted');
            assert.equal(a.eip,b.eip,'halt EIP stable');assert.equal(a.cr2,0xdeadb000,'actual CR2');
            assert.equal(a.cr3,b.cr3,'CR3 stable');assert.ok(a.cr3,'CR3 installed');
            assert.equal(a.flags&0x200,0,'interrupts disabled');assert.equal(a.serial,b.serial,'serial stable, no reboot');
            assert.equal(b.resets,0,'no CPU reboot');assert.equal(a.instructions,b.instructions,'no instructions after HLT');
            delete a.serial;delete b.serial;record.haltSamples=[a,b];
        }
        record.status='pass';report.tests.push(record);console.log(`PASS round2 v86 ${name}`);await page.close();current=null;
    }
    assert.deepEqual(report.errors,[],'no browser/runtime errors');
}catch(e){
    report.errors.push(String(e.stack||e));
    if(current)try{report.failureSerial=await current.evaluate(()=>window.serial);}catch{}
    console.error(report.failureSerial||'');throw e;
}finally{
    if(browser)await browser.close();await new Promise(r=>server.close(r));if(profile)await rm(profile,{recursive:true,force:true});
    report.finished=new Date().toISOString();await mkdir(dirname(reportPath),{recursive:true});
    await writeFile(reportPath,JSON.stringify(report,null,2)+'\n');console.log(`round2 v86 report: ${reportPath}`);
}
