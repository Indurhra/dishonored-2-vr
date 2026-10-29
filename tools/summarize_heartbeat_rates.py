"""Compute counter rates from timestamped heartbeat snapshots; preserve mode labels."""
import argparse,json,re,datetime
p=argparse.ArgumentParser();p.add_argument('jsonl');a=p.parse_args()
rows=[];seen=set();discarded=0
for line in open(a.jsonl,encoding='utf-8-sig'):
 r=json.loads(line);t=datetime.datetime.fromisoformat(r['utc']).timestamp()
 if t in seen:continue
 text=r.get('text')
 if not isinstance(text,str) or not text.strip():
  discarded+=1;continue
 seen.add(t)
 def get(pattern):
  m=re.search(pattern,text);return int(m.group(1)) if m else None
 rows.append((t,{'presents':get(r'^frames (\d+)'),'xr_submission_attempts':get(r'\nxr_frames (\d+)'),
 'xr_runtime_accepted':get(r'\nxr_accepted (\d+)'),'native_pairs_runtime_accepted':get(r'\nxr_native_pairs_accepted (\d+)'),
 'native_pairs':get(r'syncstereo staged \d+ stagefail \d+ pairs (\d+)'),
 'dropped_pairs':get(r'leftalone \d+ dropped (\d+)'),
 'mode':get(r'xr_stereo \d+ mode (\d+)')}))
rows.sort();rows=[r for r in rows if r[0]>=rows[0][0]+3]
if len(rows)<3:raise SystemExit('Insufficient samples')
t0,x=rows[0];t1,y=rows[-1];dt=t1-t0
if x['mode']!=y['mode']:raise SystemExit('Renderer transition in interval')
print(json.dumps({'seconds':dt,'mode':x['mode'],'discarded_empty_snapshots':discarded, 'rates_per_second':{k:(y[k]-x[k])/dt for k in x if k!='mode' and x[k] is not None and y[k] is not None},
 'note':'xr_frames counts attempts; inspect end-frame failures. AER refreshes one eye only and is not AFW.'},indent=2))
