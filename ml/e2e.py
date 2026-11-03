"""End-to-end recall under the firmware gate: a take counts only if it triggers, is classified right and clears conf_min.
Leave-one-subject-out; also reports false triggers per minute on the background takes. Usage: python e2e.py ../data"""
import sys,json,os
from pathlib import Path
os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL","2"); os.environ.setdefault("TF_USE_LEGACY_KERAS","1")
sys.path.insert(0, str(Path(__file__).resolve().parent))
import numpy as np
from slapml import Config,load_takes,to_tflite,tflite_predict,trigger_points,cut
from train import train_once
D=Path(sys.argv[1]); out={}
def run(task,ath,gth,conf_min,label):
    cfg=Config(task,acc_th=ath,gyro_th=gth)
    takes=load_takes(D,cfg)
    subs=sorted({t.subject for t in takes})
    tot={c:[0,0,0] for c in cfg.classes[1:]}   # takes, triggered, correct+confident
    fp=0; bgtrig=0; mins=0; folds=[]
    for test in subs:
        rest=[s for s in subs if s!=test]
        rng=np.random.default_rng(cfg.seed)
        m,(Xtr,_),_,mean,std,_=train_once(takes,cfg,rest[:-1],rest[-1:],[test],rng)
        lite=to_tflite(m,Xtr)
        f={c:[0,0,0] for c in cfg.classes[1:]}; ffp=0; fm=0
        for t in takes:
            if t.subject!=test: continue
            name=Path(t.path if hasattr(t,'path') else t.src).parent.name
            pts=trigger_points(t.x,cfg)
            if name in cfg.classes[1:]:
                f[name][0]+=1
                if pts:
                    f[name][1]+=1
                    p=tflite_predict(lite,((cut(t.x,pts[0],cfg)-mean)/std)[None])[0]
                    if int(p.argmax())==cfg.classes.index(name) and p.max()*255>=conf_min: f[name][2]+=1
            elif name=='background':
                fm+=len(t.x)/cfg.hz/60
                if pts:
                    W=np.stack([(cut(t.x,i,cfg)-mean)/std for i in pts]); P=tflite_predict(lite,W)
                    bgtrig+=len(pts)
                    ffp+=int(((P.argmax(1)!=0)&(P.max(1)*255>=conf_min)).sum())
        for c in f:
            for i in range(3): tot[c][i]+=f[c][i]
        fp+=ffp; mins+=fm; folds.append(dict(subject=test,fp_per_min=round(ffp/fm,2),by={c:f[c] for c in f}))
        print(label,test,f,'fp/min',round(ffp/fm,2),flush=True)
    r=dict(task=task,acc_th=ath,gyro_th=gth,conf_min=conf_min,classes=tot,recall={c:round(tot[c][2]/tot[c][0],4) for c in tot},trig={c:round(tot[c][1]/tot[c][0],4) for c in tot},fp=fp,bg_trig=bgtrig,minutes=round(mins,1),fp_per_min=round(fp/mins,3),folds=folds)
    print(label,json.dumps({k:r[k] for k in('recall','trig','fp','minutes','fp_per_min')}),flush=True); out[label]=r
    json.dump(out,open(sys.argv[2],'w'),indent=1)
run('attacker',2.5,600.,180,'atk_default')
run('attacker',1.8,350.,180,'atk_low')
run('defender',1.6,250.,170,'def_default')
