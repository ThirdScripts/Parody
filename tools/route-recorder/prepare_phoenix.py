"""Convert supplied GLB default pose into a textured native-WPF scene. Requires numpy."""
import json, struct, pathlib, sys
import numpy as np
src=pathlib.Path(sys.argv[1]); out=pathlib.Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
data=src.read_bytes(); magic,version,total=struct.unpack_from('<III',data);assert magic==0x46546c67 and version==2 and total==len(data)
n,_=struct.unpack_from('<II',data,12);j=json.loads(data[20:20+n]);bn,bt=struct.unpack_from('<II',data,20+n);buf=data[28+n:28+n+bn]
def acc(i):
 a=j['accessors'][i];v=j['bufferViews'][a['bufferView']];dtype={5126:'<f4',5125:'<u4',5123:'<u2',5121:'u1'}[a['componentType']];cols={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']];size=np.dtype(dtype).itemsize
 return np.ndarray((a['count'],cols),dtype=dtype,buffer=buf,offset=v.get('byteOffset',0)+a.get('byteOffset',0),strides=(v.get('byteStride',cols*size),size)).copy()
world={}
def walk(i,parent):
 node=j['nodes'][i]
 if 'matrix' in node:m=np.array(node['matrix']).reshape(4,4).T
 else:
  x,y,z,w=node.get('rotation',[0,0,0,1]);m=np.eye(4);m[:3,:3]=np.array([[1-2*y*y-2*z*z,2*x*y-2*z*w,2*x*z+2*y*w],[2*x*y+2*z*w,1-2*x*x-2*z*z,2*y*z-2*x*w],[2*x*z-2*y*w,2*y*z+2*x*w,1-2*x*x-2*y*y]])@np.diag(node.get('scale',[1,1,1]));m[:3,3]=node.get('translation',[0,0,0])
 world[i]=parent@m
 for child in node.get('children',[]):walk(child,world[i])
for root in j['scenes'][j.get('scene',0)]['nodes']:walk(root,np.eye(4))
parts=[]
for i,node in enumerate(j['nodes']):
 if 'mesh' not in node or i not in world:continue
 for p in j['meshes'][node['mesh']]['primitives']:
  assert p.get('mode',4)==4
  a=p['attributes'];pos=acc(a['POSITION']);norm=acc(a['NORMAL']);uv=acc(a['TEXCOORD_0']);h=np.column_stack([pos,np.ones(len(pos))])
  if 'skin' in node:
   skin=j['skins'][node['skin']];inv=acc(skin['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1);mats=np.array([world[v]@inv[k] for k,v in enumerate(skin['joints'])]);weights=acc(a['WEIGHTS_0']);joints=acc(a['JOINTS_0']).astype(int);blend=(mats[joints]*weights[:,:,None,None]).sum(axis=1);pos=np.einsum('nij,nj->ni',blend,h)[:,:3];norm=np.einsum('nij,nj->ni',blend[:,:3,:3],norm)
  else:pos=(h@world[i].T)[:,:3];norm=norm@np.linalg.inv(world[i][:3,:3])
  norm/=np.maximum(np.linalg.norm(norm,axis=1)[:,None],1e-12)
  material=j['materials'][p['material']];tex=material.get('pbrMetallicRoughness',{}).get('baseColorTexture');image=j['images'][j['textures'][tex['index']]['source']] if tex else None
  name=''
  if image:
   view=j['bufferViews'][image['bufferView']];name='texture-'+str(p['material'])+('.png' if image['mimeType']=='image/png' else '.jpg');start=view.get('byteOffset',0);(out/name).write_bytes(buf[start:start+view['byteLength']])
  parts.append((pos,norm,uv,acc(p['indices']).flatten(),name))
allpos=np.concatenate([p[0] for p in parts]);lo=allpos.min(axis=0);hi=allpos.max(axis=0);center=(hi+lo)/2;scale=2/(hi-lo).max()
with (out/'phoenix.scene').open('wb') as f:
 f.write(struct.pack('<I',len(parts)))
 for pos,norm,uv,idx,name in parts:
  name=name.encode();f.write(struct.pack('<I',len(name)));f.write(name);f.write(struct.pack('<II',len(pos),len(idx)));f.write(np.column_stack([(pos-center)*scale,norm,uv]).astype('<f4').tobytes());f.write(idx.astype('<u4').tobytes())
print('parts',len(parts),'triangles',sum(len(p[3])//3 for p in parts),'bounds',lo,hi)
