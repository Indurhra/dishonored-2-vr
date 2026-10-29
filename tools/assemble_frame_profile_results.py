"""Assemble the investigation's selected evidence; raw captures stay local."""
from pathlib import Path
import json,csv,statistics,hashlib
root=Path('local/frame-profile');game=Path('Q:/SteamLibrary/steamapps/common/Dishonored2')
bench=json.loads((root/'benchmark-results.json').read_text())
selected={k:v for k,v in bench.items() if k.startswith(('final_','identity_','motion_','candidate_','deployed_','scan_','movement_long_','toolkit_','release_'))}
profiles=json.loads((root/'profile-analysis.json').read_text())
memory={}
for tag in ('final_cpu_native','final_cpu_copy','candidate_cpu','scan_cpu_off','scan_cpu_on'):
 p=game/f'DVR_cpu_{tag}.csv.memory.csv'
 if not p.exists():continue
 rows=[r for r in csv.DictReader(p.open()) if float(r['seconds'])>=2]
 memory[tag]={}
 for k in ('private_bytes','working_bytes','local_video_bytes','local_video_budget'):
  valid='video_valid' if k.startswith('local_') else 'process_valid'
  vals=[int(r[k]) for r in rows if r[valid]=='1']
  if vals:memory[tag][k]={'samples':len(vals),'mean':statistics.mean(vals),'min':min(vals),'max':max(vals)}
out={'candidate_sha256':hashlib.sha256(Path('build-q/bin/DishonoredVR.dll').read_bytes()).hexdigest().upper(),
 'scope':'Simulator application performance; not physical-headset acceptance',
 'unavailable':['physical headset drops/reprojection','gaze tracking','motion-to-photon latency','validated live DLSS GPU stage time'],
 'gpu_stage_validity':{'0':'unvalidated in hooked live context','2':'unvalidated in hooked live context','3':'elapsed bridge, overlaps 4/5','4':'independent D3D12 queue timestamp','5':'independent D3D12 queue timestamp'},
 'benchmarks':selected,'run_metadata':{k:json.loads((root/f'{k}.jsonl.metadata.json').read_text(encoding='utf-8-sig')) for k in selected if (root/f'{k}.jsonl.metadata.json').exists()},'memory':memory,'cpu_and_stage_summaries':profiles,
 'scan_experiment':json.loads((root/'scan-summary.json').read_text()),
 'presentmon':json.loads((root/'presentmon-summary.json').read_text()),
 'toolkit':{k:json.loads((root/f'toolkit-active-{k}.xml.summary.json').read_text()) for k in ('native','afw')}}
Path('docs/results/frame-profile-2026-10-28.json').write_text(json.dumps(out,indent=2))
for k,v in selected.items():
 if k.startswith(('candidate_','scan_','deployed_','movement_long_')):print(k,round(v['stereo_fps'],3),round(v['ms_per_pair'],3))
print('memory',json.dumps(memory,indent=2))
