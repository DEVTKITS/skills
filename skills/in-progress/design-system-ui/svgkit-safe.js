/* svgkit-safe.js — SVG-Sanitizer (Allowlist), Teil von svgspec/1. Browser: window.SvgKitSafe, Node: require('./svgkit-safe.js').
   Zwei Wege mit derselben Allowlist: DOMParser (Browser) und eine eigenständige Zeichenketten-Variante (Node/Bun).
   Die Ausgabe wird ausschließlich aus erlaubten Tokens neu geschrieben; Text und Attributwerte werden neu escaped. */
(g=>{
'use strict';
const NS='http://www.w3.org/2000/svg',MAX=1e6,MAXN=5000,MAXD=64;
const set=s=>new Set(s.split(' '));
const EL=set('svg g defs title desc path rect circle ellipse line polyline polygon text tspan textPath linearGradient radialGradient stop pattern clipPath mask marker symbol use');
const TXT=set('text tspan textPath title desc');
const AT=set('id class style transform opacity role lang xml:space aria-label aria-hidden aria-labelledby aria-describedby tabindex focusable '+
'x y x1 y1 x2 y2 cx cy r rx ry fx fy dx dy width height d points viewBox preserveAspectRatio version xmlns '+
'fill fill-opacity fill-rule clip-rule stroke stroke-width stroke-opacity stroke-linecap stroke-linejoin stroke-miterlimit stroke-dasharray stroke-dashoffset '+
'vector-effect paint-order font-family font-size font-weight font-style text-anchor dominant-baseline letter-spacing text-decoration '+
'offset stop-color stop-opacity gradientUnits gradientTransform spreadMethod patternUnits patternContentUnits patternTransform '+
'clipPathUnits maskUnits maskContentUnits clip-path mask marker-start marker-mid marker-end markerWidth markerHeight markerUnits refX refY orient '+
'startOffset href xlink:href');
const XE={'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&apos;'};
const esc=s=>s.replace(/[&<>"']/g,c=>XE[c]);
const ENT={amp:'&',lt:'<',gt:'>',quot:'"',apos:"'"};
const BADC=/[\0-\x08\x0b\x0c\x0e-\x1f\x7f\ufffe\uffff\ud800-\udfff]/;
/* Entities auflösen; unbekannte benannte Entities (z. B. aus einer DOCTYPE) werden zu '' und gemeldet */
const dec=(s,rm)=>s.replace(/&(#x[0-9a-fA-F]+|#[0-9]+|[A-Za-z][A-Za-z0-9]*);/g,(m,e)=>{
let c;
if(e[0]=='#'){c=e[1]=='x'?parseInt(e.slice(2),16):+e.slice(1);return c>0&&c<0x110000?String.fromCodePoint(c):(rm('entity:'+e),'');}
if(ENT[e])return ENT[e];
rm('entity:'+e);return '';
});
/* Wertprüfung je Attribut; liefert null, wenn der Wert verworfen werden muss */
const okVal=(n,v,rm)=>{
if(BADC.test(v)){rm('attr:'+n+' (Steuerzeichen)');return null;}
if(n=='href'||n=='xlink:href'){if(/^#[\w.:-]+$/.test(v.trim()))return v.trim();rm('attr:'+n+' (nicht lokal)');return null;}
let k=v.replace(/[\s\0-\x1f\x7f-\x9f\u00ad\u200b-\u200f\u2028\u2029\ufeff]/g,'').toLowerCase();
if(n!='aria-label'&&/(?:java|vb|live)script:|data:|expression\(|@import|behavior:|-moz-binding|&#|\\/.test(k)){rm('attr:'+n+' (Schema)');return null;}
if(n=='style'&&/<|>/.test(k)){rm('attr:style');return null;}
for(let m,re=/url\(([^)]*)\)/gi;m=re.exec(v);)if(!/^\s*(['"]?)#[\w.:-]+\1\s*$/.test(m[1])){rm('attr:'+n+' (url)');return null;}
return v;
};
const run=(svg,mode)=>{
const removed=[],out=[],rm=m=>{if(!removed.includes(m))removed.push(m);};
let n=0,depth=0,rootDone=false,stack=[],skip=0;
/* gemeinsamer Kern: open() liefert false, wenn das Element samt Inhalt verworfen wird */
const open=(name,attrs,self)=>{
if(!rootDone&&name!='svg'){rm('element:'+name);return false;}
if(!EL.has(name)){rm('element:'+name);return false;}
if(++n>MAXN||stack.length>=MAXD){rm('limit');return false;}
let s='<'+name,seen={};
if(name=='svg'&&!rootDone)s+=' xmlns="'+NS+'"';
for(let[k,v]of attrs){
if(k=='xmlns'){if(v!=NS)rm('attr:xmlns');continue;}
if(k=='xmlns:xlink')continue;
if(k.slice(0,6)=='xmlns:'||k.slice(0,2).toLowerCase()=='on'){rm('attr:'+k);continue;}
if(!AT.has(k)){rm('attr:'+k);continue;}
v=okVal(k,v,rm);if(v==null)continue;
let o=k=='xlink:href'?'href':k;if(seen[o])continue;seen[o]=1;
s+=' '+o+'="'+esc(v)+'"';
}
if(name=='use'&&!seen.href){rm('element:use (ohne lokalen Verweis)');return false;}
rootDone=true;
out.push(s+(self?'/>':'>'));
if(!self)stack.push(name);
return true;
};
const close=()=>{if(stack.length)out.push('</'+stack.pop()+'>');};
const text=t=>{if(stack.length&&TXT.has(stack[stack.length-1])&&t){if(BADC.test(t))t=t.replace(new RegExp(BADC.source,'g'),'');out.push(esc(t));}};
if(typeof svg!='string')svg=String(svg==null?'':svg);
if(svg.length>MAX)return{svg:'',removed:['zu groß (>1 MB)']};
if(mode!='str'&&typeof DOMParser!='undefined'){
let doc=new DOMParser().parseFromString(svg,'image/svg+xml'),root=doc.documentElement;
if(!root||doc.getElementsByTagName('parsererror').length||root.namespaceURI!=NS){return run(svg,'str');}
const walk=el=>{
let nm=el.localName,at=[];
if(el.namespaceURI!=NS){rm('element:'+el.nodeName);return;}
for(let a of el.attributes)at.push([a.name,a.value]);
let sf=!el.firstChild;
if(!open(nm,at,sf)||sf)return;
for(let c=el.firstChild;c;c=c.nextSibling){
if(c.nodeType==1)walk(c);
else if(c.nodeType==3||c.nodeType==4)text(c.nodeValue);
else if(c.nodeType==10)rm('doctype');
}
close();
};
for(let c=doc.firstChild;c;c=c.nextSibling){if(c.nodeType==10)rm('doctype');else if(c.nodeType==1&&c===root)walk(root);else if(c.nodeType==7)rm('processing-instruction');}
return fin(out,removed,rootDone);
}
/* Zeichenketten-Variante: Tokenizer für <…>-Marken, Kommentare, CDATA, PI, DOCTYPE */
let i=0,L=svg.length,m,TAG=/<(\/?)([^\s\/>"'=<]+)((?:"[^"]*"|'[^']*'|[^>"'])*)>/y,ATT=/([^\s=\/"'<>]+)(?:\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s"'>]+)))?/g;
while(i<L){
let lt=svg.indexOf('<',i);
if(lt<0){if(!skip)text(dec(svg.slice(i),rm));break;}
if(lt>i&&!skip)text(dec(svg.slice(i,lt),rm));
i=lt;
if(svg.startsWith('<!--',i)){let e=svg.indexOf('-->',i+4);rm('comment');i=e<0?L:e+3;continue;}
if(svg.startsWith('<![CDATA[',i)){let e=svg.indexOf(']]>',i);if(!skip)text(svg.slice(i+9,e<0?L:e));i=e<0?L:e+3;continue;}
if(svg.startsWith('<?',i)){let e=svg.indexOf('?>',i);rm('processing-instruction');i=e<0?L:e+2;continue;}
if(svg.startsWith('<!',i)){
rm('doctype');let d=0,e=i;
for(;e<L;e++){let c=svg[e];if(c=='[')d++;else if(c==']')d--;else if(c=='>'&&d<=0)break;}
i=e+1;continue;
}
TAG.lastIndex=i;m=TAG.exec(svg);
if(!m){rm('markup (defekt)');i++;continue;}
i=TAG.lastIndex;
let[,cl,nm,rest]=m,self=!cl&&/\/\s*$/.test(rest);
if(cl){
if(skip){skip--;continue;}
let k=stack.lastIndexOf(nm);
if(k>=0)while(stack.length>k)close();
continue;
}
if(skip){if(!self)skip++;continue;}
let at=[],a;
ATT.lastIndex=0;
for(rest=self?rest.replace(/\/\s*$/,''):rest;a=ATT.exec(rest);){
let v=a[2]!=null?a[2]:a[3]!=null?a[3]:a[4]!=null?a[4]:'';
at.push([a[1],dec(v,rm)]);
}
if(!open(nm,at,self)){
if(!self){
if(/^(script|style|foreignObject)$/.test(nm)){let r=new RegExp('</'+nm+'\\s*>','i'),t=r.exec(svg.slice(i));i=t?i+t.index+t[0].length:L;}
else skip=1;
}
}
}
return fin(out,removed,rootDone);
};
const fin=(out,removed,ok)=>{
if(!ok)return{svg:'',removed:removed.concat('kein svg-Element')};
return{svg:out.join(''),removed};
};
const sanitize=(svg,mode)=>{
let r=run(svg,mode);
/* Selbstprüfung: das Ergebnis darf keine Gefahrenmuster enthalten (Fail-closed) */
if(/<(?:script|foreignObject|iframe|object|embed|style|image|a|animate|set)[\s>\/]|\son[a-z]+\s*=|javascript:/i.test(r.svg.replace(/>[^<]*</g,'><').replace(/aria-label="[^"]*"/g,'')))return{svg:'',removed:r.removed.concat('selbstprüfung')};
return r;
};
const S={sanitize,allow:{elements:[...EL],attributes:[...AT]}};
if(typeof module=='object'&&module.exports)module.exports=S;else g.SvgKitSafe=S;
})(this);
