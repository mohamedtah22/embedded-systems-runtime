import csv
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from generate_demo import generate
from transform import build, validate, write_atomic

FIELDS=['event_id','timestamp','lot_id','wafer_id','tool_id','process_step',
        'dies_tested','dies_passed','cycle_seconds','temperature_c']

def row(**changes):
    base=dict(event_id='1',timestamp='2026-09-01T00:00:00+00:00',lot_id='L-1',wafer_id='W1',
              tool_id='ETCH-02',process_step='etch',dies_tested='100',dies_passed='80',
              cycle_seconds='20',temperature_c='26')
    base.update(changes)
    return base

class ValidationTests(unittest.TestCase):
    def test_accepts_valid(self): self.assertEqual(validate(row())['passed'],80)
    def test_rejects_invalid_counts(self):
        for kwargs in [dict(dies_tested='0'),dict(dies_passed='101'),dict(dies_passed='-1')]:
            with self.subTest(kwargs=kwargs),self.assertRaises(ValueError):validate(row(**kwargs))
    def test_rejects_missing(self):
        with self.assertRaises(ValueError): validate(row(event_id=''))
    def test_rejects_naive_timestamp(self):
        with self.assertRaises(ValueError): validate(row(timestamp='2026-09-01T00:00:00'))
    def test_rejects_bad_numeric(self):
        for kwargs in [dict(temperature_c='500'),dict(cycle_seconds='-2'),dict(dies_tested='wat')]:
            with self.subTest(kwargs=kwargs),self.assertRaises(ValueError):validate(row(**kwargs))

class AggregationTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.events=self.root/'events.csv';self.notes=self.root/'notes.jsonl'
    def write(self,rows):
        with self.events.open('w',newline='',encoding='utf-8') as f:
            w=csv.DictWriter(f,fieldnames=FIELDS);w.writeheader();w.writerows(rows)
    def test_weighted_yield_not_average_of_percentages(self):
        self.write([row(event_id='1',dies_tested='100',dies_passed='80'),
                    row(event_id='2',dies_tested='10',dies_passed='10')])
        s=build(self.events,self.notes)
        self.assertEqual(s['summary']['overallYieldPct'],81.82)
        self.assertEqual(s['summary']['diesFailed'],20)
    def test_duplicate_and_corrupt_quarantined(self):
        self.write([row(),row(),row(event_id='3',dies_passed='101'),row(event_id='4',lot_id='L2')])
        s=build(self.events,self.notes)
        self.assertEqual(s['meta']['rowsAccepted'],2)
        self.assertEqual(s['meta']['rowsRejected'],2)
        self.assertEqual(s['summary']['totalLots'],2)
        self.assertTrue(s['invalidRows'][0]['reason'].startswith('duplicate'))
    def test_alert_and_unstructured_note(self):
        self.write([row()])
        self.notes.write_text(json.dumps({'lotId':'L-1','text':'Defect spike observed'})+'\n',encoding='utf-8')
        s=build(self.events,self.notes)
        self.assertEqual(s['summary']['alertCount'],1)
        self.assertTrue(s['notes'][0]['flagged'])
    def test_unknown_note_not_accepted(self):
        self.write([row()])
        self.notes.write_text(json.dumps({'lotId':'none','text':'Hi'})+'\n')
        self.assertEqual(build(self.events,self.notes)['meta']['notesRejected'],1)
    def test_no_valid_data_fails(self):
        self.write([row(dies_passed='101')])
        with self.assertRaises(ValueError):build(self.events,self.notes)
    def test_atomic_file_writes_valid_json(self):
        out=self.root/'sub'/'snapshot.json';write_atomic(out,{'lots':[1]})
        self.assertEqual(json.loads(out.read_text())['lots'],[1])
        write_atomic(out,{'lots':[2]})
        self.assertEqual(json.loads(out.read_text())['lots'],[2])
    def test_seeded_generation_is_reproducible(self):
        a,b=self.root/'a.csv',self.root/'b.csv';na,nb=self.root/'a.jsonl',self.root/'b.jsonl'
        generate(a,na,lots=5,wafers=8)
        generate(b,nb,lots=5,wafers=8)
        self.assertEqual(a.read_bytes(),b.read_bytes())
        self.assertEqual(na.read_bytes(),nb.read_bytes())
        self.assertTrue(build(a,na)['summary']['alertCount']>0)
    def test_generated_dataset_has_correct_counts(self):
        generate(self.events,self.notes,lots=10,wafers=3)
        d=build(self.events,self.notes)
        self.assertEqual(d['summary']['totalLots'],10)
        self.assertEqual(d['summary']['totalWafers'],30)
        self.assertEqual(d['meta']['rowsRejected'],0)

if __name__=='__main__':unittest.main()
