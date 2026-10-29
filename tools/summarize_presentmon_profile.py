import csv,json,statistics,collections
from pathlib import Path
out={}
for mode in ('native','afw'):
 p=Path('local/frame-profile')/f'presentmon-{mode}.csv'
 rows=[r for r in csv.DictReader(p.open()) if float(r['CPUStartTime'])>2000]
 groups=collections.defaultdict(list)
 for r in rows:
  for k in ('FrameTime','CPUBusy','CPUWait','GPULatency','GPUTime','GPUBusy','GPUWait'):
   if r[k]!='NA':groups[k].append(float(r[k]))
 def stats(v):
  v.sort();return dict(n=len(v),mean=statistics.mean(v),median=statistics.median(v),p95=v[int((len(v)-1)*.95)],p99=v[int((len(v)-1)*.99)],max=max(v))
 out[mode]={k:stats(v) for k,v in groups.items()}
 print(mode,json.dumps(out[mode],indent=2))
Path('local/frame-profile/presentmon-summary.json').write_text(json.dumps(out,indent=2))
