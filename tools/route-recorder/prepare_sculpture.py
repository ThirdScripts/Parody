"""Offline vertex clustering of SMK KAS353 (public-domain STL)."""
import struct, sys, pathlib
data = pathlib.Path(sys.argv[1]).read_bytes()
count = struct.unpack_from('<I', data, 80)[0]
assert len(data) == 84 + count * 50
triangles = [struct.unpack_from('<9f', data, 84+i*50+12) for i in range(count)]
points = [(t[i],t[i+2],-t[i+1]) for t in triangles for i in (0,3,6)]
lo = [min(p[a] for p in points) for a in range(3)]
hi = [max(p[a] for p in points) for a in range(3)]
scale = max(hi[a]-lo[a] for a in range(3))
cells = {}; vertices = []; counts = []; indices = []
for p in points:
    p = tuple((p[a]-(lo[a]+hi[a])/2)/scale*2 for a in range(3))
    key = tuple(round(v/.014) for v in p)
    if key not in cells:
        cells[key] = len(vertices); vertices.append([0.,0.,0.]); counts.append(0)
    idx = cells[key]; indices.append(idx); counts[idx] += 1
    for a in range(3): vertices[idx][a] += p[a]
faces = [indices[i:i+3] for i in range(0,len(indices),3) if len(set(indices[i:i+3]))==3]
with open(sys.argv[2], 'wb') as out:
    out.write(struct.pack('<II',len(vertices),len(faces)))
    for i,p in enumerate(vertices): out.write(struct.pack('<3f',*(v/counts[i] for v in p)))
    for f in faces: out.write(struct.pack('<3I',*f))
print(f'{count} -> {len(faces)} triangles; {len(vertices)} vertices')
