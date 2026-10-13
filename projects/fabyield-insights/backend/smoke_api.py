"""End-to-end HTTP smoke test for the REAL running ASP.NET backend (no mocks)."""
from __future__ import annotations
import json
import sys
from urllib.error import HTTPError
from urllib.request import urlopen

base=sys.argv[1].rstrip('/') if len(sys.argv)>1 else 'http://127.0.0.1:8080'

def get(path,code=200):
    try:
        with urlopen(base+path,timeout=5) as r:
            assert r.status==code,(path,r.status)
            return json.load(r)
    except HTTPError as e:
        assert e.code==code,(path,e.code)
        return json.load(e)

assert get('/api/health')['status']=='ok'
s=get('/api/summary')
assert s['summary']['totalLots']>=5
assert s['meta']['source']=='deterministic synthetic demo only'
assert 0 < s['summary']['overallYieldPct'] < 100
assert len(get('/api/tools'))>=2
assert len(get('/api/trend'))>=2
lots=get('/api/lots?page=1&pageSize=5')
assert lots['total']>=5 and len(lots['items'])==5
low=lots['items'][0]
assert 'yieldPct' in low and 'toolId' in low
narrow=get('/api/lots?tool='+low['toolId']+'&pageSize=3')
assert all(x['toolId']==low['toolId'] for x in narrow['items'])
assert get('/api/alerts')
assert get('/api/notes')
assert get('/api/data-quality')['meta']['rowsRejected']==0
assert get('/api/lots?page=0',400)['error']
assert get('/api/lots?pageSize=1000',400)['error']
assert get('/api/alerts?severity=invalid',400)['error']
print('PASS: ASP.NET Core HTTP smoke (health, aggregation, pagination, filtering, notes, alerts, invalid inputs)')
