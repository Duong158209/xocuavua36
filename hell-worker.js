// hell-worker-rapfi.js - Worker chay Hell Rapfi Hybrid
self.onmessage = (e)=>{
  const {id, b, pen, t} = e.data;
  try{
    const SIZE = Math.sqrt(b.byteLength);
    self.SIZE = SIZE;
    self.idx = (r,c)=>r*SIZE+c;
    self.inb = (r,c)=>r>=0&&r<SIZE&&c>=0&&c<SIZE;
    self.board = new Uint8Array(b);

    // Inline Hell Rapfi v2.5 - copy tu file rieng de worker doc lap
    const HELL_TIME = t || 12000, HELL_SOFT = Math.min(7000, t*0.6 || 7000);
    const N = SIZE*SIZE;
    const DR=[0,1,1,1], DC=[1,0,1,-1];
    const WINV=100000000, INF=1e12, MAXPLY=96, EXTMAX=28;
    const VCF_NODES=3000000; let vcfLim=VCF_NODES, bx0=0,bx1=0,by0=0,by1=0;
    const OPEN4=50000000, FOUR=5000000, OPEN3=1500000, BRK3=400000, DBL3=8000000;
    const W=[0,24,192,2000,25000,0];
    const DK=14;
    const cw=new Array(N), nbr=new Array(N);
    const wSt=new Int32Array(4*N), wB1=new Int32Array(4*N), wB2=new Int32Array(4*N);
    for(let r=0;r<SIZE;r++)for(let c=0;c<SIZE;c++){
      const i=self.idx(r,c), L=[];
      for(let d=0;d<4;d++)for(let k=0;k<5;k++){
        const sr=r-k*DR[d], sc=c-k*DC[d], er=sr+4*DR[d], ec=sc+4*DC[d];
        if(self.inb(sr,sc)&&self.inb(er,ec)){
          const w=d*N+self.idx(sr,sc);
          L.push(w);
          wSt[w]=DR[d]*SIZE+DC[d];
          const a1=sr-DR[d], a2=sc-DC[d], b1=er+DR[d], b2=ec+DC[d];
          wB1[w]=self.inb(a1,a2)?self.idx(a1,a2):-1;
          wB2[w]=self.inb(b1,b2)?self.idx(b1,b2):-1;
        }
      }
      cw[i]=Int32Array.from(L);
      const nb=[];
      for(let dr=-4;dr<=4;dr++)for(let dc=-4;dc<=4;dc++){ if((dr||dc)&&self.inb(r+dr,c+dc)) nb.push(self.idx(r+dr,c+dc)); }
      nbr[i]=Int16Array.from(nb);
    }
    let hb,w1,w2,near,E,won,nodes,deadline,aborted,vn;
    const fs=[null,new Set(),new Set()], ts=[null,new Set(),new Set()];
    let H1=0,H2=0;
    const TTN=1<<23;
    const TK=new Int32Array(TTN), TD=new Int8Array(TTN).fill(-1), TS=new Int32Array(TTN), TF=new Uint8Array(TTN), TM=new Int16Array(TTN);
    let ttCount=0;
    const hist=new Int32Array(N), killer=new Int32Array(2*MAXPLY).fill(-1), S7=new Uint8Array(7), counter=new Int16Array(N).fill(-1);
    const cv=(x,p)=>x===0?0:(x===p?1:2);
    const Z1=new Int32Array(2*N), Z2=new Int32Array(2*N);
    (()=>{let q=123456789; const rn=()=>{q^=q<<13; q^=q>>>17; q^=q<<5; return q|0;}; for(let i=0;i<2*N;i++){Z1[i]=rn(); Z2[i]=rn();}})();
    function upd(i,p,d){
      const L=cw[i];
      for(let k=0;k<L.length;k++){
        const w=L[k], a=w1[w], b=w2[w];
        E-=(a===0?W[b]:0)-(b===0?W[a]:0);
        if(a===4&&b===0) fs[1].delete(w);
        if(b===4&&a===0) fs[2].delete(w);
        if(a===3&&b===0) ts[1].delete(w);
        if(b===3&&a===0) ts[2].delete(w);
        const a2=p===1?a+d:a, b2=p===2?b+d:b;
        w1[w]=a2; w2[w]=b2;
        E+=(a2===0?W[b2]:0)-(b2===0?W[a2]:0);
        if(a2===4&&b2===0) fs[1].add(w);
        if(b2===4&&a2===0) fs[2].add(w);
        if(a2===3&&b2===0) ts[1].add(w);
        if(b2===3&&a2===0) ts[2].add(w);
        if(d>0&&((p===1&&a2===5)||(p===2&&b2===5))) won=true;
      }
    }
    function put(i,p){ hb[i]=p; won=false; H1^=Z1[i*2+p-1]; H2^=Z2[i*2+p-1]; upd(i,p,1); const nb=nbr[i]; for(let k=0;k<nb.length;k++) near[nb[k]]++; return won; }
    function undo(i){ const p=hb[i]; hb[i]=0; H1^=Z1[i*2+p-1]; H2^=Z2[i*2+p-1]; upd(i,p,-1); const nb=nbr[i]; for(let k=0;k<nb.length;k++) near[nb[k]]--; }
    function emptyOf(w){ const st=wSt[w], s=w%N; for(let k=0;k<5;k++){ const c=s+k*st; if(hb[c]===0) return c; } return -1; }
    function wins(p){ const out=[]; for(const w of fs[p]){ const i=emptyOf(w); if(i>=0&&out.indexOf(i)<0) out.push(i); } return out; }
    function openFourMove(p){
      const T=ts[p]; if(T.size===0) return -1;
      const arr=Array.from(T), seen=new Set();
      for(let a=0;a<arr.length;a++){
        const w=arr[a], st=wSt[w], s0=w%N;
        for(let k=0;k<5;k++){
          const c=s0+k*st;
          if(hb[c]!==0||seen.has(c)) continue;
          seen.add(c); put(c,p); const n=wins(p).length; undo(c); if(n>=2) return c;
        }
      }
      return -1;
    }
    function checkTime(){ if(performance.now()>=deadline) aborted=true; return aborted; }
    function evalSide(p){
      const F=p===1?fs[1]:fs[2], T=p===1?ts[1]:ts[2];
      let s=0, live3=0;
      for(const w of F){
        const b1=wB1[w], b2=wB2[w]; let open=0; if(b1>=0&&hb[b1]===0) open++; if(b2>=0&&hb[b2]===0) open++;
        if(open===2) s+=OPEN4; else s+=FOUR;
      }
      for(const w of T){
        const st=wSt[w], base=w%N; const b1=wB1[w], b2=wB2[w];
        S7[0]=b1<0?3:cv(hb[b1],p); for(let k=0;k<5;k++) S7[k+1]=cv(hb[base+k*st],p); S7[6]=b2<0?3:cv(hb[b2],p);
        let m=-1; for(let t=0;t<=2;t++) if(S7[t]===0&&S7[t+1]===1&&S7[t+2]===1&&S7[t+3]===1&&S7[t+4]===0){ m=t; break; }
        const openL=m===0||(m>0&&S7[m-1]<2), openR=m>=0&&(m+5>=7||S7[m+5]<2);
        let brk=false; for(let t=0;t<=3&&!brk;t++){ const x=S7[t],y=S7[t+1],z=S7[t+2],u=S7[t+3]; if((x===1&&y===1&&z===0&&u===1)||(x===1&&y===0&&z===1&&u===1)) brk=true; }
        if(m>=0&&openL&&openR){ s+=OPEN3; live3++; } else if(brk) s+=BRK3; else s+=6000;
      }
      if(live3>=2) s+=DBL3; return s;
    }
    function ev(me){ const v=E+evalSide(2)-evalSide(1); return me===2?v:-v; }
    function gen(me,forced,quiet,K){
      const opp=3-me; const oppWin=wins(opp);
      if(oppWin.length) return {lose: oppWin.length>1, cells: oppWin, scs: oppWin.map(()=>1000000), mks: oppWin.map(()=>1)};
      const myWin=wins(me); if(myWin.length) return {lose:false,cells:myWin,scs:myWin.map(()=>2000000),mks:myWin.map(()=>1)};
      // FIX: Chặn open 3 của địch với ưu tiên cực cao
      if(ts[opp].size>0){
        const blockCells=[]; const seen=new Set();
        for(const w of ts[opp]){ const st=wSt[w], base=w%N; for(let k=0;k<5;k++){ const c=base+k*st; if(hb[c]===0&&!seen.has(c)){ seen.add(c); blockCells.push(c); } } }
        if(blockCells.length){ return {lose:false,cells:blockCells,scs:blockCells.map(()=>950000),mks:blockCells.map(()=>1)}; }
      }
      const ofm=openFourMove(me); 
      const cells=[], scs=[], mks=[]; const seen=new Set();
      if(ofm>=0){ seen.add(ofm); cells.push(ofm); scs.push(900000); mks.push(1); }
      const candMap=new Map();
      for(let i=0;i<N;i++) if(hb[i]) for(const nb of nbr[i]) if(hb[nb]===0&&!seen.has(nb)&&!candMap.has(nb)&&near[nb]>0){
        let sc=hist[nb]*0.6 + near[nb]*12; const r=(nb/SIZE)|0, c=nb%SIZE; sc+= (SIZE - (Math.abs(r-SIZE/2)+Math.abs(c-SIZE/2)))*2;
        candMap.set(nb, sc);
      }
      for(let r=by0;r<=by1;r++) for(let c=bx0;c<=bx1;c++){ const id=self.idx(r,c); if(hb[id]===0&&!seen.has(id)&&!candMap.has(id)&&near[id]>0) candMap.set(id, hist[id]*0.3+5); }
      const sorted=[...candMap.entries()].sort((a,b)=>b[1]-a[1]).slice(0,K||64);
      for(const [id,sc] of sorted){ cells.push(id); scs.push(sc); mks.push(near[id]>2?1:0); }
      if(cells.length===0) for(let i=0;i<N;i++) if(hb[i]===0) {cells.push(i); scs.push(0); mks.push(0); if(cells.length>=K) break;}
      return {lose:false,cells,scs,mks};
    }
    function nm(depth,alpha,beta,me,ply,ext){
      nodes++; if((nodes & 2047)===0 && checkTime()) return 0; if(aborted) return 0;
      const enemy=wins(3-me); const forced=enemy.length===1; if(ply>=MAXPLY) return ev(me); const quiet=depth<=0;
      if(quiet&&!forced&&ext>=28) return ev(me); const a0=alpha; let best=-INF,bm=-1,ttm=-1,key=-1;
      if(!quiet){ key=(H1>>>0)&(TTN-1); if(TD[key]>=0&&TK[key]===(H2|0)){ ttm=TM[key]; const f=TF[key], sv=TS[key]; if(TD[key]>=depth&&(f===0||(f===1&&sv>=beta)||(f===2&&sv<=alpha))) return sv; } }
      if(quiet&&!forced){ best=ev(me); if(best>=beta) return best; if(best>alpha) alpha=best; }
      const g=forced?{lose:false,cells:enemy.slice(),scs:enemy.map(()=>0),mks:enemy.map(()=>1)}:gen(me,false,quiet,quiet?28:44);
      if(g.lose) return -WINV+ply+1; const cells=g.cells; if(!cells.length) return quiet?best:ev(me);
      let chosen=[]; const used=new Uint8Array(cells.length);
      const take=(v)=>{ if(v<0)return; const j=cells.indexOf(v); if(j>=0&&!used[j]){ used[j]=1; chosen.push(v); } };
      take(ttm); take(killer[ply*2]); take(killer[ply*2+1]); take(counter[bm]); for(let j=0;j<cells.length;j++) if(!used[j]) chosen.push(cells[j]);
      for(let j=0;j<chosen.length;j++){ const i=chosen[j]; let sc; if(put(i,me)) sc=WINV-ply; else{ const nd=depth-1, nx=quiet?ext+1:ext; if(j===0) sc=-nm(nd,-beta,-alpha,3-me,ply+1,nx); else{ let rd=0; if(j>=8&&depth>=4&&!forced&&!quiet){ const q=g.cells.indexOf(i); if(q>=0&&!g.mks[q]) rd=1; } sc=-nm(nd-rd,-alpha-1,-alpha,3-me,ply+1,nx); if(!aborted&&rd&&sc>alpha) sc=-nm(nd,-alpha-1,-alpha,3-me,ply+1,nx); if(!aborted&&sc>alpha&&sc<beta) sc=-nm(nd,-beta,-alpha,3-me,ply+1,nx); } } undo(i); if(aborted) return 0; if(sc>best){ best=sc; bm=i; counter[ply]=i; } if(sc>alpha) alpha=sc; if(alpha>=beta){ hist[i]=Math.min(500000,hist[i]+Math.max(4,depth*depth*3)); if(!quiet&&j>0){ killer[ply*2+1]=killer[ply*2]; killer[ply*2]=i; } break; } }
      if(!quiet&&bm>=0&&Math.abs(best)<WINV-1000&&(TD[key]<0||TD[key]<=depth+2)){ if(TD[key]<0) ttCount++; TK[key]=H2|0; TD[key]=depth; TS[key]=best; TF[key]=best<=a0?2:(best>=beta?1:0); TM[key]=bm; }
      return best;
    }
    function vcf(att,ply,maxPly){
      if((++vn&15)===0&&checkTime()) return -1; if(aborted||vn>vcfLim||ply>=maxPly) return -1;
      const own=wins(att); if(own.length) return own[0]; const dw=wins(3-att); if(dw.length>1) return -1;
      const moves=dw.length?dw:gen(att,false,true,80).cells;
      for(const i of moves){ if(aborted) break; const wonNow=put(i,att); let ok=wonNow; if(!ok){ const replies=wins(att), counter=wins(3-att); if(!counter.length){ if(replies.length>1) ok=true; else if(replies.length===1){ const b=replies[0]; const lost=put(b,3-att); if(!lost) ok=vcf(att,ply+2,maxPly)>=0; undo(b); } } } undo(i); if(ok) return i; }
      return -1;
    }
    function doRoot(depth,a,b,order,fbest){
      let bs=-INF, bi=fbest; const scored=[]; let al=a;
      for(let j=0;j<order.length;j++){ if((nodes&31)===0&&checkTime()) return {aborted:true,bs,bi,order}; const i=order[j]; let sc; if(put(i,2)) sc=WINV; else if(j===0) sc=-nm(depth-1,-b,-al,1,1,0); else{ sc=-nm(depth-1,-al-1,-al,1,1,0); if(!aborted&&sc>al&&sc<b) sc=-nm(depth-1,-b,-al,1,1,0); } undo(i); if(aborted) return {aborted:true,bs,bi,order}; scored.push([i,sc]); if(sc>bs){ bs=sc; bi=i; } if(sc>al) al=sc; if(al>=b) break; }
      scored.sort((x,y)=>y[1]-x[1]); return {aborted:false,bs,bi,order:scored.map(x=>x[0])};
    }
    // MAIN THINK
    const started=performance.now(); deadline=started+HELL_TIME; aborted=false; nodes=0; vn=0;
    hb=new Uint8Array(N); w1=new Uint8Array(4*N); w2=new Uint8Array(4*N); near=new Uint8Array(N);
    E=0; won=false; H1=0; H2=0; fs[1].clear(); fs[2].clear(); ts[1].clear(); ts[2].clear(); killer.fill(-1);
    let stones=0; for(let i=0;i<N;i++) if(self.board[i]){ put(i,self.board[i]); stones++; }
    let r0=SIZE,r1=-1,c0=SIZE,c1=-1; for(let i=0;i<N;i++) if(self.board[i]){ const r=(i/SIZE)|0, c=i%SIZE; if(r<r0)r0=r; if(r>r1)r1=r; if(c<c0)c0=c; if(c>c1)c1=c; }
    by0=Math.max(0,r0-8); by1=Math.min(SIZE-1,r1+8); bx0=Math.max(0,c0-8); bx1=Math.min(SIZE-1,c1+8); vcfLim=VCF_NODES;
    if(!stones){ postMessage({id, m: self.idx(SIZE>>1,SIZE>>1), stats:{reason:'center', ms:1}}); return; }
    const immediate=wins(2); if(immediate.length){ postMessage({id, m: immediate[0], stats:{reason:'win'}}); return; }
    const blocks=wins(1); if(blocks.length===1){ postMessage({id, m: blocks[0], stats:{reason:'forced-block'}}); return; }
    if(blocks.length>=2){ let bestB=blocks[0], bestS=-INF; for(const b of blocks){ put(b,2); const s=ev(2); undo(b); if(s>bestS){ bestS=s; bestB=b; } } postMessage({id, m: bestB, stats:{reason:'double-block'}}); return; }
    // FIX: Chặn ngay OPEN 3 của đối phương - bug báo: không chặn 3 mở của X mà đi đánh 3 của mình
    if(ts[1].size>0){
      // đối phương có 3 mở -> phải chặn ngay
      const blockOpen3 = [];
      const seenB = new Set();
      for(const w of ts[1]){
        const st=wSt[w], base=w%N;
        for(let k=0;k<5;k++){ const c=base+k*st; if(hb[c]===0 && !seenB.has(c)){ seenB.add(c); blockOpen3.push(c); } }
      }
      // nếu có 2 chỗ 3 mở khác nhau -> thua ngay, chặn chỗ nào cũng được nhưng phải chặn
      if(blockOpen3.length>0){
        // ưu tiên ô chặn được nhiều 3 mở nhất
        let bestO3=blockOpen3[0], bestCnt=0;
        for(const b of blockOpen3){
          let cnt=0; put(b,2);
          // đếm còn lại bao nhiêu open3 của địch
          let remain=0; for(const w of ts[1]) remain++;
          undo(b);
          // nếu chặn xong vẫn còn open3 -> vẫn nguy hiểm, nhưng vẫn phải chặn
          if(remain===0) { bestO3=b; break; }
          // chọn ô gần nhất với thế của địch
          if(cnt>=bestCnt){ bestCnt=cnt; bestO3=b; }
        }
        // Nếu chỉ có 1 open3 và chỉ có 1-2 ô chặn, chặn luôn
        if(ts[1].size>=1){
          postMessage({id, m: bestO3, stats:{reason:'block-open3-enemy'}}); return;
        }
      }
    }
    // FIX: Nếu mình có open 4 thì đánh ngay (thắng nhanh)
    {
      const myOpen4Moves=[];
      const seen=new Set();
      for(const w of fs[2]){ const b1=wB1[w], b2=wB2[w]; if(b1>=0&&hb[b1]===0&&!seen.has(b1)){seen.add(b1); myOpen4Moves.push(b1);} if(b2>=0&&hb[b2]===0&&!seen.has(b2)){seen.add(b2); myOpen4Moves.push(b2);} const e=emptyOf(w); if(e>=0&&!seen.has(e)){seen.add(e); myOpen4Moves.push(e);} }
      if(myOpen4Moves.length){ /* sẽ được gen xử lý nhưng ưu tiên */ }
    }
    const g0=gen(2,false,false,128); const cand=[];
    for(let j=0;j<g0.cells.length;j++){ const i=g0.cells[j]; put(i,2); const v=ev(2); undo(i); cand.push([i, v + Math.min(hist[i],8000) + g0.scs[j]*0.06]); }
    if(!cand.length){ for(let i=0;i<N;i++) if(!hb[i]){ postMessage({id, m:i, stats:{}}); return;} }
    cand.sort((a,b)=>b[1]-a[1]); let order=cand.slice(0,96).map(c=>c[0]);
    if(pen){ const lo=order.filter(i=>pen[i]>=500); if(lo.length&&lo.length<order.length){ let rest=order.filter(i=>!(pen[i]>=500)); order=rest.length>=5?rest.concat(lo.filter(i=>!(pen[i]>=900))):rest.concat(lo); } }
    let best=order[0], completed=0, bestScore=-INF, stable=0; const fullDeadline=deadline;
    deadline=Math.min(fullDeadline, performance.now()+Math.min(4000,HELL_TIME*0.28));
    const proof=vcf(2,0,96); deadline=fullDeadline; aborted=false;
    if(proof<0){
      vn=0; vcfLim=400000; deadline=Math.min(fullDeadline,performance.now()+2000);
      const thr=vcf(1,0,80); aborted=false;
      if(thr>=0){ if(order.indexOf(thr)<0) order.unshift(thr); const safe=[]; let k=0; for(;k<order.length;k++){ const i=order[k]; vn=0; if(put(i,2)){ safe.push(i); undo(i); continue; } const r=vcf(1,0,80); undo(i); if(aborted) break; if(r<0) safe.push(i); } for(;k<order.length;k++) safe.push(order[k]); aborted=false; if(safe.length) order=safe; }
      deadline=fullDeadline; vcfLim=VCF_NODES; vn=0;
    }
    if(proof>=0){ postMessage({id, m:proof, stats:{depth:0,nodes:nodes,vcfNodes:vn,ms:Math.round(performance.now()-started),reason:'proved-vcf'}}); return; }
    let lastIterMs=0;
    for(let depth=1; depth<=48; depth++){
      if(depth>=10&&lastIterMs>0){ const remaining=deadline-performance.now(); if(remaining<=0||lastIterMs*2.2>remaining) break; if(performance.now()-started>HELL_SOFT&&stable>=2&&bestScore>-WINV/2) break; }
      if(order.length<=1&&depth>=4) break; let a0=-INF,b0=INF; if(depth>=7&&Math.abs(bestScore)<WINV-1000){ a0=bestScore-400; b0=bestScore+400; }
      const tIt=performance.now(); let r=doRoot(depth,a0,b0,order,best); if(r.aborted) break;
      if(b0!==INF&&(r.bs<=a0||r.bs>=b0)&&!checkTime()){ r=doRoot(depth,-INF,INF,order,best); if(r.aborted) break; }
      stable=(r.bi===best)?stable+1:0; best=r.bi; bestScore=r.bs; completed=depth; order=r.order; lastIterMs=performance.now()-tIt; if(Math.abs(bestScore)>WINV-1000) break;
    }
    postMessage({id, m:best, stats:{depth:completed,nodes:nodes,vcfNodes:vn,ms:Math.round(performance.now()-started),score:bestScore,reason:'search-rapfi-hybrid',tt:ttCount}});
  }catch(err){
    postMessage({id, m:-1, stats:{reason:'error '+err.message}});
  }
};
