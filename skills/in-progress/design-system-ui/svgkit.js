/* svgkit.js — svgspec/1 (DkZ), Regeln: svg/SPEC.md. CLI: node svgkit.js <spec.json> */
(g=>{'use strict';const ty=(v,t)=>typeof v==t,M=Math,F=M.floor,mn=M.min,mx=M.max,ab=M.abs,P=16,NA='— ausstehend —';const fmt=n=>{
if(!isFinite(n))return '0';let k=F(ab(n)*100+.5),f=k%100;return k?(n<0?'-':'')+F(k/100)+(f?'.'+(f<10?'0'+f:f%10?f:f/10):''):'0';};
const short=v=>ab(v)>=1e6?fmt(v/1e6)+'M':ab(v)>=1e4?fmt(v/1e3)+'k':fmt(v);const XE={'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&apos;'};
const esc=s=>String(s).replace(/[&<>"']/g,c=>XE[c]);const cps=s=>Array.from(s),len=s=>cps(s).length;
const clean=(s,n)=>cps(s.replace(/[\0-\x1f\x7f\ufffe\uffff\p{Cs}]/gu,'')).slice(0,n).join('');
const trunc=(s,n)=>len(s)<=n?s:n<1?'':cps(s).slice(0,n-1).join('')+'…';const fit=(px,fs)=>F(px/(fs*.6));const E=(t,a,c)=>{let s='<'+t;
for(let k in a){let v=a[k];if(v!=null&&v!==false)s+=' '+k+'="'+(ty(v,'number')?fmt(v):esc(v))+'"';}
return s+(c==null?'/>':'>'+c+'</'+t+'>');};const D=(...a)=>a.map((v,i)=>ty(v,'number')?(ty(a[i-1],'number')?' ':'')+fmt(v):v).join('');
const TOK='bg card elevated text muted accent green yellow red blue pink amber cyan lila'.split(' ');
const CSSV={card:'bg-card',elevated:'bg-elevated',muted:'text-muted'};
const BASE='060608 0e0e10 141418 f6f6f7 8e8e8f fa1e4e 00ff88 ffb800 ff3b5c 55acee ec4899 f5a623 3fe0d0 9d7bff'.split(' ');
const FONT='Inter,system-ui,-apple-system,sans-serif',SER='accent cyan lila amber green blue pink yellow';
const th=(hex,ser,radius,font,line,lineop)=>({hex:Object.fromEntries(TOK.map((t,i)=>[t,'#'+hex[i]])),series:ser.split(' '),radius,font,line,lineop});
const THEMES={master:th(BASE,SER,12,FONT,'text',.08),
light:th('f6f6f7 ffffff f0f0f2 0e0e10 6a6a6b'.split(' ').concat(BASE.slice(5)),'accent blue lila pink amber cyan green yellow',12,FONT,'text',.08),
contrast:th(TOK.map((t,i)=>i<3?'000000':'00ff88'),'accent',12,FONT,'text',1),control:th(BASE,SER,0,'Archivo Expanded,'+FONT,'accent',.3),
content:th(BASE,'accent lila cyan pink amber green blue yellow',20,'Syne,'+FONT,'text',.08)
};let T,V,K; // aktives Theme, vars-Modus, Kontrast
const hexOf=t=>K?(~TOK.indexOf(t)&&TOK.indexOf(t)<3?'#000000':'#00ff88'):t[0]=='#'?t:T.hex[t];
const C=t=>V&&t[0]!='#'?'var(--'+(CSSV[t]||t)+', '+hexOf(t)+')':hexOf(t);
const ink=t=>{if(K)return '#000000';let n=parseInt(hexOf(t).slice(1),16);return(n>>16)*299+(n>>8&255)*587+(n&255)*114>=128000?'#060608':'#ffffff';};
const op=x=>K?null:x,SC=i=>T.series[i%T.series.length],MID='middle',CM=()=>C('muted'),CT=()=>C('text'),U=id=>'url(#'+id+')',isA=Array.isArray,SW='stroke-width',DA='stroke-dasharray',DF=c=>E('defs',{},c),RJ={'stroke-linejoin':'round','stroke-linecap':'round'};
const LN=()=>({stroke:C(T.line),'stroke-opacity':op(T.lineop)});
/* Schema: name|typ|min/Auswahl|max|default|label|len */
const WH=(w,h,a)=>`width|int|${a}|2400|${w}|Breite;height|int|${a>99?120:a}|2400|${h}|Höhe;`;
const COL=(n,d,l)=>`${n}|color|||${d}|${l};`,B=(n,d,l)=>`${n}|bool|||${d}|${l};`,HD=B('heading',1,'Titel zeigen'),FA=COL('color','accent','Farbe');
const LAB=n=>`labels|strs|||[]|Beschriftungen|${n};`;const ICONS={
check:'M5 12.5L9.5 17L19 7.5',cross:'M6 6L18 18M18 6L6 18',play:'M8 5V19L19 12Z',pause:'M8 5V19M16 5V19',
'arrow-right':'M4 12H20M14 6L20 12L14 18','arrow-left':'M20 12H4M10 6L4 12L10 18',
star:'M12 3L14.4 9.3L21 9.6L15.8 13.7L17.6 20.2L12 16.5L6.4 20.2L8.2 13.7L3 9.6L9.6 9.3Z',
heart:'M12 20C12 20 4 15.5 4 9.5A4 4 0 0 1 12 7.5A4 4 0 0 1 20 9.5C20 15.5 12 20 12 20Z',
menu:'M4 6H20M4 12H20M4 18H20',plus:'M12 5V19M5 12H19',minus:'M5 12H19',
info:'M3 12A9 9 0 1 0 21 12A9 9 0 1 0 3 12ZM12 11V17M12 7.5V8',search:'M4 10.5A6.5 6.5 0 1 0 17 10.5A6.5 6.5 0 1 0 4 10.5ZM15.5 15.5L20 20',
download:'M12 4V15M7 10L12 15L17 10M5 20H19'
};const TY={
bar:['Balkendiagramm',WH(480,280,160)+'data|nums|-1e9|1e9|[12,19,7,15,22,10]|Werte|64;'+LAB(64)+FA+B('values',1,'Werte zeigen')+B('grid',1,'Raster')+HD],
line:['Liniendiagramm',WH(480,280,160)+'data|series|-1e9|1e9|[[4,9,6,12,10,15,13]]|Reihen|200;'+LAB(200)+FA+B('area',1,'Fläche')+B('dots',1,'Punkte')+B('grid',1,'Raster')+HD],
donut:['Ringdiagramm',WH(360,240,160)+'data|nums|0|1e9|[45,30,25]|Werte|12;'+LAB(12)+'thickness|num|0.1|0.9|0.34|Ringstärke;center|str||||Mitte|16;'+B('legend',1,'Legende')+HD],
pattern:['Muster',WH(240,240,16)+'kind|enum|grid/dots/stripes/waves/confetti||grid|Art;size|int|4|200|24|Zellgröße;stroke|num|0.5|12|1.5|Linienstärke;angle|int|-90|90|45|Winkel;density|int|1|400|60|Dichte;opacity|num|0.05|1|0.6|Deckkraft;'+FA+B('bg',1,'Karte')],
badge:['Badge','height|int|16|64|24|Höhe;label|str|||DkZ|Beschriftung|32;value|str|||aktiv|Wert|32;'+FA],
icon:['Symbol','name|enum|'+Object.keys(ICONS).join('/')+'||check|Symbol;size|int|12|512|48|Größe;'+FA+'stroke|num|1|4|2|Linienstärke;'+B('bg',0,'Karte')],
monogram:['Monogramm','text|str|||DZ|Zeichen|3;size|int|24|512|96|Größe;shape|enum|circle/square/hex||circle|Form;'+FA+COL('color2','lila','Zweitfarbe')],
gradient:['Verlauf',WH(480,240,16)+'kind|enum|linear/radial||linear|Art;'+COL('from','accent','Von')+COL('to','lila','Bis')+'via|colorx||||Über;angle|int|0|360|45|Winkel;cx|num|0|100|50|Mitte x;cy|num|0|100|50|Mitte y;text|str||||Text|60;'],
graph:['Workflow',WH(0,0,0)+'nodes|nodes|||[{"id":"A","type":"input"},{"id":"B","type":"model"},{"id":"C","type":"output"}]|Knoten|60;edges|edges|||[{"from":"A","to":"B"},{"from":"B","to":"C"}]|Kanten|120;'+HD]
};
const COMMON='theme|enum|'+Object.keys(THEMES).join('/')+'||master|Theme;colors|enum|vars/static||vars|Farbmodus;idprefix|str|||sk|ID-Präfix|32;seed|int|0|4294967295|1|Seed;';
const SCHEMA={},UNIT={width:'px',height:'px',size:'px',stroke:'px',angle:'°',cx:'%',cy:'%'};
for(let t in TY)SCHEMA[t]={label:TY[t][0],felder:(COMMON+'title|str|||'+TY[t][0]+'|Titel|120;'+TY[t][1]).split(';').filter(Boolean).map(d=>{
let[name,type,a,b,v,label,l]=d.split('|'),f={name,type,label:label||name};if(type=='enum')f.choices=a.split('/');
if(a&&type!='enum'){f.min=+a;f.max=+b;}
if(type=='int'||type=='num'){f.step=type=='int'?1:.01;if(UNIT[name])f.unit=UNIT[name];}
f.default=f.step?+v:type=='bool'?v=='1':/^[[{]/.test(v)?JSON.parse(v):v;if(l)f.len=+l;return f;})};const bad=(field,error)=>{throw{field,error};};
const isNum=v=>ty(v,'number')&&isFinite(v);const val=(f,v)=>{
let k=f.type,no=m=>bad(f.name,m||'ungültiger Wert'),num=e=>isNum(e)?mn(mx(e,f.min),f.max):no('Zahl erwartet');
let str=(e,n)=>e==null?'':ty(e,'string')?clean(e,n):no('Text erwartet');if(v==null)return structuredClone(f.default);
if(k=='int'||k=='num')return k=='int'?F(num(v)):num(v);if(k=='bool')return ty(v,'boolean')?v:no('true/false erwartet');
if(k=='enum')return f.choices.includes(v)?v:no('erlaubt: '+f.choices.join(', '));if(k=='str')return str(v,f.len);if(k[0]=='c'){
if(!ty(v,'string')||!(TOK.includes(v)||/^#([0-9a-fA-F]{3}){1,2}$/.test(v)||k=='colorx'&&!v))no('Token oder #rgb/#rrggbb');
return v.toLowerCase().replace(/^#(.)(.)(.)$/,'#$1$1$2$2$3$3');}
if(!isA(v))no('Liste erwartet');v=v.slice(0,f.len);if(k=='nums')return v.map(num);
if(k=='series')return isA(v[0])?v.slice(0,4).map(r=>isA(r)?r.slice(0,200).map(num):no()):v.length?[v.map(num)]:[];
if(k=='strs')return v.map(e=>ty(e,'string')?clean(e,40):no('Text erwartet'));let o=[];for(let e of v){if(!e||!ty(e,'object')||isA(e))no();
if(k=='edges'){o.push({from:str(e.from,32),to:str(e.to,32)});continue;}
let r={id:str(e.id,32),type:str(e.type,24),label:str(e.label,40)};if(!r.id||o.some(q=>q.id==r.id))no('id fehlt oder doppelt');
if(e.x!=null||e.y!=null){if(!isNum(e.x)||!isNum(e.y))no('x/y: Zahlen');r.x=mn(mx(e.x,0),4000);r.y=mn(mx(e.y,0),4000);}
o.push(r);}
return o;};const norm=x=>{if(!x||!ty(x,'object')||isA(x))bad('spec','Objekt erwartet');
if(!ty(x.type,'string')||!TY.hasOwnProperty(x.type))bad('type','unbekannter Typ');let o={type:x.type};
for(let f of SCHEMA[x.type].felder)o[f.name]=val(f,x[f.name]);if(o.nodes&&x.nodes!=null&&x.edges==null)o.edges=[];
if(!/^[A-Za-z][A-Za-z0-9_-]*$/.test(o.idprefix))bad('idprefix','[A-Za-z][A-Za-z0-9_-]*');if(!o.title)o.title=TY[x.type][0];
let has=id=>o.nodes.some(n=>n.id==id);if(o.edges)for(let e of o.edges)if(!has(e.from)||!has(e.to))bad('edges','unbekannter Knoten');return o;};
const RC=(x,y,width,height,rx,a)=>E('rect',{x,y,width,height,rx,...a}),CI=(cx,cy,r,a)=>E('circle',{cx,cy,r,...a}),PA=(d,a)=>E('path',{d,...a});
const fr=(w,h,rx,a)=>RC(.5,.5,w-1,h-1,rx,a);const card=(w,h)=>[fr(w,h,T.radius,{fill:C('card'),...LN()})].concat(T.radius?[]:
PA(D('M',1,11,'V',1,'H',11,'M',w-11,1,'H',w-1,'V',11,'M',w-1,h-11,'V',h-1,'H',w-11,'M',11,h-1,'H',1,'V',h-11),{fill:'none',stroke:C('accent'),[SW]:2}));
const txt=(x,y,fs,fill,s,a,w,ex)=>E('text',{x,y,'font-size':fs,'font-weight':w,'text-anchor':a,fill,...ex},esc(s));
const base=(s,w,h)=>card(w,h).concat(head(s,w)),head=(s,w)=>s.heading?[txt(P,29,13,CT(),trunc(s.title,fit(w-32,13)),null,600)]:[];
const none=(x,y,f)=>txt(x,y,f||13,CM(),NA,MID);const ticks=(lo,hi)=>{let raw=(hi-lo)/4,mag=1,r,st;while(mag*10<=raw)mag*=10;while(mag>raw)mag/=10;
r=raw/mag;st=mag*(r<=1?1:r<=2?2:r<=5?5:10);lo=F(lo/st)*st;hi=M.ceil(hi/st)*st;return[lo,hi,st,F((hi-lo)/st+.5)];};
const maxlen=a=>a.reduce((m,s)=>mx(m,len(s)),0);const axes=(s,w,h,lo,hi,o)=>{if(hi-lo<.01){lo-=1;hi+=1;}
let t=ticks(lo,hi),tl=[],i,y0=P+(s.heading?26:0)+8,y1=h-P-(s.labels.length?20:0),x1=w-P;lo=t[0];hi=t[1];
for(i=0;i<=t[3];i++)tl.push(short(lo+i*t[2]));let x0=P+(s.grid?maxlen(tl)*6.6+8:0),Y=v=>y0+(hi-v)*(y1-y0)/(hi-lo);
if(s.grid)for(i=0;i<=t[3];i++){let y=Y(lo+i*t[2]);o.push(E('line',{x1:x0,y1:y,x2:x1,y2:y,...LN(),[DA]:K?'2 4':null}),txt(x0-8,y+4,11,CM(),tl[i],'end'));}
return[x0,x1,y0,y1,Y];};
/* Typen: Elemente oder [Breite, Höhe, viewBox|0, Elemente] */
const R={};R.bar=(s,w,h)=>{let d=s.data,n=d.length,o=base(s,w,h);if(!n)return o.concat(none(w/2,h/2+4));
let[x0,x1,,y1,Y]=axes(s,w,h,mn(0,...d),mx(0,...d),o),sl=(x1-x0)/n,bw=mn(sl*.64,64),c=C(s.color);d.forEach((v,i)=>{
let cx=x0+i*sl+sl/2,yv=Y(v),yz=Y(0),lb=short(v);o.push(RC(cx-bw/2,mn(yv,yz),bw,ab(yv-yz),mn(T.radius/3,bw/2),{fill:c}));
if(s.values&&len(lb)*6.6<=sl)o.push(txt(cx,v<0?mn(yv+14,y1-4):yv-6,11,CT(),lb,MID));
if(i<s.labels.length)o.push(txt(cx,h-P-4,11,CM(),trunc(s.labels[i],fit(sl-4,11)),MID));});return o;};R.line=(s,w,h)=>{
let S=s.data,o=base(s,w,h),all=[].concat(...S),m=mx(0,...S.map(r=>r.length));if(!m)return o.concat(none(w/2,h/2+4));
let[x0,x1,,y1,Y]=axes(s,w,h,mn(...all),mx(...all),o),X=j=>m==1?(x0+x1)/2:x0+j*(x1-x0)/(m-1);
let ls=s.labels.slice(0,m).map(l=>trunc(l,12)),lw=maxlen(ls)*6.6+8,ev=1;while(m>1&&ev*(x1-x0)/(m-1)<lw)ev++;
ls.forEach((l,j)=>{if(j%ev==0)o.push(txt(X(j),h-P-4,11,CM(),l,m>1&&j==0?'start':m>1&&j==m-1?'end':MID));});S.forEach((r,i)=>{if(!r.length)return;
let c=C(i?SC(i):s.color),p=r.map((v,j)=>D(j?'L':'M',X(j),Y(v))).join('');
if(s.area&&S.length==1&&!K)o.push(PA(p+D('L',X(r.length-1),y1,'L',X(0),y1,'Z'),{fill:c,'fill-opacity':.16}));
o.push(PA(p,{fill:'none',stroke:c,[SW]:2,...RJ,[DA]:K&&i%2?'6 4':null}));
if(s.dots&&m<=40)r.forEach((v,j)=>o.push(CI(X(j),Y(v),3,{fill:C('card'),stroke:c,[SW]:2})));});return o;};R.donut=(s,w,h)=>{
let d=s.data,n=d.length,o=base(s,w,h),tot=d.reduce((q,v)=>q+v,0);
let top=P+(s.heading?26:0),ah=h-top-P,aw=s.legend?(w-2*P)*.5:w-2*P,r=mn(ah,aw)/2,cx=s.legend?P+aw/2:w/2,cy=top+ah/2,ri=r*(1-s.thickness);
let ring=q=>D('M',cx,cy-q,'A',q,q,0,1,1,cx,cy+q,'A',q,q,0,1,1,cx,cy-q,'Z');
let pt=(q,a)=>D(cx+q*M.cos(a),cy+q*M.sin(a)),col=i=>K?(i%2?'bg':'accent'):SC(i);
if(!(tot>0))return o.concat(PA(ring(r)+ring(ri),{fill:CM(),'fill-opacity':op(.25),'fill-rule':'evenodd'}),none(cx,cy+4,mn(13,ri/4.5)));
let cum=0,fs=mn(mx(F(ri*.45),10),36),k=s.legend?mn(n,F(ah/20)):0,lx=P+aw+16,ly=cy-k*10;d.forEach((v,i)=>{if(v<=0)return;
let a0=-M.PI/2+2*M.PI*cum/tot,a1=-M.PI/2+2*M.PI*(cum+v)/tot,L=a1-a0>M.PI?1:0,full=v>=tot;cum+=v;
o.push(PA(full?ring(r)+ring(ri):'M'+pt(r,a0)+D('A',r,r,0,L,1)+' '+pt(r,a1)+'L'+pt(ri,a1)+D('A',ri,ri,0,L,0)+' '+pt(ri,a0)+'Z',{fill:C(col(i)),'fill-rule':full?'evenodd':null,stroke:C(K?'accent':'card'),[SW]:K?1:2}));
});o.push(txt(cx,cy+fs*.35,fs,CT(),trunc(s.center||short(tot),fit(ri*2-8,fs)),MID,700));for(let i=0;i<k;i++){let y=ly+i*20;
o.push(RC(lx,y+5,10,10,2,{fill:C(col(i)),stroke:K?C('accent'):null}),
txt(lx+16,y+14,12,CT(),trunc(i<s.labels.length?s.labels[i]:'Teil '+(i+1),fit(w-P-lx-68,12))),
txt(w-P,y+14,12,CM(),fmt(F(d[i]*1000/tot+.5)/10)+'%','end'));}
return o;};R.pattern=(s,w,h)=>{let z=s.size,c=C(s.color),id=s.idprefix,o=s.bg?card(w,h):[],sd=s.seed;
let rnd=()=>(sd=(sd*1664525+1013904223)%4294967296)/4294967296,g=[];if(s.kind=='confetti'){for(let i=0;i<s.density;i++){
let x=rnd()*w,y=rnd()*h,k=F(rnd()*2),f=C(T.series[F(rnd()*T.series.length)]),q=z*.3+rnd()*z*.4,tr='rotate('+D(F(rnd()*360),x,y)+')';
g.push(k?RC(x-q/2,y-q*.2,q,q*.4,1,{fill:f,transform:tr}):CI(x,y,q/2,{fill:f}));}
return o.concat(DF(E('clipPath',{id:id+'-clip'},RC(null,null,w,h,T.radius))),E('g',{'clip-path':U(id+'-clip'),opacity:op(s.opacity)},g.join('')));}
let tw=s.kind=='waves'?z*2:z,m=z/2,ln={fill:'none',stroke:c,[SW]:s.stroke};
let b={grid:PA(D('M',z,0,'L',0,0,'L',0,z),ln),dots:CI(m,m,s.stroke,{fill:c}),stripes:RC(null,null,s.stroke,z,null,{fill:c}),
waves:PA(D('M',0,m,'Q',m,0,z,m,'T',tw,m),ln)}[s.kind];
return o.concat(DF(E('pattern',{id:id+'-pat',width:tw,height:z,patternUnits:'userSpaceOnUse',patternTransform:s.kind=='stripes'?'rotate('+fmt(s.angle)+')':null},b)),
RC(null,null,w,h,T.radius,{fill:U(id+'-pat'),opacity:op(s.opacity)}));};R.badge=s=>{
let h=s.height,fs=h*.46,cw=fs*.6,nl=len(s.label),lw=nl?nl*cw+h*.8:0,vw=len(s.value)*cw+h*.8,w=lw+vw,r=T.radius==20?h/2:mn(T.radius,h)/2,c=C(s.color),y=h/2+fs*.35;
let o=[fr(w,h,r,{fill:C('elevated'),...LN()}),RC(lw,null,vw,h,r,{fill:c})];if(lw&&r)o.push(RC(lw,null,mn(r,vw),h,null,{fill:c}));
if(nl)o.push(txt(lw/2,y,fs,CT(),s.label,MID,500));o.push(txt(lw+vw/2,y,fs,ink(s.color),s.value,MID,700));return[w,h,0,o];};
R.icon=s=>[s.size,s.size,s.bg?'-4 -4 32 32':'0 0 24 24',(s.bg?[RC(-4,-4,32,32,T.radius/2,{fill:C('card'),...LN(),[SW]:.5})]:[])
.concat(PA(ICONS[s.name],{fill:'none',stroke:C(s.color),[SW]:s.stroke,...RJ}))];R.monogram=s=>{
let z=s.size,c=z/2,q=c-1,k=.8660254037844386,n=len(s.text),fs=z*(n<2?.5:n<3?.42:.34),id=s.idprefix+'-grad',fill=K?C('accent'):U(id),o=[];
if(!K)o.push(DF(E('linearGradient',{id,x1:0,y1:0,x2:1,y2:1},E('stop',{offset:0,'stop-color':C(s.color)})+E('stop',{offset:1,'stop-color':C(s.color2)}))));
o.push(s.shape=='circle'?CI(c,c,q,{fill}):s.shape=='square'?RC(1,1,z-2,z-2,T.radius?z*.22:0,{fill})
:PA(D('M',c,1,'L',c+q*k,c-q/2,'L',c+q*k,c+q/2,'L',c,z-1,'L',c-q*k,c+q/2,'L',c-q*k,c-q/2,'Z'),{fill}));
if(n)o.push(txt(c,c+fs*.35,fs,ink(s.color),s.text,MID,700));return[z,z,0,o];};R.gradient=(s,w,h)=>{
let id=s.idprefix+'-grad',a=s.angle*M.PI/180,cs=M.cos(a)*50,sn=M.sin(a)*50,st='',tx=s.text;
let stop=(offset,t)=>st+=E('stop',{offset,'stop-color':C(t)}),pc=v=>fmt(v)+'%';
if(K){stop(0,'accent');stop(.5,'accent');stop(.5,'bg');stop(1,'bg');}else{stop(0,s.from);if(s.via)stop(.5,s.via);stop(1,s.to);}
let o=[DF(s.kind=='linear'?E('linearGradient',{id,x1:pc(50-cs),y1:pc(50-sn),x2:pc(50+cs),y2:pc(50+sn)},st):E('radialGradient',{id,cx:pc(s.cx),cy:pc(s.cy),r:'75%'},st)),
fr(w,h,T.radius,{fill:U(id),stroke:K?C('accent'):null})];tx=trunc(tx,fit(w-48,12));if(tx){let fs=mn(mx(h*.16,12),72,(w-48)/(len(tx)*.6));
o.push(txt(w/2,h/2+fs*.35,fs,K?C('accent'):ink(s.from),tx,MID,700,K&&{stroke:C('bg'),[SW]:3,'paint-order':'stroke'}));}
return o;};R.graph=s=>{let N=s.nodes,nn=N.length,nw=136,nh=48,gx=56,gy=20,top=P+(s.heading?28:0),pos=[],cw,ch,o=[],id=s.idprefix+'-arrow';
let ix=k=>N.findIndex(n=>n.id==k);if(!nn){cw=320;ch=top+80;}
else if(N.every(n=>n.x!=null)){pos=N.map(n=>[P+n.x,top+n.y]);cw=P+mx(...N.map(n=>n.x))+nw+P;ch=top+mx(...N.map(n=>n.y))+nh+P;}else{
let rk=N.map(()=>0),go=1,it=0,idx=[];
while(go&&it++<nn){go=0;for(let e of s.edges){let a=ix(e.from),b=ix(e.to);if(a!=b&&rk[b]<rk[a]+1&&rk[a]+1<nn){rk[b]=rk[a]+1;go=1;}}}
let cnt=rk.slice(0,mx(...rk)+1).fill(0);rk.forEach((r,i)=>idx[i]=cnt[r]++);let mc=mx(...cnt),hh=mc*nh+(mc-1)*gy;
pos=rk.map((r,i)=>[P+r*(nw+gx),top+(hh-(cnt[r]*nh+(cnt[r]-1)*gy))/2+idx[i]*(nh+gy)]);cw=2*P+cnt.length*nw+(cnt.length-1)*gx;ch=top+hh+P;}
if(s.edges.some(e=>e.from!=e.to&&pos[ix(e.to)][0]<=pos[ix(e.from)][0]+nw))ch+=28;o=card(cw,ch).concat(head(s,cw));if(!nn)o.push(none(160,top+36));
else{
o.push(DF(E('marker',{id,viewBox:'0 0 10 10',refX:9,refY:5,markerWidth:7,markerHeight:7,orient:'auto-start-reverse'},PA('M0 0L10 5L0 10Z',{fill:CM()}))));
for(let e of s.edges){let[ax,ay]=pos[ix(e.from)],[bx,by]=pos[ix(e.to)],bk=bx<=ax+nw,sx=bk?ax+nw/2:ax+nw,sy=ay+(bk?nh:nh/2),tx=bk?bx+nw/2:bx,ty=by+(bk?nh:nh/2),dx=bk?0:mx(24,(tx-sx)/2),dy=bk?40:0;
if(e.from!=e.to)o.push(PA(D('M',sx,sy,'C',sx+dx,sy+dy,tx-dx,ty+dy,tx,ty),{fill:'none',stroke:CM(),[SW]:1.5,[DA]:bk?'4 4':null,'marker-end':U(id)}));}
N.forEach((n,i)=>{let[x,y]=pos[i],c=C(SC(cps(n.type).reduce((q,ch)=>q+ch.codePointAt(0),0)));
o.push(RC(x,y,nw,nh,mn(T.radius,10),{fill:C('elevated'),stroke:c,'stroke-opacity':op(.45)}),RC(x+6,y+10,3,nh-20,1.5,{fill:c}),
txt(x+16,y+21,12,CT(),trunc(n.label||n.id,fit(nw-22,12)),null,600));if(n.type)o.push(txt(x+16,y+36,10,CM(),trunc(n.type,fit(nw-22,10))));});}
return[s.width||cw,s.height||ch,D(0,0,cw,ch),o];};const generate=spec=>{let s;
try{s=norm(spec);}catch(e){if(e&&e.field)return{ok:false,error:e.error,field:e.field};throw e;}
T=THEMES[s.theme];V=s.colors=='vars';K=s.theme=='contrast';
let r=R[s.type](s,s.width,s.height),[w,h,vb,body]=ty(r[0],'number')?r:[s.width,s.height,0,r],ti=esc(s.title);
return{ok:true,spec:s,svg:'<svg xmlns="http://www.w3.org/2000/svg" width="'+fmt(w)+'" height="'+fmt(h)+'" viewBox="'+(vb||D(0,0,w,h))+'" role="img" aria-label="'+ti+'" font-family="'+(V?'var(--font-main, '+T.font+')':T.font)+'">\n<title>'+ti+'</title>\n'+body.join('\n')+'\n</svg>'};
};const SK={version:'svgspec/1',generate,schema:SCHEMA,themes:Object.keys(THEMES),tokens:n=>({...(THEMES[n]||THEMES.master).hex}),fmt,
sanitize:svg=>(typeof SvgKitSafe!='undefined'?SvgKitSafe:require('./svgkit-safe.js')).sanitize(svg)};
if(typeof module!='object'||!module.exports)return g.SvgKit=SK;module.exports=SK;if(require.main===module){
let r;try{r=generate(JSON.parse(require('fs').readFileSync(process.argv[2],'utf8')))}catch(e){r={field:'spec',error:e.message}}
if(r.ok)process.stdout.write(r.svg);else{process.stderr.write('svgspec: '+r.field+': '+r.error+'\n');process.exitCode=1}
}
})(this);