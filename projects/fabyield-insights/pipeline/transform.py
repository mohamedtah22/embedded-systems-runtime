"""Streaming CSV validation and yield analytics. Python standard library only."""
from __future__ import annotations
import argparse
from collections import defaultdict
import csv
from datetime import datetime
import json
import os
from pathlib import Path
import tempfile

REQUIRED = ('event_id','timestamp','lot_id','wafer_id','tool_id','process_step',
            'dies_tested','dies_passed','cycle_seconds','temperature_c')


def validate(row: dict) -> dict:
    for key in REQUIRED:
        if key not in row or row[key] is None or not str(row[key]).strip():
            raise ValueError(f'missing {key}')
    tested, passed = int(row['dies_tested']), int(row['dies_passed'])
    if not 0 <= passed <= tested or tested == 0 or tested > 100000:
        raise ValueError('inconsistent die counts')
    cycle = float(row['cycle_seconds'])
    temperature = float(row['temperature_c'])
    if not 0 < cycle < 100000 or not -50 <= temperature <= 200:
        raise ValueError('invalid numeric reading')
    ts = datetime.fromisoformat(row['timestamp'])
    if ts.tzinfo is None:
        raise ValueError('timestamp must include timezone')
    return {'event':row['event_id'], 'timestamp':ts,'lot':row['lot_id'],
            'tool':row['tool_id'],'step':row['process_step'],
            'tested':tested,'passed':passed,'cycle':cycle,'temperature':temperature}


def _summary(k: str, v: dict) -> dict:
    total, passed = v['tested'], v['passed']
    return {'id':k, 'wafers':v['wafers'], 'diesTested':total, 'diesPassed':passed,
            'diesFailed':total-passed,'yieldPct':round(passed/total*100,2),
            'avgCycleSeconds':round(v['cycle']/v['wafers'],2),
            'avgTemperatureC':round(v['temperature']/v['wafers'],2)}


def _bucket():
    return {'tested':0,'passed':0,'wafers':0,'cycle':0.0,'temperature':0.0}


def build(events: Path, notes: Path, max_invalid: int = 10000) -> dict:
    """Reject bad rows and duplicate event IDs; aggregate in O(lots + tools + days) memory."""
    lots=defaultdict(_bucket); tools=defaultdict(_bucket); days=defaultdict(_bucket)
    seen=set(); invalid=[]; accepted=0; tested=passed=0
    with events.open(newline='',encoding='utf-8') as fh:
        for line,row in enumerate(csv.DictReader(fh),start=2):
            try:
                r=validate(row)
                if r['event'] in seen: raise ValueError('duplicate event_id')
                seen.add(r['event'])
            except (ValueError,TypeError,OverflowError) as err:
                if len(invalid)<max_invalid:
                    invalid.append({'line':line,'reason':str(err)})
                continue
            accepted+=1;tested+=r['tested'];passed+=r['passed']
            for b in (lots[r['lot']],tools[r['tool']],days[r['timestamp'].date().isoformat()]):
                b['tested']+=r['tested'];b['passed']+=r['passed'];b['wafers']+=1
                b['cycle']+=r['cycle'];b['temperature']+=r['temperature']
            lots[r['lot']]['toolId']=r['tool'];lots[r['lot']]['processStep']=r['step']
    if accepted == 0:
        raise ValueError('no valid events; refusing to publish empty analytics')
    lot_rows=[]
    for key,v in sorted(lots.items()):
        lot_rows.append({**_summary(key,v),'toolId':v['toolId'],'processStep':v['processStep']})
    tool_rows=[_summary(k,v) for k,v in sorted(tools.items())]
    trend=[{'date':k,**{name:val for name,val in _summary(k,v).items() if name!='id'}}
           for k,v in sorted(days.items())]
    # The simulated ETCH-02 tool deliberately has an elevated failure rate.
    alerts=[]
    for l in lot_rows:
        if l['yieldPct'] < 93.0:
            alerts.append({'lotId':l['id'],'toolId':l['toolId'],
                           'severity':'high' if l['yieldPct'] < 90 else 'medium',
                           'metric':'yieldPct','observed':l['yieldPct'],'threshold':93.0,
                           'reason':'Lot yield below synthetic demo threshold'})
    note_rows=[];note_errors=0
    if notes.exists():
        with notes.open(encoding='utf-8') as fh:
            for line in fh:
                if not line.strip():continue
                try:
                    obj=json.loads(line)
                    if not isinstance(obj,dict): raise ValueError('object required')
                    lot=obj['lotId'];message=obj['text']
                    if not isinstance(message,str) or not isinstance(lot,str):raise ValueError('invalid note')
                    if lot not in lots:raise ValueError('unknown lot')
                    note_rows.append({'lotId':lot,'message':message[:300],
                                      'flagged':any(x in message.lower() for x in ('defect','spike','deviation','failure'))})
                except (ValueError,KeyError,TypeError):note_errors+=1
    return {
      'meta':{'source':'deterministic synthetic demo only','rowsAccepted':accepted,
              'rowsRejected':len(invalid),'notesRejected':note_errors,'schemaVersion':1},
      'summary':{'totalLots':len(lot_rows),'totalWafers':accepted,'diesTested':tested,
                 'diesPassed':passed,'diesFailed':tested-passed,
                 'overallYieldPct':round(passed/tested*100,2),'alertCount':len(alerts)},
      'lots':lot_rows,'tools':tool_rows,'trend':trend,
      'alerts':alerts,'notes':note_rows,'invalidRows':invalid,
    }


def write_atomic(destination:Path,obj:dict)->None:
    destination.parent.mkdir(parents=True,exist_ok=True)
    temp=None
    try:
        with tempfile.NamedTemporaryFile('w',encoding='utf-8',dir=destination.parent,
                                         prefix='.snapshot-',suffix='.json',delete=False) as f:
            temp=Path(f.name)
            json.dump(obj,f,indent=2,ensure_ascii=False,allow_nan=False)
            f.write('\n')
            f.flush();os.fsync(f.fileno())
        os.replace(temp,destination)
    finally:
        if temp and temp.exists():temp.unlink()

if __name__ == '__main__':
    ap=argparse.ArgumentParser()
    ap.add_argument('--input',type=Path,default=Path('data/events.csv'))
    ap.add_argument('--notes',type=Path,default=Path('data/technician_notes.jsonl'))
    ap.add_argument('--output',type=Path,default=Path('data/snapshot.json'))
    a=ap.parse_args()
    result=build(a.input,a.notes)
    write_atomic(a.output,result)
    print(json.dumps({'summary':result['summary'],'quality':result['meta']}))
