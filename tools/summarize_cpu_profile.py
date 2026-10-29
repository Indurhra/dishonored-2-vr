import csv,json,sys,statistics,collections
from pathlib import Path
p=Path(sys.argv[1]); groups=collections.defaultdict(lambda:[[],[],set()])
for r in csv.DictReader(p.open()):
 if float(r['start_seconds'])<2:continue
 a=groups[(r['name'],int(r['thread']))];a[0].append(float(r['inclusive_ms']));a[1].append(float(r['exclusive_ms']));a[2].add(int(r['present_epoch']))
def stats(a):
 a.sort();return dict(mean=statistics.mean(a),p50=a[len(a)//2],p95=a[int((len(a)-1)*.95)],p99=a[int((len(a)-1)*.99)],max=a[-1],sum=sum(a))
out=[dict(name=k[0],thread=k[1],sampled_calls=len(v[0]),sampled_epochs=len(v[2]),inclusive=stats(v[0]),exclusive=stats(v[1])) for k,v in groups.items()]
out.sort(key=lambda r:r['exclusive']['sum'],reverse=True)
Path(sys.argv[2]).write_text(json.dumps(out,indent=2))
for r in out[:24]:print(r['name'],r['thread'],r['sampled_calls'],'exclusive total/mean',round(r['exclusive']['sum'],3),round(r['exclusive']['mean'],4),'inclusive p95/max',round(r['inclusive']['p95'],3),round(r['inclusive']['max'],3))
