import os, sys, json, shutil, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_mc_server as R
MASK=(1<<48)-1
def find_salt(seed,cx,cz,spacing):
    i=cx//spacing; j=cz//spacing
    wx=cx-i*spacing; wz=cz-j*spacing
    shift=31-(spacing.bit_length()-1)
    base=i*341873128712+j*132897987541+seed
    for salt in range(1,1<<31):
        s=((base+salt)^0x5DEECE66D)&MASK
        s=(s*0x5DEECE66D+0xB)&MASK
        if ((s>>17)>>shift)!=wx: continue
        s=(s*0x5DEECE66D+0xB)&MASK
        if ((s>>17)>>shift)==wz: return salt
spacing=int(sys.argv[1])
world=os.path.join(R.BASE,'world_portal')
shutil.rmtree(world,ignore_errors=True); os.makedirs(world)
with zipfile.ZipFile('mc_test_noise.zip') as z: z.extractall(world)
salt=find_salt(777,-10,10,spacing)
p=os.path.join(world,'datapacks','tmc_lairs','data','tmc','worldgen','structure_set','ruined_portal_here_0.json')
open(p,'w').write(json.dumps({"placement":{"type":"minecraft:random_spread","salt":salt,"separation":0,"spacing":spacing},"structures":[{"structure":"minecraft:ruined_portal","weight":1}]}))
R.prep()
log=R.run('world_portal',['execute positioned 500 70 500 run locate structure minecraft:ruined_portal','execute positioned 20 70 -20 run locate structure minecraft:ruined_portal'])
for l in R.tail(log,['nearest','could not']): print(l)
