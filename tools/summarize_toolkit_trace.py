import xml.etree.ElementTree as E,collections,json,sys
p=sys.argv[1];ns={'e':'http://schemas.microsoft.com/win/2004/08/events/event'};counts=collections.Counter();candidates=collections.Counter();enabled=collections.Counter();last={};examples={}
for _,el in E.iterparse(p,events=('end',)):
 if el.tag!='{'+ns['e']+'}Event':continue
 task=el.findtext('e:RenderingInfo/e:Task',namespaces=ns) or el.findtext('e:System/e:Task',namespaces=ns);ex=el.find('e:System/e:Execution',ns);tid=ex.get('ThreadID') if ex is not None else ''
 vals={d.get('Name'):d.text for d in el.findall('e:EventData/e:Data',ns)};counts[task]+=1
 if task=='IsVariableRateShadingCandidate':
  dims=tuple(vals.get(k) for k in ('Width','Height','ArraySize','Format'));candidates[dims]+=1;last[tid]=dims
 if task=='EnableVariableRateShading':enabled[last.get(tid,('unknown',))]+=1
 if task not in examples:examples[task]=vals
 el.clear()
r={'tasks':dict(counts),'candidates':{str(k):v for k,v in candidates.items()},'enabled_after_candidate':{str(k):v for k,v in enabled.items()},'examples':{k:v for k,v in examples.items() if 'Shading' in str(k) or 'RenderTarget' in str(k)}}
open(p+'.summary.json','w').write(json.dumps(r,indent=2));print(json.dumps({k:v for k,v in r.items() if k!='tasks'},indent=2))
