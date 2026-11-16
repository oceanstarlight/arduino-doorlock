const fs=require('fs');
const html=fs.readFileSync(require('path').join(__dirname,'../../docs/wiring.html'),'utf8');
const src=html.match(/<script id="scene-data">([\s\S]*?)<\/script>/)[1];
const SCENES=new Function(src+';return SCENES;')();
const RAIL_COLS=[2,3,4,5,6,8,9,10,11,12,14,15,16,17,18,20,21,22,23,24,26,27,28,29,30];
const UNO=['SCL','SDA','AREF','GND_T','D13','D12','D11','D10','D9','D8','D7','D6','D5','D4','D3','D2','D1','D0','NC','IOREF','RESET','3V3','5V','GND_B1','GND_B2','VIN','A0','A1','A2','A3','A4','A5'];
function key(r){
  if(r.uno){if(!UNO.includes(r.uno))throw 'bad uno '+r.uno; return 'U:'+r.uno;}
  if(r.bb){const [c,row]=r.bb; if(c<1||c>30||!'abcdefghij'.includes(row))throw 'bad hole'; return ('abcde'.includes(row)?'T':'B')+'col'+c;}
  if(r.rail){if(!RAIL_COLS.includes(r.rail[1]))throw 'no rail hole '+r.rail; return 'R:'+r.rail[0];}
  if(r.mod) return 'M:'+r.mod[0]+'.'+r.mod[1];
}
function check(name, expected, btnOrient){
  const S=SCENES[name]; const parent={};
  const f=x=>{parent[x]??=x; while(parent[x]!==x){parent[x]=parent[parent[x]]??parent[x]; x=parent[x];} return x;};
  const u=(a,b)=>{parent[f(a)]=f(b);};
  const holesUsed={};
  const sockets={};
  const occupy=(r,who)=>{ if(r.uno||r.mod){const k=JSON.stringify(r); if(sockets[k]) throw `socket ${k} used twice`; sockets[k]=who;}
    if(r.bb){const k=r.bb.join(''); if(holesUsed[k]) throw `hole ${k} used twice (${holesUsed[k]}, ${who})`; holesUsed[k]=who;} };
  const terms=new Set();
  // Uno internal GND
  ['GND_T','GND_B1','GND_B2'].forEach(g=>u('U:'+g,'U:GND_T'));
  S.wires.forEach((w,i)=>{occupy(w.from,'w'+i);occupy(w.to,'w'+i);u(key(w.from),key(w.to));
    [w.from,w.to].forEach(r=>{if(r.uno||r.mod)terms.add(key(r));});});
  S.mods.forEach(m=>{}); 
  S.parts.forEach(p=>{
    if(p.type==='res'){occupy(p.a,p.id);occupy(p.b,p.id);u('P:'+p.id+'.a',key(p.a));u('P:'+p.id+'.b',key(p.b));terms.add('P:'+p.id+'.a');terms.add('P:'+p.id+'.b');}
    if(p.type==='led'){occupy(p.a,p.id);occupy(p.k,p.id);u('P:'+p.id+'.a',key(p.a));u('P:'+p.id+'.k',key(p.k));terms.add('P:'+p.id+'.a');terms.add('P:'+p.id+'.k');}
    if(p.type==='cap'){u('P:'+p.id+'.p',key(p.p));u('P:'+p.id+'.n',key(p.n));terms.add('P:'+p.id+'.p');terms.add('P:'+p.id+'.n');}
    if(p.type==='button'){const c=p.col; const h={tl:{bb:[c,'e']},tr:{bb:[c+2,'e']},bl:{bb:[c,'f']},br:{bb:[c+2,'f']}};
      Object.entries(h).forEach(([n,r])=>{occupy(r,p.id);u('P:SW.'+n,key(r));});
      if(btnOrient===1){u('P:SW.tl','P:SW.bl');u('P:SW.tr','P:SW.br');} else {u('P:SW.tl','P:SW.tr');u('P:SW.bl','P:SW.br');}
      terms.add('P:SW.tl');terms.add('P:SW.br');}
  });
  // nets
  const nets={}; terms.forEach(t=>{(nets[f(t)]??=[]).push(t);});
  const got=Object.values(nets).map(n=>n.sort());
  const errs=[];
  const norm=s=>s.slice().sort().join(' | ');
  const gotSet=new Map(got.map(n=>[norm(n),n]));
  expected.forEach(e=>{ if(!gotSet.has(norm(e))) {
      const holder=got.filter(n=>e.some(t=>n.includes(t)));
      errs.push('EXPECTED '+norm(e)+'\n   GOT '+holder.map(norm).join('\n       '));}});
  got.forEach(n=>{ if(!expected.some(e=>norm(e)===norm(n))) errs.push('UNEXPECTED NET '+norm(n)); });
  console.log(`== ${name}${btnOrient?' (button orient '+btnOrient+')':''}: ${got.length} nets, ${errs.length?'FAIL':'OK'}`);
  [...new Set(errs)].forEach(e=>console.log('  '+e));
  return got;
}
const A=[
 ['U:D10','P:R1.a'],['P:R1.b','P:R2.a','M:rc.SDA'],
 ['U:D13','P:R3.a'],['P:R3.b','P:R4.a','M:rc.SCK'],
 ['U:D11','P:R5.a'],['P:R5.b','P:R6.a','M:rc.MOSI'],
 ['U:D12','M:rc.MISO'],
 ['U:3V3','M:rc.3.3V','M:rc.RST'],
 ['U:GND_T','U:GND_B1','U:GND_B2','P:R2.b','P:R4.b','P:R6.b','M:rc.GND','M:lcd.GND','M:lkA.GND'],
 ['U:5V','M:lcd.VCC'],['U:A4','M:lcd.SDA'],['U:A5','M:lcd.SCL'],
 ['U:D8','M:kp.R1'],['U:D7','M:kp.R2'],['U:D6','M:kp.R3'],['U:D5','M:kp.R4'],['U:D4','M:kp.C1'],['U:D3','M:kp.C2'],['U:D2','M:kp.C3'],['U:A0','M:kp.C4'],
 ['U:A2','M:lkA.B D4'],['U:A1','M:lkA.B D5'],
];
check('A',A);
const Bexp=[
 ['U:D1','P:R1.a'],['P:R1.b','P:R2.a','M:bt.RXD'],
 ['U:D0','M:bt.TXD'],
 ['U:5V','M:bt.VCC','M:sr.VCC'],
 ['U:GND_T','U:GND_B1','P:R2.b','M:bt.GND','M:sr.GND','P:LR.k','P:LY.k','P:LG.k','P:SW.br','M:ps.GND','M:sv.GND','P:C1.n','M:lkB.GND'],
 ['U:D6','M:sr.Trig'],['U:D7','M:sr.Echo'],
 ['U:D12','P:RR.a'],['P:RR.b','P:LR.a'],['U:D11','P:RY.a'],['P:RY.b','P:LY.a'],['U:D10','P:RG.a'],['P:RG.b','P:LG.a'],
 ['U:D2','P:SW.tl'],
 ['M:ps.+5V','M:sv.V+','P:C1.p'],
 ['U:A0','M:sv.SIG'],
 ['U:D4','M:lkB.A A2'],['U:D5','M:lkB.A A1'],
];
check('B',Bexp,1); check('B',Bexp,2);
