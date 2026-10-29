import csv,json,statistics,collections
from pathlib import Path
root=Path('local/frame-profile');g=Path('Q:/SteamLibrary/steamapps/common/Dishonored2')
def stats(v):
 v=sorted(v);return dict(n=len(v),mean=statistics.mean(v),median=statistics.median(v),p95=v[int((len(v)-1)*.95)],p99=v[int((len(v)-1)*.99)],max=max(v))
out={}
for tag in ['final_cpu_native','final_cpu_copy','identity_cpu','final_fine','candidate_cpu','candidate_native_cpu','scan_cpu_off','scan_cpu_on']:
 p=g/f'DVR_cpu_{tag}.csv'
 if not p.exists():continue
 groups=collections.defaultdict(list);worst=collections.defaultdict(list)
 for r in csv.DictReader(p.open()):
  if float(r['start_seconds'])<2:continue
  groups[r['name']].append(r)
 result={}
 for name,rows in groups.items():
  result[name]={'inclusive_ms':stats([float(r['inclusive_ms']) for r in rows]),'exclusive_ms':stats([float(r['exclusive_ms']) for r in rows]),'threads':sorted(set(int(r['thread']) for r in rows)),'worst_calls':sorted(rows,key=lambda r:float(r['inclusive_ms']),reverse=True)[:5]}
 out[tag]=result
for tag in ['final_gpu_copy','candidate_gpu']:
 p=g/f'DVR_dlss_{tag}.csv'
 if not p.exists():continue
 groups=collections.defaultdict(list)
 for r in csv.DictReader(p.open()):
  if float(r['elapsed_seconds'])>=2:groups[r['stage']].append(float(r['total_gpu_ms']))
 out[tag]={s:stats(v) for s,v in groups.items()}
root.joinpath('profile-analysis.json').write_text(json.dumps(out,indent=2))
for tag,groups in out.items():
 print(tag)
 for name,v in groups.items():
  if name.startswith('interval.') or name in ('Main.OnPresent','input.SendInput','DLSS.temporalEvaluation') or tag.endswith('gpu_copy') or tag=='candidate_gpu':print(name,v.get('inclusive_ms',v))
