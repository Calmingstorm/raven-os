#!/usr/bin/env node
/** Synthetic production-linked runtime/ATA tests, not an engine or game test.
 * Only disposable in-memory disks and an isolated headless Chrome profile.
 * The slave uses v86's real lazy async HTTP transport, including its last sector.
 */
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {createHash} from 'node:crypto';
import {access,mkdir,mkdtemp,readFile,rm,writeFile} from 'node:fs/promises';
import {dirname,join,resolve} from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';

const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const options={};
for(let i=2;i<process.argv.length;i++){
    const arg=process.argv[i];
    if(arg==='--help'){
        console.log('round3-v86-test.sh [--assets DIR] [--playwright PATH | --puppeteer PATH] [--chrome FILE] [--report FILE]\n'+
            'Builds disposable synthetic guests and fixtures. mjs requires --kernels DIR --fixtures DIR.\n'+
            'Scenarios: valid, latency (1500 ms per cold slave chunk), corrupt, short, missing, small-master. ROUND3_KEEP=1 retains build/fixtures/report.\n'+
            'No engine, playable WAD, shared build output, host disks or active desktop session.');
        process.exit(0);
    }
    if(!['--assets','--playwright','--puppeteer','--chrome','--report','--kernels','--fixtures'].includes(arg)||!process.argv[i+1])throw Error(`Invalid option ${arg}`);
    options[arg.slice(2)]=process.argv[++i];
}
assert.ok(options.kernels&&options.fixtures,'--kernels and --fixtures are mandatory');
const kernelDir=resolve(options.kernels),fixtureDir=resolve(options.fixtures);
const reportPath=resolve(options.report||process.env.ROUND3_REPORT||join(kernelDir,'v86-report.json'));
const sha=b=>createHash('sha256').update(b).digest('hex');
async function first(paths,label){for(const p of paths){try{await access(p);return p;}catch{}}throw Error(`Missing ${label}; searched ${paths.join(', ')}`);}
const assetRoots=(options.assets||process.env.V86_ASSETS)?[resolve(options.assets||process.env.V86_ASSETS)]:
    ['web/dist','web','../pr6-22919b7/web/dist','build/console-v86'].map(p=>resolve(root,p));
const asset=(name,dirs)=>first(assetRoots.flatMap(base=>dirs.map(d=>join(base,d,name))),name);
const files=new Map([
    ['/libv86.mjs',await asset('libv86.mjs',['','build','node_modules/v86/build'])],
    ['/v86.wasm',await asset('v86.wasm',['','build','node_modules/v86/build'])],
    ['/seabios.bin',await asset('seabios.bin',['','bios','vendor/bios'])],
    ['/vgabios.bin',await asset('vgabios.bin',['','bios','vendor/bios'])],
]);
const kernels=new Map();
for(const mode of [0,1])kernels.set(`/runtime-${mode}.kernel`,await readFile(join(kernelDir,`runtime-${mode}.kernel`)));
const fixtures=new Map();
for(const name of ['valid','corrupt','short','master','small-master'])fixtures.set(name,await readFile(join(fixtureDir,`${name}.img`)));
const scenarios=[
    {name:'valid',mode:0,master:'master',slave:'valid'},
    {name:'latency',mode:0,master:'master',slave:'valid',slaveDelayMs:1500},
    {name:'corrupt',mode:1,master:'master',slave:'corrupt'},
    {name:'short',mode:1,master:'master',slave:'short'},
    {name:'missing',mode:1,master:'master',slave:null},
    {name:'small-master',mode:0,master:'small-master',slave:'valid'},
];
const logicalBytes=28795076;
const chunkBytes=256*1024,imageBytes=28835840;
assert.equal(fixtures.get('valid').length,imageBytes);
assert.equal(fixtures.get('corrupt').length,imageBytes);
assert.equal(imageBytes,110*chunkBytes,'full final v86 chunk is backed');
for(const name of ['valid','corrupt','short'])assert.equal(fixtures.get(name).length%chunkBytes,0);
const report={started:new Date().toISOString(),v86Pin:'0.5.473+g315b2b0',tests:[],errors:[],assets:{},kernels:{},fixtures:{},
    limitations:['Synthetic IWAD and production-linked guest only; not Doom/Freedoom or a bootable distribution test.',
        'v86 private IDE buffer fields are pinned to the verified JS/WASM hashes for host-side in-memory disk comparison.',
        'Lazy HTTP byte accounting covers slave transport only; the master is deliberately a full writable in-memory ArrayBuffer.']};
for(const [url,path] of files)report.assets[url]={path,sha256:sha(await readFile(path))};
assert.equal(report.assets['/libv86.mjs'].sha256,'f9f3b4cae7dafb882e10e51aae9137ec52d5aed5ffbc9026f9081c73bbaae4c8','pinned v86 JS');
assert.equal(report.assets['/v86.wasm'].sha256,'2f0c4150f266415b754a84a32e6f8e4c975a1d3abb94c78b047fa30027db1ef1','pinned v86 WASM');
for(const [url,data] of kernels)report.kernels[url]=sha(data);
for(const [name,data] of fixtures)report.fixtures[name]={bytes:data.length,sha256:sha(data)};
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
const requests=new Map(scenarios.map(s=>[s.name,[]]));
function diskResponse(req,res,data,log,delayMs=0,receivedAt=performance.now()){
    res.setHeader('Accept-Ranges','bytes');res.setHeader('Content-Type','application/octet-stream');
    res.setHeader('Cache-Control','no-store');
    if(req.method==='HEAD'){res.setHeader('Content-Length',data.length);res.end();return;}
    assert.equal(req.method,'GET','disk transport method');
    let start=0,end=data.length-1,status=200;
    if(req.headers.range){
        const m=/^bytes=(\d*)-(\d*)$/.exec(req.headers.range);
        if(!m||(!m[1]&&!m[2])){res.writeHead(416,{'Content-Range':`bytes */${data.length}`});res.end();return;}
        if(!m[1]){start=Math.max(0,data.length-Number(m[2]));if(Number(m[2])===0)start=data.length;}
        else {start=Number(m[1]);if(m[2])end=Math.min(end,Number(m[2]));}
        if(!Number.isSafeInteger(start)||!Number.isSafeInteger(end)||start>end||start>=data.length){
            res.writeHead(416,{'Content-Range':`bytes */${data.length}`});res.end();return;
        }
        status=206;res.setHeader('Content-Range',`bytes ${start}-${end}/${data.length}`);
    }
    const length=end-start+1;
    log?.push({range:req.headers.range||null,start,end,bytes:length,status,delayMs,
        responseWaitMs:performance.now()-receivedAt});
    res.writeHead(status,{'Content-Length':length});res.end(data.subarray(start,end+1));
}
const html=s=>`<!doctype html><meta charset="utf-8"><title>Round3 ${s.name}</title><script type="module">
import {V86} from '/libv86.mjs';
window.serial='';window.resets=0;window.ready=false;
window.masterBuffer=await (await fetch('/master/${s.name}.img')).arrayBuffer();
window.emulator=new V86({wasm_path:'/v86.wasm',memory_size:128*1024*1024,vga_memory_size:8*1024*1024,
 bios:{url:'/seabios.bin'},vga_bios:{url:'/vgabios.bin'},multiboot:{url:'/runtime-${s.mode}.kernel'},
 hda:{buffer:window.masterBuffer},${s.slave?`hdb:{url:'/slave/${s.name}.img',async:true,size:${fixtures.get(s.slave).length},fixed_chunk_size:${chunkBytes}},`:''}
 disable_mouse:true,disable_keyboard:true,disable_speaker:true,autostart:false});
emulator.add_listener('serial0-output-byte',c=>window.serial+=String.fromCharCode(c));
emulator.add_listener('cpu-reboot',()=>window.resets++);
emulator.add_listener('emulator-ready',()=>{window.ready=true;emulator.run();});
</script>`;
const server=createServer(async(req,res)=>{
    const path=new URL(req.url,'http://127.0.0.1').pathname;
    try{
        const scenario=scenarios.find(s=>path===`/${s.name}`);
        if(scenario){res.setHeader('Content-Type','text/html');res.end(html(scenario));return;}
        if(path==='/range-selftest.img'){diskResponse(req,res,fixtures.get('valid'));return;}
        const disk=/^\/(master|slave)\/([a-z-]+)\.img$/.exec(path);
        if(disk){
            const s=scenarios.find(s=>s.name===disk[2]),name=s?.[disk[1]];
            if(!name){res.writeHead(404);res.end();return;}
            const receivedAt=performance.now();
            const delayMs=disk[1]==='slave'&&req.headers.range?(s.slaveDelayMs||0):0;
            if(delayMs)await new Promise(r=>setTimeout(r,delayMs));
            diskResponse(req,res,fixtures.get(name),disk[1]==='slave'?requests.get(s.name):null,delayMs,receivedAt);return;
        }
        if(kernels.has(path)){res.end(kernels.get(path));return;}
        if(!files.has(path)){res.writeHead(404);res.end();return;}
        res.setHeader('Content-Type',path.endsWith('.mjs')?'text/javascript':path.endsWith('.wasm')?'application/wasm':'application/octet-stream');
        res.end(await readFile(files.get(path)));
    }catch(e){report.errors.push(String(e));if(!res.headersSent)res.writeHead(500);res.end();}
});
let browser,profile,current;
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
async function waitForSerial(page){
    const fn=()=>window.serial&&(window.serial.includes('ROUND3 PASS all assertions=')||window.serial.includes('ROUND3 FAIL'));
    if(kind==='playwright')await page.waitForFunction(fn,null,{timeout:90000,polling:100});
    else await page.waitForFunction(fn,{timeout:90000,polling:100});
}
async function transportSelftest(base){
    const data=fixtures.get('valid');
    for(const [range,start,end] of [['bytes=0-3',0,3],[`bytes=${data.length-512}-`,data.length-512,data.length-1],['bytes=-3',data.length-3,data.length-1],[`bytes=${data.length-2}-${data.length+20}`,data.length-2,data.length-1]]){
        const r=await fetch(`${base}/range-selftest.img`,{headers:{Range:range}});
        assert.equal(r.status,206);assert.equal(r.headers.get('content-range'),`bytes ${start}-${end}/${data.length}`);
        assert.equal(r.headers.get('content-length'),String(end-start+1));
        assert.deepEqual(Buffer.from(await r.arrayBuffer()),data.subarray(start,end+1));
    }
    for(const range of [`bytes=${data.length}-`,'bytes=5-4','bytes=0-1,3-4','bytes=-0']){
        const r=await fetch(`${base}/range-selftest.img`,{headers:{Range:range}});
        assert.equal(r.status,416);assert.equal(r.headers.get('content-range'),`bytes */${data.length}`);await r.arrayBuffer();
    }
    report.rangeServerSelftest='pass: exact bounded/open/suffix/clipped ranges, invalid and unsatisfiable ranges rejected';
}
try{
    await new Promise(r=>server.listen(0,'127.0.0.1',r));
    const base=`http://127.0.0.1:${server.address().port}`;
    await transportSelftest(base);
    profile=await mkdtemp(join(kernelDir,'round3-chrome-'));
    const executablePath=options.chrome||process.env.CHROME_PATH||'/usr/bin/google-chrome';
    const args=['--disable-dev-shm-usage','--disable-background-networking',...(process.getuid?.()===0?['--no-sandbox']:[])];
    browser=kind==='playwright'?await runtime.chromium.launchPersistentContext(profile,{executablePath,headless:true,args}):
        await runtime.launch({executablePath,headless:true,userDataDir:profile,args});
    for(const s of scenarios){
        const started=Date.now(),page=await browser.newPage();current=page;
        const pageErrors=[],debugExits=[];
        page.on('pageerror',e=>pageErrors.push(String(e.message||e)));
        page.on('console',m=>{const text=m.text();if(text.startsWith('Test exited with code '))debugExits.push(text);});
        await page.goto(`${base}/${s.name}`);
        await waitForSerial(page);
        // v86 Multiboot installs the debug-exit port itself and throws HALT.
        // A HALT is accepted only together with success markers and status 0x10.
        for(let n=0;n<50&&!pageErrors.length;n++)await sleep(100);
        const serial=await page.evaluate(()=>window.serial);console.log(serial);
        assert.doesNotMatch(serial,/ROUND3 FAIL/);
        for(const stage of ['runtime','clock','libc',s.mode===0?'WAD':'WAD refusal','master unchanged','all assertions='])
            assert.ok(serial.includes(`ROUND3 PASS ${stage}`),`${s.name}: ${stage}`);
        assert.deepEqual(pageErrors,['HALT'],'only the expected debug-exit HALT');
        assert.deepEqual(debugExits,['Test exited with code 0x10'],'guest success debug-exit status');
        const master=await page.evaluate(async()=>{
            const disk=emulator.v86.cpu.devices.ide.primary.master.buffer;
            if(!(disk.buffer instanceof ArrayBuffer)||disk.buffer!==window.masterBuffer||typeof disk.set!=='function')
                throw Error('Master must be the same writable in-memory ArrayBuffer, not a read-only transport');
            const buffer=await new Promise(resolve=>disk.get_buffer(resolve));
            const digest=await crypto.subtle.digest('SHA-256',buffer);
            return {sha256:Array.from(new Uint8Array(digest),v=>v.toString(16).padStart(2,'0')).join(''),bytes:buffer.byteLength,
                sameWritableBuffer:true,resets:window.resets};
        });
        assert.equal(master.resets,0,'no guest reboot');
        assert.equal(master.sha256,report.fixtures[s.master].sha256,'actual emulated master unchanged, host byte hash');
        assert.equal(master.bytes,fixtures.get(s.master).length);
        const ranges=requests.get(s.name),totalBytes=ranges.reduce((n,r)=>n+r.bytes,0);
        if(s.slave){
            assert.ok(ranges.length,'lazy slave requests observed');
            for(const r of ranges){assert.equal(r.status,206);assert.ok(r.range);assert.ok(r.start>=0&&r.end<fixtures.get(s.slave).length);assert.equal(r.bytes,chunkBytes,'no short final chunk');assert.equal(r.start%chunkBytes,0);}
        }else assert.equal(ranges.length,0,'no slave disk attached');
        if(s.mode===0){
            assert.ok(totalBytes<fixtures.get(s.slave).length,'sparse WAD read, not full image transfer');
            const finalStart=Math.floor((logicalBytes-1)/512)*512;
            assert.ok(ranges.some(r=>r.start<=finalStart&&r.end>=finalStart+511),'final partial logical sector fetched including physical padding');
        }
        if(s.slaveDelayMs){
            assert.ok(ranges.some(r=>r.start>0),'cold chunk beyond v86 initialization prefetch was fetched');
            for(const r of ranges){
                assert.equal(r.delayMs,1500,'each cold range delayed on real HTTP transport');
                assert.ok(r.responseWaitMs>=1490,'server really waited for the latency, not just guest-clock simulation');
            }
        }
        report.tests.push({name:s.name,status:'pass',serial,ms:Date.now()-started,debugExits,pageErrors,master,
            slave:{fixture:s.slave,async:!!s.slave,fixedChunkSize:chunkBytes,size:s.slave?fixtures.get(s.slave).length:0,totalBytes,ranges}});
        console.log(`PASS round3 v86 ${s.name}; slave HTTP bytes=${totalBytes}; master SHA-256 unchanged`);
        await page.close();current=null;
    }
    assert.deepEqual(report.errors,[],'no HTTP/browser errors');
}catch(e){
    report.errors.push(String(e.stack||e));
    if(current)try{report.failureSerial=await current.evaluate(()=>window.serial);}catch{}
    console.error(report.failureSerial||'');throw e;
}finally{
    if(browser)await browser.close();await new Promise(r=>server.close(r));if(profile)await rm(profile,{recursive:true,force:true});
    report.finished=new Date().toISOString();await mkdir(dirname(reportPath),{recursive:true});
    await writeFile(reportPath,JSON.stringify(report,null,2)+'\n');console.log(`round3 v86 report: ${reportPath}`);
}
