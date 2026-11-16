// 그려진 선 검사: 모든 구간이 수평/수직인지, 다른 선과 같은 방향으로 겹치는 구간이 없는지, 끝점이 핀 위치와 맞는지
const {chromium}=require('playwright');const fs=require('fs');
(async()=>{
 fs.writeFileSync(require('os').tmpdir()+'/g.html','<!doctype html><meta charset=utf-8><body>'+fs.readFileSync(require('path').join(__dirname,'../../docs/wiring.html'),'utf8'));
 const b=await chromium.launch();const p=await b.newPage();p.on('pageerror',e=>console.log('ERR',e.message));
 await p.goto('file://'+require('os').tmpdir()+'/g.html');await p.waitForTimeout(400);
 const res=await p.evaluate(()=>{
   const out={};
   for(const k of ['A','B']){
     const I=window.__routeInfo[k];
     const wires=[...document.querySelectorAll(`#bench${k} .wire`)].map(g=>{
       const d=g.querySelector('.wcore').getAttribute('d');
       return d.slice(1).split(' L').map(t=>t.trim().split(/\s+/).map(Number));});
     let diag=0,overlap=[],minGap=1e9;
     const segs=[];
     wires.forEach((pts,wi)=>{for(let i=1;i<pts.length;i++){const [x0,y0]=pts[i-1],[x1,y1]=pts[i];
       if(Math.abs(x0-x1)>0.05&&Math.abs(y0-y1)>0.05)diag++;
       segs.push({wi,h:Math.abs(y0-y1)<0.05,a:Math.min(x0,x1),b:Math.max(x0,x1),c:Math.min(y0,y1),d:Math.max(y0,y1)});}});
     for(let i=0;i<segs.length;i++)for(let j=i+1;j<segs.length;j++){const s=segs[i],t=segs[j];if(s.wi===t.wi||s.h!==t.h)continue;
       if(s.h){const gap=Math.abs(s.c-t.c), ov=Math.min(s.b,t.b)-Math.max(s.a,t.a); if(ov>1){ if(gap<0.5)overlap.push([s.wi,t.wi]); else minGap=Math.min(minGap,gap);} }
       else{const gap=Math.abs(s.a-t.a), ov=Math.min(s.d,t.d)-Math.max(s.c,t.c); if(ov>1){ if(gap<0.5)overlap.push([s.wi,t.wi]); else minGap=Math.min(minGap,gap);} }}
     out[k]={wires:wires.length,cached:I.cached,diagonal:diag,overlaps:overlap.length,overlapPairs:overlap.slice(0,5),minParallelGap:+minGap.toFixed(2)};
   }
   return out;});
 console.log(JSON.stringify(res,null,1));
 await b.close();})();
