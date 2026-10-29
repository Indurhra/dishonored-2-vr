import json,re,datetime,statistics,csv,collections
from pathlib import Path
root=Path('local/frame-profile');result={}
patterns={'presents':r'^frames (\d+)','native':r'\nxr_native_pairs_accepted (\d+)','fresh':r'afw requested \d+ fresh (\d+)','rebuilt':r'afw requested \d+ fresh \d+ rebuilt (\d+)','accepted':r'afw requested \d+ fresh \d+ rebuilt \d+ accepted (\d+)','refused':r'afw requested \d+ fresh \d+ rebuilt \d+ accepted \d+ refused (\d+)','taa':r'shaders \d+ dispatches (\d+)','held':r'stereosync on \d+ lefts \d+ rights \d+ held (\d+)'}
for p in root.glob('*.jsonl'):
 if p.stem in ('native_coarse','afw_coarse'):continue
 rows=[json.loads(l) for l in p.read_text(encoding='utf-8-sig').splitlines()];rows=[r for r in rows if r.get('text')]
 if not rows:continue
 for r in rows:r['time']=datetime.datetime.fromisoformat(r.get('heartbeat_utc',r['utc'])).timestamp()
 rows=[r for r in rows if r['time']>=rows[0]['time']+3]
 if len(rows)<3:continue
 a,b=rows[0],rows[-1];duration=b['time']-a['time'];delta={k:int(re.search(pat,b['text'])[1])-int(re.search(pat,a['text'])[1]) for k,pat in patterns.items()};fps=delta['accepted' if a['mode'] else 'native']/duration
 flags=[]
 if a['mode']==0 and abs(delta['presents']-2*delta['native'])>2:flags.append('extra Presents: mixed/transition native interval; exclude from clean pair comparisons')
 if a['mode'] and not (delta['accepted']==delta['fresh']==delta['rebuilt']==delta['taa']==delta['presents'] and delta['refused']==0 and delta['held']==0):flags.append('AFW counters do not match a clean one-render-per-pair interval')
 result[p.stem]={'quality_flags':flags,'seconds':duration,'stereo_fps':fps,'ms_per_pair':1000/fps if fps else None,'delta':delta,'private_bytes_mean':statistics.mean(r['private_memory'] for r in rows),'movement':a['movement']}
root.joinpath('benchmark-results.json').write_text(json.dumps(result,indent=2))
for k,r in result.items():print(k,round(r['stereo_fps'],3),round(r['ms_per_pair'],3) if r['ms_per_pair'] else None,r['delta'])
