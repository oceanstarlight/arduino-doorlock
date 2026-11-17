const {chromium}=require('playwright');const fs=require('fs');
const FILE=require('path').join(__dirname,'../../docs/wiring.html');
const TRIALS=+(process.argv[2]||40);
(async()=>{
 let src=fs.readFileSync(FILE,'utf8');
 // 캐시 비운 상태로 로드
 const noCache=src.replace(/const ROUTE_CACHE = [\s\S]*?;\n<\/script>/,'const ROUTE_CACHE = {};\n</script>');
 fs.writeFileSync(require('os').tmpdir()+'/pc.html','<!doctype html><meta charset=utf-8><body>'+noCache);
 const b=await chromium.launch();const p=await b.newPage();p.on('pageerror',e=>console.log('ERR',e.message));
 await p.goto('file://'+require('os').tmpdir()+'/pc.html');await p.waitForTimeout(300);
 const cache={};
 for(const k of ['A','B']){
   const t0=Date.now();

   const r=await p.evaluate(([k,n])=>window.__optimize(k,n),[k,TRIALS]);
   // 꺾이는 칸만 남겨 압축
   const comp=r.routes.map(cells=>cells.filter((c,i)=>{if(i===0||i===cells.length-1)return true;const a=cells[i-1],d=cells[i+1];return !((a[0]===c[0]&&c[0]===d[0])||(a[1]===c[1]&&c[1]===d[1]));}));
   cache[k]={sig:r.sig,routes:comp};
   console.log(k,'score',r.score.toFixed(0),'ms',Date.now()-t0);
 }
 await b.close();
 const out=src.replace(/const ROUTE_CACHE = [\s\S]*?;\n<\/script>/,'const ROUTE_CACHE = '+JSON.stringify(cache)+';\n</script>');
 fs.writeFileSync(FILE,out);
})();
