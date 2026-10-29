"""Summarize diagnostic command-list GPU timings, not total frame GPU time."""
import argparse,csv,json,statistics
from collections import defaultdict
p=argparse.ArgumentParser();p.add_argument('csv');a=p.parse_args()
frames=defaultdict(lambda:[0.,0.,0]);times=[]
with open(a.csv,newline='') as f:
 for row in csv.DictReader(f):
  t=float(row['elapsed_seconds'])
  if t<3:continue # exclude query pool creation and startup
  times.append(t);v=frames[int(row['present_index'])]
  v[0]+=float(row['command_list_gpu_ms']);v[1]+=float(row['execute_cpu_ms']);v[2]+=1
ids=sorted(frames)
if len(ids)<3:raise SystemExit('Insufficient frames')
# Boundary frames are incomplete when a timed window starts/ends mid-frame.
values=[frames[i] for i in ids[1:-1]]
def summary(xs):
 xs=sorted(xs);return {'mean':statistics.mean(xs),'median':statistics.median(xs),'p95':xs[int((len(xs)-1)*.95)]}
print(json.dumps({'scope':'summed ExecuteCommandList spans; excludes immediate work and inter-list gaps',
 'complete_present_bins':len(values),'observed_present_rate':(ids[-1]-ids[0])/(max(times)-min(times)),
 'gpu_list_ms_per_present':summary([v[0] for v in values]),'cpu_execute_ms_per_present':summary([v[1] for v in values]),
 'lists_per_present':summary([v[2] for v in values])},indent=2))
