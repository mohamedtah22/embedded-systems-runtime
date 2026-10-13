import { useEffect, useMemo, useState } from 'react';

type Summary = { totalLots:number;totalWafers:number;diesTested:number;diesPassed:number;diesFailed:number;overallYieldPct:number;alertCount:number };
type Meta = {source:string;rowsAccepted:number;rowsRejected:number;notesRejected:number;schemaVersion:number};
type Lot = {id:string;toolId:string;processStep:string;wafers:number;diesTested:number;diesFailed:number;yieldPct:number;avgCycleSeconds:number};
type Tool = {id:string;yieldPct:number;wafers:number;diesTested:number;diesFailed:number};
type Trend = {date:string;yieldPct:number;wafers:number};
type Alert = {lotId:string;toolId:string;severity:string;observed:number;reason:string};
type Note = {lotId:string;message:string;flagged:boolean};
type Payload = {summary:Summary;meta:Meta;lots:Lot[];tools:Tool[];trend:Trend[];alerts:Alert[];notes:Note[]};

async function load<T>(route:string): Promise<T> {
  const response=await fetch('/api/'+route);
  if(!response.ok) throw new Error('API '+route+' returned HTTP '+response.status);
  return response.json() as Promise<T>;
}
const numeral=(n:number)=>new Intl.NumberFormat('en-US').format(n);
const pct=(n:number)=>n.toFixed(2)+'%';

function TrendChart({points}:{points:Trend[]}) {
  const w=650,h=200, pad=28;
  const visible=points.slice(-32);
  const values=visible.map(p=>p.yieldPct);
  const lo=Math.min(...values,80)-1, hi=Math.max(...values,100)+1;
  const coords=visible.map((p,i)=>{
    const x=pad+i*(w-2*pad)/Math.max(visible.length-1,1);
    const y=h-pad-(p.yieldPct-lo)/(hi-lo)*(h-2*pad);
    return `${x.toFixed(1)},${y.toFixed(1)}`;
  }).join(' ');
  return <div className="chart" role="img" aria-label="Daily yield trend as a line chart">
    <svg viewBox={`0 0 ${w} ${h}`} preserveAspectRatio="none">
      {[0,1,2,3,4].map(i=><line key={i} x1={pad} x2={w-pad} y1={pad+i*(h-2*pad)/4} y2={pad+i*(h-2*pad)/4} stroke="currentColor" strokeOpacity=".09"/>) }
      <polyline fill="none" stroke="#30d4bc" strokeWidth="3.2" strokeLinejoin="round" strokeLinecap="round" points={coords}/>
      {visible.map((p,i)=> <circle key={p.date} cx={pad+i*(w-2*pad)/Math.max(visible.length-1,1)} cy={h-pad-(p.yieldPct-lo)/(hi-lo)*(h-2*pad)} r="3.5" fill="#30d4bc"><title>{p.date}: {pct(p.yieldPct)}</title></circle>)}
    </svg>
    <div className="chart-legend"><span>{visible[0]?.date}</span><span>{visible[visible.length-1]?.date}</span></div>
  </div>;
}

export default function App() {
  const [payload,setPayload]=useState<Payload|null>(null);
  const [loading,setLoading]=useState(true);
  const [error,setError]=useState('');
  const [tool,setTool]=useState('all');
  const [search,setSearch]=useState('');
  const [showAll,setShowAll]=useState(false);
  const reload=()=>{
    setLoading(true);setError('');
    Promise.all([load<{summary:Summary;meta:Meta}>('summary'),load<{items:Lot[]}>('lots?pageSize=100'),load<Tool[]>('tools'),load<Trend[]>('trend'),load<Alert[]>('alerts'),load<Note[]>('notes')])
      .then(([main,lots,tools,trend,alerts,notes])=>setPayload({...main,lots:lots.items,tools,trend,alerts,notes}))
      .catch(e=>setError(String(e))).finally(()=>setLoading(false));
  };
  useEffect(()=>{reload();},[]);
  const lots=useMemo(()=>payload?.lots.filter(l=>(tool==='all'||l.toolId===tool)&&l.id.toLowerCase().includes(search.trim().toLowerCase()))??[],[payload,tool,search]);
  return <div className="page">
    <aside className="rail"><div className="logo-mark">F<span>Y</span></div><div className="rail-icon active">▦</div><div className="rail-icon">⌁</div><div className="rail-icon">◎</div><div className="rail-end">DEMO</div></aside>
    <main className="main">
      <header className="head">
        <div><div className="eyebrow">MANUFACTURING INTELLIGENCE <span className="eyebrow-sep">/</span> ENGINEERING OVERVIEW</div><h1>FabYield <span>Insights</span></h1><p className="subtitle">Quality telemetry, actionable deviations, and traceable process analytics.</p></div>
        <div className="head-side"><span className="live"><i/> LIVE DEMO DATA</span><button type="button" className="refresh" onClick={reload}>↻ Refresh analytics</button></div>
      </header>
      <div className="notice"><span className="notice-dot">i</span><span><strong>Synthetic dataset.</strong> Fabricated semiconductor events for portfolio demonstration. No real Intel or customer records.</span></div>
      {loading && <div className="status">Loading yield telemetry…</div>}
      {error && <div className="status error" role="alert">Data service unavailable: {error}. <button onClick={reload}>Retry</button></div>}
      {payload && !loading && <>
        <section className="kpis" aria-label="Key metrics">
          <div className="kpi"><div className="kpi-label">GLOBAL DIE YIELD <span>↗</span></div><strong>{pct(payload.summary.overallYieldPct)}</strong><small>Weighted by tested dies</small><div className="kpi-accent mint"/></div>
          <div className="kpi"><div className="kpi-label">PROCESSED WAFERS <span>◫</span></div><strong>{numeral(payload.summary.totalWafers)}</strong><small>Across {payload.summary.totalLots} production lots</small><div className="kpi-accent blue"/></div>
          <div className="kpi"><div className="kpi-label">DIES TESTED <span>⌗</span></div><strong>{numeral(payload.summary.diesTested)}</strong><small>{numeral(payload.summary.diesFailed)} failed dies identified</small><div className="kpi-accent purple"/></div>
          <div className="kpi"><div className="kpi-label">YIELD ALERTS <span>⚠</span></div><strong className="alert-number">{payload.summary.alertCount}</strong><small>Below demonstration threshold</small><div className="kpi-accent orange"/></div>
        </section>
        <section className="insights">
          <div className="panel trend-panel"><div className="panel-head"><div><div className="panel-label">PROCESS PERFORMANCE</div><h2>Yield over time</h2></div><span className="subpill">Daily mean</span></div><TrendChart points={payload.trend}/></div>
          <div className="panel tools-panel"><div className="panel-head"><div><div className="panel-label">TOOL COMPARISON</div><h2>Yield by equipment</h2></div></div><div className="tool-bars">{payload.tools.map(t=><div className="tool-row" key={t.id}><div className="tool-meta"><span>{t.id}</span><strong>{pct(t.yieldPct)}</strong></div><div className="bar-track"><div className={'bar '+(t.yieldPct<93?'warning':'')} style={{width:`${t.yieldPct}%`}}/></div></div>)}</div></div>
        </section>
        <section className="insights lower">
          <div className="panel table-panel"><div className="panel-head"><div><div className="panel-label">QUALITY INVESTIGATION</div><h2>Lot yield explorer</h2></div><span className="subpill">{lots.length} matches</span></div>
            <div className="filters"><input aria-label="Search lot" placeholder="Search lot ID…" value={search} onChange={e=>setSearch(e.target.value)}/><select aria-label="Filter tool" value={tool} onChange={e=>setTool(e.target.value)}><option value="all">All equipment</option>{payload.tools.map(t=><option key={t.id} value={t.id}>{t.id}</option>)}</select></div>
            <div className="table-scroll"><table><thead><tr><th>LOT ID</th><th>TOOL</th><th>WAFERS</th><th>YIELD</th><th>STATUS</th></tr></thead><tbody>{lots.slice(0,showAll?100:9).map(l=><tr key={l.id}><td className="lot-id">{l.id}</td><td>{l.toolId}</td><td>{l.wafers}</td><td className="value">{pct(l.yieldPct)}</td><td><span className={'status-tag '+(l.yieldPct<93?'warn':'good')}>{l.yieldPct<93?'Review':'Normal'}</span></td></tr>)}{lots.length===0&&<tr><td colSpan={5}>No matching lots.</td></tr>}</tbody></table></div>
            {lots.length>9&&<button className="text-button" type="button" onClick={()=>setShowAll(v=>!v)}>{showAll?'Show fewer ↑':'View all matching lots →'}</button>}
          </div>
          <div className="panel alert-panel"><div className="panel-head"><div><div className="panel-label">DIAGNOSTIC FEED</div><h2>Engineering alerts</h2></div><span className="alerts-pill">{payload.alerts.length} alerts</span></div>
            <div className="alert-feed">{payload.alerts.slice(0,5).map(a=><article key={a.lotId} className="alert-item"><div className="alert-icon">!</div><div><div className="alert-title">Low yield · {a.lotId}</div><div className="alert-detail">{a.toolId} · {pct(a.observed)} · Threshold 93.00%</div></div><span className={'severity '+a.severity}>{a.severity}</span></article>)}</div>
            <div className="note-block"><div className="panel-label">TECHNICIAN SIGNALS</div><p>{payload.notes.filter(n=>n.flagged).length} notes contain possible issue keywords. Notes are <strong>contextual signals</strong>, not confirmed root causes.</p></div>
          </div>
        </section>
        <footer><span>FABYIELD LAB / PORTFOLIO REFERENCE ARCHITECTURE</span><span>DATASET {payload.meta.rowsAccepted} VALID · {payload.meta.rowsRejected} QUARANTINED</span></footer>
      </>}
    </main>
  </div>;
}
