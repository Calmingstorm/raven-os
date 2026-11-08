/** QMP proof of production fatal HLT, rather than reset or silence. */
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {connect} from 'node:net';
import {mkdtemp,rm,writeFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {checkFault} from './round2-guest-fault-check.mjs';
const [iso,log]=process.argv.slice(2);
assert.ok(iso && log,'usage: round2-guest-qemu-fault.mjs ISO SERIAL_LOG');
const temp=await mkdtemp(join(tmpdir(),'r2-qmp-')),socket=join(temp,'qmp');
const child=spawn('qemu-system-i386',['-cdrom',iso,'-m','128M','-display','none','-serial','stdio',
    '-monitor','none','-no-reboot','-no-shutdown','-qmp',`unix:${socket},server=on,wait=off`],{stdio:['ignore','pipe','pipe']});
let serial='',errors='',stream,wire='',sequence=0;
child.stdout.on('data',b=>{serial+=b;process.stdout.write(b);});child.stderr.on('data',b=>{errors+=b;});
const sleep=ms=>new Promise(r=>setTimeout(r,ms)),pending=new Map();
async function command(execute,args){
    const id=++sequence;
    return new Promise((resolve,reject)=>{
        const timer=setTimeout(()=>{pending.delete(id);reject(Error(`QMP timeout ${execute}`));},5000);
        pending.set(id,{resolve:v=>{clearTimeout(timer);resolve(v);},reject});
        stream.write(JSON.stringify({execute,arguments:args||{},id})+'\n');
    });
}
try{
    const end=Date.now()+30000;
    while(!serial.includes('interrupts disabled; halted')){
        assert.equal(child.exitCode,null,`QEMU exited prematurely: ${errors}`);
        assert.ok(Date.now()<end,`Fatal serial timeout: ${serial}\n${errors}`);await sleep(50);
    }
    checkFault(serial);
    await new Promise((resolve,reject)=>{
        stream=connect(socket);stream.on('error',reject);
        stream.on('data',b=>{
            wire+=b;
            while(wire.includes('\n')){
                const split=wire.indexOf('\n'),line=wire.slice(0,split);wire=wire.slice(split+1);
                if(!line.trim())continue;const value=JSON.parse(line);if(value.QMP)resolve();
                if(pending.has(value.id)){
                    const p=pending.get(value.id);pending.delete(value.id);
                    if(value.error)p.reject(Error(JSON.stringify(value.error)));else p.resolve(value.return);
                }
            }
        });
    });
    await command('qmp_capabilities');await sleep(100);
    const a=await command('human-monitor-command',{'command-line':'info registers'});
    const stable=serial;await sleep(700);
    const b=await command('human-monitor-command',{'command-line':'info registers'});
    for(const value of [a,b]){
        assert.match(value,/HLT=1/,'CPU halted');assert.match(value,/CR2=deadb000/i,'actual CR2');
        const flags=value.match(/EFL=([0-9a-f]+)/i);assert.ok(flags,'EFLAGS reported');
        assert.equal(parseInt(flags[1],16)&0x200,0,'interrupts disabled');
        assert.match(value,/CR3=0*[1-9a-f][0-9a-f]*/i,'CR3 installed');
    }
    assert.equal(a,b,'all QEMU CPU registers stable after halt');assert.equal(serial,stable,'serial stable, no reboot');
    assert.equal(child.exitCode,null,'QEMU remains alive');
    await writeFile(log+'.halt.json',JSON.stringify({samples:[a,b],observationMs:700},null,2)+'\n');
    console.log('PASS QEMU actual HLT=1, CR2, CR3, IF=0, stable registers and serial');
}finally{
    if(stream)stream.destroy();
    if(child.exitCode===null){child.kill('SIGTERM');await new Promise(r=>child.once('exit',r));}
    await writeFile(log,serial+errors);await rm(temp,{recursive:true,force:true});
}
