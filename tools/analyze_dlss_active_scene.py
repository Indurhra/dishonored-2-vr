"""Summarise this investigation after captures complete; never infer headset FPS from Presents."""
from pathlib import Path
import csv,json,statistics,hashlib
root=Path('local/frame-profile'); evidence=Path('local/early-scene')
bench=json.loads((root/'benchmark-results.json').read_text())
selected={k:v for k,v in bench.items() if k.startswith('early_')}
def stats(v):
 v=sorted(v);return dict(n=len(v),mean=statistics.mean(v),median=statistics.median(v),p95=v[int((len(v)-1)*.95)],p99=v[int((len(v)-1)*.99)],max=max(v))
gpu={}
for p in sorted(evidence.glob('gpu_*.csv')):
 rows=[r for r in csv.DictReader(p.open()) if r.get('CPUStartTime') not in (None,'NA') and float(r['CPUStartTime'])>2000]
 data={}
 for key in ('FrameTime','CPUBusy','CPUWait','GPUTime','GPUBusy','GPUWait'):
  vals=[float(r[key]) for r in rows if r.get(key) not in (None,'NA','')]
  if vals:data[key]=stats(vals)
 gpu[p.stem]={'metrics':data,'metadata':json.loads(p.with_suffix('.metadata.json').read_text(encoding='utf-8-sig'))}
result={'dll_sha256':hashlib.sha256(Path('build-q/bin/DishonoredVR.dll').read_bytes()).hexdigest().upper(),'conditions':'MockXR, fixed scene, 3072x3264 output/eye, game allocation 3286x3555; accepted stereo counters; no physical headset acceptance','benchmarks':selected,'run_metadata':{k:json.loads((root/f'{k}.jsonl.metadata.json').read_text(encoding='utf-8-sig')) for k in selected},'sr_status':{k:json.loads((root/f'{k}.sr.json').read_text(encoding='utf-8-sig')) for k in selected if (root/f'{k}.sr.json').exists()},'gpu':gpu,'pass_audit':json.loads((evidence/'pass-summary.json').read_text())}
Path('docs/results/dlss-active-scene-2026-10-28.json').write_text(json.dumps(result,indent=2))
for tag,r in selected.items():print(tag,round(r['stereo_fps'],3),round(r['ms_per_pair'],3),r['quality_flags'])
for tag,r in gpu.items():print(tag,{k:round(v['mean'],3) for k,v in r['metrics'].items()})
