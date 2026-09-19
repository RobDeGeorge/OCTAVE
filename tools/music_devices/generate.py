#!/usr/bin/env python3
"""Build original, reference-inspired glTF players. Dev dependencies: numpy, Pillow.
All geometry/materials/textures are generated here; no downloaded assets are bundled.
Face is +Z, Y is up; dimensions are display units, not manufacturing dimensions.

Geometry is budgeted for the on-screen size, not for close-ups: curves get only
as many segments as hide the chord error at about two screen pixels per unit,
sub-pixel bevels and corner radii collapse to plain boxes, solid cylinders skip
their degenerate inner wall, and every printed label in an asset shares one
texture atlas so the whole set of decals costs a single blended draw call.
"""
import io
import copy
import json
import math
import struct
import os
import colorsys
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).resolve().parents[2] / 'frontend/assets/music_devices'
FONT = os.environ.get('OCTAVE_MODEL_FONT', '/usr/share/fonts/gnu-free/FreeSans.otf')
BOLD = os.environ.get('OCTAVE_MODEL_FONT_BOLD', '/usr/share/fonts/gnu-free/FreeSansBold.otf')
# Upper bound on how large a unit gets on screen (immersive editor on a hi-dpi
# desktop). The media row on a 1024x600 head unit is under one pixel per unit.
PIXELS_PER_UNIT = 2.0
ATLAS_PADDING = 4


def curve_segments(radius, limit=None):
    """Segments needed so the chord error of a circle stays under 0.2 px."""
    pixels = max(radius * PIXELS_PER_UNIT, .5)
    n = math.pi / math.acos(max(-1.0, 1 - .2 / pixels)) if pixels > .2 else 8
    n = int(min(96, max(8, 4 * math.ceil(n / 4))))
    return min(n, limit) if limit else n


class Asset:
    def __init__(self):
        self.doc = {'asset': {'version': '2.0', 'generator': 'OCTAVE music device workshop'},
                    'scene': 0, 'scenes': [{'nodes': []}], 'nodes': [], 'meshes': [],
                    'materials': [], 'bufferViews': [], 'accessors': []}
        self.blob = bytearray()
        self.triangles = 0

    def buffer(self, data, target=None):
        self.blob.extend(b'\0' * (-len(self.blob) % 4))
        v = {'buffer': 0, 'byteOffset': len(self.blob), 'byteLength': len(data)}
        if target: v['target'] = target
        self.doc['bufferViews'].append(v)
        self.blob.extend(data)
        return len(self.doc['bufferViews']) - 1

    def accessor(self, values, kind, dtype, component):
        a = np.array(values, dtype=dtype)
        v = self.buffer(a.tobytes(), 34963 if kind == 'SCALAR' else 34962)
        item = {'bufferView': v, 'componentType': component, 'count': len(a), 'type': kind}
        if kind == 'VEC3': item.update(min=a.min(axis=0).tolist(), max=a.max(axis=0).tolist())
        self.doc['accessors'].append(item)
        return len(self.doc['accessors']) - 1

    def image(self, picture, lossy=False):
        stream = io.BytesIO()
        if lossy: picture.convert('RGB').save(stream, format='JPEG', quality=88, optimize=True)
        else: picture.save(stream, format='PNG', optimize=True)
        images = self.doc.setdefault('images', [])
        images.append({'bufferView': self.buffer(stream.getvalue()), 'mimeType': 'image/jpeg' if lossy else 'image/png'})
        textures = self.doc.setdefault('textures', [])
        textures.append({'source': len(images) - 1})
        return len(textures) - 1

    def material(self, name, color, metal=0, rough=.4, texture=None, opaque=False, lossy=False):
        rgba = [int(color[i:i+2],16)/255 for i in (1,3,5)] + [1]
        pbr = {'baseColorFactor': rgba, 'metallicFactor': metal, 'roughnessFactor': rough}
        mat = {'name': name, 'pbrMetallicRoughness': pbr}
        if texture is not None:
            pbr['baseColorTexture'] = {'index': self.image(texture, lossy)}
            if not opaque:
                mat['alphaMode'] = 'BLEND'; mat['doubleSided'] = True
        self.doc['materials'].append(mat)
        return len(self.doc['materials'])-1

    def mesh(self,name,v,f,mat,normals=None,uv=None):
        v=np.array(v,dtype=float); f=np.array(f,dtype=np.uint32)
        if normals is None:
            # Flat faces by default; curved primitives supply analytic normals.
            v=v[f].reshape(-1,3)
            n=np.cross(v[1::3]-v[::3],v[2::3]-v[::3]); n/=np.maximum(np.linalg.norm(n,axis=1)[:,None],1e-12)
            normals=np.repeat(n,3,axis=0); f=np.arange(len(v)).reshape(-1,3)
        attr={'POSITION':self.accessor(v,'VEC3','<f4',5126),'NORMAL':self.accessor(normals,'VEC3','<f4',5126)}
        if uv is not None: attr['TEXCOORD_0']=self.accessor(uv,'VEC2','<f4',5126)
        # 16-bit indices halve the index buffer; every part here has far fewer than 65k vertices.
        wide=len(v)>65535
        self.doc['meshes'].append({'name':name,'primitives':[{'attributes':attr,'indices':self.accessor(f.ravel(),'SCALAR','<u4' if wide else '<u2',5125 if wide else 5123),'material':mat}]})
        self.doc['nodes'].append({'name':name,'mesh':len(self.doc['meshes'])-1})
        self.doc['scenes'][0]['nodes'].append(len(self.doc['nodes'])-1)
        self.triangles+=len(f)

    def box(self,name,pos,size,mat,r=1.5,bevel=.6):
        w,h,d=size; r=min(r,w/2-.001,h/2-.001); bevel=min(bevel,d/2-.001,r)
        # Corner rounding and edge bevels below a screen pixel collapse to sharp geometry.
        k=9 if r>=6 else 5 if r>=2 else 3 if r>=.6 else 1
        if bevel*PIXELS_PER_UNIT<.5: bevel=0
        n=4*k; rings=[]
        for inset,z in [(bevel,-d/2),(0,-d/2+bevel),(0,d/2-bevel),(bevel,d/2)]:
            for cx,cy,start in [(w/2-r,h/2-r,0),(-w/2+r,h/2-r,90),(-w/2+r,-h/2+r,180),(w/2-r,-h/2+r,270)]:
                for j in range(k):
                    if k==1: a=math.radians(start+45); radius=(r-inset)*math.sqrt(2)
                    else: a=math.radians(start+j*90/(k-1)); radius=r-inset
                    rings.append([cx+radius*math.cos(a),cy+radius*math.sin(a),z])
        f=[]
        for strip in ((1,) if bevel==0 else (0,1,2)):
            for i in range(n):
                a=strip*n+i;b=strip*n+(i+1)%n;c=b+n;d0=a+n;f.extend([(a,b,c),(a,c,d0)])
        rings.extend([[0,0,-d/2],[0,0,d/2]])
        for i in range(n): f.extend([(4*n,(i+1)%n,i),(4*n+1,3*n+i,3*n+(i+1)%n)])
        v=np.array(rings); normals=[]
        for ring in range(4):
            for corner in range(4):
                for j in range(k):
                    angle=math.radians(corner*90+(45 if k==1 else j*90/(k-1)))
                    normals.append([0,0,-1] if ring==0 else [0,0,1] if ring==3 else [math.cos(angle),math.sin(angle),0])
        normals.extend([[0,0,-1],[0,0,1]])
        self.mesh(name,v+pos,f,mat,normals)

    def ring(self,name,pos,outer,inner,depth,mat,segments=None):
        segments=curve_segments(outer,segments)
        pos=np.array(pos,dtype=float); v=[];n=[];f=[]
        circle=[(math.cos(2*math.pi*i/segments),math.sin(2*math.pi*i/segments)) for i in range(segments+1)]
        if inner<=0:
            # Solid cylinder: fan caps from a single center, no inner wall.
            for z,normal,flip in ((depth/2,[0,0,1],False),(-depth/2,[0,0,-1],True)):
                c=len(v); v.append(pos+[0,0,z]); n.append(normal)
                for cx,cy in circle: v.append(pos+[outer*cx,outer*cy,z]); n.append(normal)
                for i in range(segments): f.append((c,c+2+i,c+1+i) if flip else (c,c+1+i,c+2+i))
            walls=[2]
        else: walls=[0,1,2,3]
        # Independent strips for correct hard normals at cap edges.
        for face in walls:
            start=len(v)
            for cx,cy in circle:
                if face==0: pairs=[(inner,depth/2),(outer,depth/2)]; normal=[0,0,1]
                elif face==1: pairs=[(outer,-depth/2),(inner,-depth/2)]; normal=[0,0,-1]
                elif face==2: pairs=[(outer,depth/2),(outer,-depth/2)]; normal=[cx,cy,0]
                else: pairs=[(inner,-depth/2),(inner,depth/2)]; normal=[-cx,-cy,0]
                for radius,z in pairs: v.append(pos+[radius*cx,radius*cy,z]); n.append(normal)
            for i in range(segments):
                a=start+2*i;f.extend([(a,a+1,a+3),(a,a+3,a+2)])
        self.mesh(name,v,f,mat,n)

    def disc(self,name,pos,r,mat,normal=(0,0,1),segments=6):
        """Flat polygon facing `normal`; the cheapest possible rivet, dot or lamp."""
        normal=np.array(normal,dtype=float);normal/=np.linalg.norm(normal)
        u=np.cross(normal,[0,0,1] if abs(normal[2])<.9 else [1,0,0]);u/=np.linalg.norm(u);w=np.cross(normal,u)
        pos=np.array(pos,dtype=float)
        v=[pos];n=[normal]
        for i in range(segments):
            t=2*math.pi*i/segments;v.append(pos+r*(u*math.cos(t)+w*math.sin(t)));n.append(normal)
        f=[(0,1+i,1+(i+1)%segments) for i in range(segments)]
        self.mesh(name,v,f,mat,n)

    def tube(self,name,points,r,mat):
        segments=curve_segments(r,24)
        for j,(a,b) in enumerate(zip(points,points[1:],strict=False)):
            a=np.array(a);b=np.array(b);axis=b-a;axis=axis/np.linalg.norm(axis)
            u=np.cross(axis,[0,0,1] if abs(axis[2])<.9 else [0,1,0]);u/=np.linalg.norm(u);w=np.cross(axis,u)
            v=[];n=[];f=[]
            for p in (a,b):
                for i in range(segments+1):
                    t=i*2*math.pi/segments;normal=u*math.cos(t)+w*math.sin(t);v.append(p+r*normal);n.append(normal)
            for i in range(segments):f.extend([(i,i+1,i+segments+2),(i,i+segments+2,i+segments+1)])
            self.mesh(name+str(j),v,f,mat,n)

    def label(self,text,pos,width,color='#202730',height=None,bold=False):
        font=ImageFont.truetype(BOLD if bold else FONT,42)
        bounds=font.getbbox(text); iw=bounds[2]+12; ih=64
        im=Image.new('RGBA',(iw,ih));ImageDraw.Draw(im).text((6,4-bounds[1]),text,font=font,fill=color)
        mat=self.material(text,'#ffffff',rough=.7,texture=im)
        self.doc['materials'][mat]['extras']={'label':True}
        height=height or width*ih/iw
        x,y,z=pos;w=width/2;h=height/2
        self.mesh(text,[(x-w,y-h,z),(x+w,y-h,z),(x+w,y+h,z),(x-w,y+h,z)],[(0,1,2),(0,2,3)],mat,[[0,0,1]]*4,[(0,1),(1,1),(1,0),(0,0)])

    def extract(self, start, name, pivot):
        """Export a movable assembly in pivot-local coordinates and remove it from the body."""
        part=Asset();part.doc=copy.deepcopy(self.doc);part.blob=bytearray(self.blob)
        part.doc['nodes']=part.doc['nodes'][start:]
        for node in part.doc['nodes']:
            node['translation']=(np.array(node.get('translation',[0,0,0]))-pivot).tolist()
        part.save(name)
        del self.doc['nodes'][start:]

    def consolidate(self):
        """Batch static details by material so screws/grooves don't cost draw calls,
        merge every printed label into one atlas texture, and drop the materials,
        textures and images that this asset no longer references."""
        def array(index):
            accessor=self.doc['accessors'][index];view=self.doc['bufferViews'][accessor['bufferView']]
            width={'VEC3':3,'VEC2':2,'SCALAR':1}[accessor['type']]
            dtype={5125:'<u4',5123:'<u2'}.get(accessor['componentType'],'<f4')
            return np.frombuffer(self.blob,dtype=dtype,count=accessor['count']*width,offset=view['byteOffset']).reshape(-1,width).copy()
        def picture(material):
            texture=self.doc['textures'][self.doc['materials'][material]['pbrMetallicRoughness']['baseColorTexture']['index']]
            image=self.doc['images'][texture['source']];view=self.doc['bufferViews'][image['bufferView']]
            return bytes(self.blob[view['byteOffset']:view['byteOffset']+view['byteLength']]),image['mimeType']
        used=[self.doc['meshes'][node['mesh']]['primitives'][0]['material'] for node in self.doc['nodes']]
        labels=sorted({m for m in used if self.doc['materials'][m].get('extras',{}).get('label')})
        placement={};atlas=None
        if labels:
            sheets=[Image.open(io.BytesIO(picture(m)[0])).convert('RGBA') for m in labels]
            width=max(im.width for im in sheets);height=sum(im.height+ATLAS_PADDING for im in sheets)-ATLAS_PADDING
            canvas=Image.new('RGBA',(width,height));y=0
            for m,im in zip(labels,sheets,strict=True):
                canvas.paste(im,(0,y));placement[m]=(im.width/width,im.height/height,y/height);y+=im.height+ATLAS_PADDING
            atlas=copy.deepcopy(self.doc['materials'][labels[0]]);atlas['name']='labels';atlas.pop('extras',None)
            stream=io.BytesIO();canvas.save(stream,format='PNG',optimize=True)
            pictures={}
        groups={}
        for node in self.doc['nodes']:
            primitive=self.doc['meshes'][node['mesh']]['primitives'][0];attr=primitive['attributes']
            v=array(attr['POSITION']);n=array(attr['NORMAL']);f=array(primitive['indices']).ravel()
            if 'rotation' in node or 'translation' in node:
                x,y,z,w=node.get('rotation',[0,0,0,1])
                rotation=np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
                                   [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
                                   [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
                v=v@rotation.T+node.get('translation',[0,0,0]);n=n@rotation.T
            material=primitive['material'];uv=array(attr['TEXCOORD_0']) if 'TEXCOORD_0' in attr else None
            if material in placement:
                su,sv,oy=placement[material];uv=uv*[su,sv]+[0,oy];material='labels'
            key=(material,uv is not None)
            group=groups.setdefault(key,{'v':[],'n':[],'f':[],'uv':[],'count':0})
            group['v'].append(v);group['n'].append(n);group['f'].append(f+group['count']);group['count']+=len(v)
            if uv is not None:group['uv'].append(uv)
        # Rebuild the document with only what the batched meshes reference.
        materials=[];pictures={};remap={}
        for material,_ in groups:
            if material in remap:continue
            remap[material]=len(materials)
            if material=='labels':materials.append(atlas);pictures[len(materials)-1]=(stream.getvalue(),'image/png')
            else:
                entry=copy.deepcopy(self.doc['materials'][material]);entry.pop('extras',None)
                if 'baseColorTexture' in entry['pbrMetallicRoughness']:pictures[len(materials)]=picture(material)
                materials.append(entry)
        self.blob=bytearray();self.triangles=0
        for key in ('nodes','meshes','bufferViews','accessors'):self.doc[key]=[]
        self.doc['scenes'][0]['nodes']=[];self.doc['materials']=materials
        self.doc.pop('images',None);self.doc.pop('textures',None)
        for index,(data,mime) in pictures.items():
            images=self.doc.setdefault('images',[]);images.append({'bufferView':self.buffer(data),'mimeType':mime})
            textures=self.doc.setdefault('textures',[]);textures.append({'source':len(images)-1})
            materials[index]['pbrMetallicRoughness']['baseColorTexture']={'index':len(textures)-1}
        for (material,has_uv),group in groups.items():
            self.mesh(materials[remap[material]]['name'],np.concatenate(group['v']),np.concatenate(group['f']).reshape(-1,3),remap[material],
                      np.concatenate(group['n']),np.concatenate(group['uv']) if has_uv else None)

    def save(self,name):
        self.consolidate()
        self.doc['buffers']=[{'byteLength':len(self.blob)}]
        meta=json.dumps(self.doc,separators=(',',':')).encode();meta+=b' '*(-len(meta)%4)
        self.blob+=b'\0'*(-len(self.blob)%4)
        payload=struct.pack('<III',0x46546c67,2,28+len(meta)+len(self.blob))+struct.pack('<I4s',len(meta),b'JSON')+meta+struct.pack('<I4s',len(self.blob),b'BIN\0')+self.blob
        (OUT/(name+'.glb')).write_bytes(payload)
        print(f'{name}: {self.triangles:,} triangles, {len(self.doc["meshes"])} draw calls, {len(payload)//1024} KiB')


def palette(a):
    return {k:a.material(k,*v) for k,v in {
        'silver':('#bac4ce',.8,.28),'chrome':('#e3e7ee',.95,.17),'dark':('#171c23',.25,.34),
        'rubber':('#080b10',0,.8),'blue':('#214b7a',.65,.32),'white':('#e9e6de',.05,.28),
        'orange':('#f78428',.05,.35),'ink':('#30343b',.1,.4),'glass':('#101e28',.35,.12),
        'cyan':('#42d4e8',.25,.23),'tape':('#31251f',.15,.65)}.items()}


def screws(a,m,points):
    for x,y,z in points:
        a.ring('recessed fastener',(x,y,z),1.8,0,.6,m['chrome'])
        a.box('screw slot',(x,y,z+.35),(2.1,.35,.15),m['ink'],.1,.03)


def turntable():
    a=Asset();m=palette(a)
    a.box('rubber isolation plinth',(0,0,-9),(320,250,28),m['rubber'],9,3)
    a.box('cast alloy top plate',(0,0,7),(320,250,14),m['silver'],7,2)
    for x in (-125,125):
        for y in (-95,95):a.ring('damped foot',(x,y,-29),19,0,16,m['rubber'])
    a.ring('platter polished rim',(-35,8,20),113,0,13,m['chrome'])
    a.ring('rubber platter mat',(-35,8,26.8),109,2,1.2,m['rubber'])
    a.ring('fixed spindle',(-35,8,32),2,0,8,m['chrome'])
    a.ring('strobe band',(-35,8,21),113.3,110.5,7,m['dark'])
    # Strobe dots sit on the outside of the band where they are actually visible,
    # as flat hexagons rather than 200 tiny cylinders.
    for row in range(2):
        for i in range(100):
            t=i*2*math.pi/100+row*.018;c=math.cos(t);s=math.sin(t)
            a.disc('strobe dot',(-35+113.35*c,8+113.35*s,19.6+row*2.6),.8,m['chrome'],(c,s,0))
    a.ring('tonearm base',(103,80,22),27,0,14,m['dark'])
    a.ring('height adjustment',(103,80,31),23,18,7,m['chrome'])
    a.ring('gimbal bearing',(103,80,36),12,0,13,m['silver'])
    a.ring('tonearm bearing shaft',(103,80,44),4,0,14,m['chrome'])
    arm_start=len(a.doc['nodes'])
    a.tube('S tonearm',[(103,103,44),(103,80,44),(108,55,43),(111,30,42),(107,4,40),(91,-23,38),(76,-49,36)],3,m['chrome'])
    a.box('headshell',(73,-58,34),(13,27,6),m['dark'],2,1)
    for x in (70,76):
        for y in (-50,-57):a.disc('headshell perforation',(x,y,37.05),1.2,m['rubber'])
    a.box('cartridge',(73,-64,28),(10,13,8),m['orange'],1,.5)
    a.tube('stylus',[(73,-65,24),(69,-67,26)],.5,m['chrome'])
    a.ring('counterweight',(103,108,45),11,0,18,m['chrome'])
    a.extract(arm_start,'tonearm',np.array([103,80,36]))
    a.box('pitch channel',(140,-34,15),(7,89,2),m['dark'],1,.3)
    for y in range(-73,12,7):a.box('pitch graduation',(132,y,15.1),(4,.55,.25),m['ink'],.1,.05)
    a.box('pitch fader',(140,-24,18),(15,9,6),m['chrome'],1,.5)
    a.box('fader center line',(140,-24,21.1),(12,.6,.2),m['ink'],.1,.05)
    a.box('start stop',(-133,-104,17),(26,18,5),m['chrome'],2,.7)
    a.label('START / STOP',(-133,-119,14.2),26)
    for x,t in [(-103,'33'),(-85,'45')]:
        a.box('speed switch',(x,-107,16),(14,7,3),m['dark'],1,.4);a.label(t,(x,-107,17.6),6,'#eeeeee')
    a.ring('power dial',(-139,-71,22),8,0,14,m['dark'])
    a.ring('strobe lamp',(-132,-79,17),2,0,3,m['dark'])
    a.label('OCTAVE',(87,-97,14.2),39,bold=True)
    a.label('DIRECT DRIVE / 33⅓',(92,-110,14.2),53)
    screws(a,m,[(-151,115,14.1),(151,115,14.1),(151,-115,14.1)])
    a.save('record-player')
    a=Asset();m=palette(a)
    a.ring('vinyl',(0,0,0),108,2,2,m['rubber'])
    # Concentric grooves in a generated texture avoid geometry aliasing. The
    # pitch is coarse enough to survive mip filtering at menu size; the disc
    # mesh already bounds the surface, so the texture is opaque JPEG.
    size=512
    yy,xx=np.mgrid[:size,:size];xx=(xx-size/2)/(size/2);yy=(yy-size/2)/(size/2)
    radius=np.sqrt(xx*xx+yy*yy);angle=np.arctan2(yy,xx)
    brightness=17+3*np.sin(radius*420)+4*np.cos(angle*2)
    for band in (.44,.58,.72,.86):brightness-=5*np.exp(-((radius-band)/.007)**2)
    pixels=np.zeros((size,size,3),dtype=np.uint8)
    for channel in range(3):pixels[:,:,channel]=np.clip(brightness+channel,0,255)
    mat=a.material('pressed vinyl grooves','#ffffff',.25,.36,Image.fromarray(pixels),opaque=True,lossy=True)
    segments=curve_segments(108)
    vertices=[[0,0,1.06]];uv=[[.5,.5]];faces=[]
    for i in range(segments+1):
        angle=i*2*math.pi/segments;c=math.cos(angle);t=math.sin(angle)
        vertices.append([108*c,108*t,1.06]);uv.append([.5+.5*c,.5-.5*t])
    for i in range(segments):faces.append((0,i+1,i+2))
    a.mesh('vinyl surface',vertices,faces,mat,[[0,0,1]]*len(vertices),uv)
    # The album cover is printed over the label at runtime; baked text underneath
    # would only compete with it in the transparent sort.
    label=a.material('ochre paper','#d69a49',0,.83)
    a.ring('record label',(0,0,1.2),33,2,.2,label)
    a.save('record')


def cassette():
    a=Asset();m=palette(a)
    a.box('metal chassis',(0,0,0),(144,224,39),m['silver'],5,1.8)
    a.box('cassette chamber',(-10,0,20),(123,221,2),m['rubber'],4,.4)
    # Connected chamber walls bridge the chassis to the closed door. Leave the
    # center open for the removable tape and its visible reels.
    for center,size in [((-68,0),(4,216)),((48,0),(4,216)),((-10,107),(120,4)),((-10,-107),(120,4))]:
        a.box('chamber sidewall',(center[0],center[1],26),(size[0],size[1],13),m['dark'],1,.5)
    for x in (-50,30):
        a.box('hinge support',(x,-106,24),(12,8,16),m['silver'],1,.5)
    a.tube('door hinge barrel',[(-54,-107,28),(34,-107,28)],5,m['chrome'])
    # The removable cassette lives behind a door with a real reel window.
    tape_start=len(a.doc['nodes'])
    a.box('cassette shell',(-10,0,24),(104,176,6),m['ink'],5,1)
    a.box('cassette paper label',(-10,0,27.2),(94,158,.5),m['white'],4,.1)
    a.box('cassette window',(-19,-2,27.6),(40,118,.6),m['dark'],3,.1)
    # The album cover is printed over the tape label at runtime, so no baked text there.
    a.box('tape label',(-19,-2,28),(35,46,.5),m['white'],1,.1)
    a.label('OCTAVE / MIX',(-10,72,27.6),74,bold=True)
    a.label('SIDE A',(-10,-70,27.6),41)
    for y in (-39,35):
        a.ring('reel axle',(-19,y,28.5),2.8,0,4,m['dark'])
    screws(a,m,[(-53,77,27.45),(33,77,27.45),(-53,-77,27.45),(33,-77,27.45)])
    a.extract(tape_start,'cassette',np.array([0,0,0]))
    door_start=len(a.doc['nodes'])
    # Four rails leave a 42 × 126 opening at x=-19, y=-2.
    for center,size in [((-10,85),(121,49)),((-10,-87),(121,45)),((-55,-2),(30,126)),((26,-2),(48,126))]:
        a.box('blue window door',(center[0],center[1],33),(size[0],size[1],3),m['blue'],2,.6)
    a.label('OCTAVE',(-25,93,34.6),60,'#dfe6ee',bold=True)
    a.label('STEREO CASSETTE PLAYER',(-11,80,34.6),95,'#bbcbd9')
    a.label('STEREO',(-13,69,34.6),64,'#dfe6ee',height=12)
    a.label('↑',(22,-7,34.6),15,'#dfe6ee',height=31)
    a.label('WALK / 01',(-8,-91,34.6),83,'#dfe6ee',bold=True)
    a.label('AUTO STOP',(-10,-103,34.6),40,'#bacbdc')
    a.extract(door_start,'cassette-door',np.array([-10,-109,33]))
    a.box('hot line key',(14,114,5),(28,7,22),m['orange'],2,.8)
    for y in (60,29,-2,-33):
        a.box('transport side key',(74,y,0),(7,24,25),m['dark'],2,.6)
        for z in (-8,-4,0,4,8):a.box('key ribs',(77.4,y,z),(.6,20,.6),m['silver'],.1,.1)
    for y in (67,17):
        a.box('volume slot',(60,y,19.8),(4,27,1),m['dark'],1,.2)
        a.box('volume slider',(60,y-3,21.7),(10,7,4),m['ink'],1,.5)
    screws(a,m,[(61,99,19.65),(61,-97,19.65),(-64,-102,32.5)])
    a.save('cassette-player')
    a=Asset();m=palette(a)
    a.ring('ivory reel',(0,0,0),8.5,3,2,m['white'])
    a.ring('reel hub',(0,0,.2),3.7,1.9,2,m['chrome'])
    for i in range(6):
        t=i*math.pi/3;a.disc('reel aperture',(6*math.cos(t),6*math.sin(t),1.05),1.6,m['dark'],segments=8)
    a.save('reel')
    a=Asset();m=palette(a)
    a.ring('wound magnetic tape',(0,0,0),16,8,1.4,m['tape'])
    # A single visible winding line; finer layers vanish below a pixel at reel size.
    a.ring('tape layers',(0,0,.8),12,11.6,.12,m['ink'])
    a.save('tape-pack')


def handheld(ipod):
    a=Asset();m=palette(a)
    w,h=(144,238) if ipod else (136,209)
    a.box('polished rear shell',(0,0,-3),(w,h,23),m['chrome'] if ipod else m['dark'],15,4)
    a.box('case seam',(0,0,6),(w+.4,h+.4,2),m['rubber'],15,.5)
    a.box('front shell',(0,0,10),(w,h,8),m['silver'] if ipod else m['dark'],15,2.5)
    sy=49 if ipod else 48; sw=115 if ipod else 101; sh=87 if ipod else 56
    a.box('screen surround',(0,sy,14),(sw+5,sh+5,2),m['ink'],5,.6)
    a.box('screen glass',(0,sy,15),(sw,sh,1),m['glass'],3,.3)
    cy=-58 if ipod else -48; radius=46 if ipod else 44
    a.ring('wheel seam',(0,cy,14.5),radius+1.5,0,1,m['ink'])
    a.ring('click wheel',(0,cy,15.1),radius,0,1,m['white'] if ipod else m['ink'])
    if not ipod:a.ring('blue control halo',(0,cy,15.7),radius,radius-2,.5,m['cyan'])
    a.ring('center button seam',(0,cy,16),18,0,1,m['dark'])
    a.ring('center button',(0,cy,16.8),16.7,0,1.5,m['silver'] if ipod else m['rubber'])
    ink='#757a80' if ipod else '#dde5ee'
    for text,x,y,width in [('MENU' if ipod else '▷Ⅱ',0,cy+31,20),('Ⅰ◁',-32,cy,11),('▷Ⅰ',32,cy,11),('▷Ⅱ' if ipod else '≡',0,cy-31,13)]:a.label(text,(x,y,16.1),width,ink)
    if not ipod:
        a.label('OCTAVE',(0,7,14.2),40,'#a5b2be')
        a.ring('home button',(47,-8,16),7,0,3,m['ink'])
        a.box('rear clip',(0,0,-24),(82,146,16),m['dark'],9,3)
        a.box('clip cutout',(0,-8,-33),(57,102,2),m['rubber'],5,.5)
        for y in (30,9):a.box('volume key',(-w/2-2,y,1),(5,16,9),m['ink'],2,.7)
    a.box('hold recess',(-26,h/2,0),(26,2,8),m['dark'],1,.3)
    a.box('hold orange marker',(-32,h/2+1,0),(8,1,6),m['orange'],.5,.2)
    a.box('hold switch',(-23,h/2+1,0),(12,3,7),m['white'],1,.5)
    a.box('dock connector' if ipod else 'USB port',(0,-h/2-.2,-2),(44 if ipod else 19,1,6),m['rubber'],1,.1)
    a.box('connector tongue',(0,-h/2-.8,-2),(37 if ipod else 12,.5,1.5),m['silver'],.3,.1)
    screws(a,m,[(-53,-108,-15),(53,-108,-15)] if ipod else [(-48,-85,-15)])
    a.save('ipod' if ipod else 'mp3-player')


def cd():
    a=Asset();m=palette(a)
    a.box('portable chassis',(0,-9,0),(235,242,30),m['dark'],10,3)
    a.box('upper alloy shell',(0,-9,15),(234,241,5),m['silver'],9,1)
    a.ring('disc well',(0,9,19),98,0,4,m['rubber'])
    a.ring('disc well trim',(0,9,21),99,97,2,m['chrome'])
    a.box('transport fascia',(0,-102,20),(211,37,7),m['silver'],4,1)
    a.box('LCD bezel',(-60,-100,24),(62,23,2),m['dark'],2,.5)
    lcd=a.material('LCD','#96a994',0,.55)
    a.box('LCD',(-60,-100,25.2),(55,17,.6),lcd,1,.2)
    for x,t in [(-13,'■'),(17,'Ⅰ◁'),(47,'▷Ⅰ'),(81,'▷Ⅱ')]:
        a.box('transport key',(x,-102,26),(24,18,4),m['dark'],2,.7);a.label(t,(x,-102,28.1),12,'#e6edf0')
    start=len(a.doc['nodes'])
    # The well and lid are inset from the chassis, including the rounded corners.
    # A real opening in the alloy frame; the single transparent pane keeps
    # the disc legible without overlapping transparent front/back surfaces.
    for center,size in [((0,103),(218,6)),((0,-87),(218,6)),((-105,8),(8,184)),((105,8),(8,184))]:
        a.box('lid perimeter',(center[0],center[1],31),(size[0],size[1],7),m['silver'],2,1)
    glass=a.material('clear blue acrylic','#c8e8ff',0,.16)
    a.doc['materials'][glass]['pbrMetallicRoughness']['baseColorFactor'][3]=.10
    a.doc['materials'][glass]['alphaMode']='BLEND'
    a.doc['materials'][glass]['doubleSided']=True
    a.mesh('clear viewing pane',[(-101,-84,34),(101,-84,34),(101,100,34),(-101,100,34)],
           [(0,1,2),(0,2,3)],glass,[[0,0,1]]*4)
    a.box('lid latch',(0,-88,32),(22,5,8),m['dark'],1,.5)
    a.label('OCTAVE',(0,103,34.6),33,height=4,bold=True)
    a.extract(start,'cd-lid',np.array([0,103,31]))
    a.tube('hinge barrel',[(-91,103,24),(91,103,24)],5,m['dark'])
    screws(a,m,[(-109,-118,18),(109,-118,18)])
    a.save('cd-player')
    a=Asset();m=palette(a)
    a.ring('optical disc',(0,0,0),94,7.5,1.3,m['chrome'])
    # Subtle angular spectral bands suggest the diffraction of a real CD: one
    # annulus with a small hue-wheel texture instead of 48 sector meshes.
    size=128
    yy,xx=np.mgrid[:size,:size];hue=(np.arctan2(size/2-yy,xx-size/2)/(2*math.pi))%1
    wheel=np.zeros((size,size,3),dtype=np.uint8)
    for i in range(48):
        r,g,b=colorsys.hsv_to_rgb(i/48,.13,.86);sector=np.floor(hue*48)==i
        wheel[sector]=[int(r*255),int(g*255),int(b*255)]
    mat=a.material('diffractive silver','#ffffff',.78,.24,Image.fromarray(wheel),opaque=True)
    segments=curve_segments(92);v=[];uv=[];f=[]
    for i in range(segments+1):
        t=2*math.pi*i/segments;c=math.cos(t);s=math.sin(t)
        for r in (24,92):v.append([r*c,r*s,.7]);uv.append([.5+.5*c*r/92,.5-.5*s*r/92])
    for i in range(segments):
        a0=2*i;f.extend([(a0,a0+1,a0+3),(a0,a0+3,a0+2)])
    a.mesh('spectral surface',v,f,mat,[[0,0,1]]*len(v),uv)
    a.ring('clear hub',(0,0,.9),23,7.5,.4,m['ink'])
    a.ring('clamp',(0,0,2),11,0,4,m['dark'])
    a.save('cd')


def artwork_masks():
    """Alpha masks for the printed album-art surfaces (record label, CD face),
    so the scene can use a plain opacity map instead of a layered OpacityMask."""
    size=512
    for name,hole in (('mask-record',.07),('mask-cd',.265)):
        im=Image.new('RGBA',(size,size),(255,255,255,0));draw=ImageDraw.Draw(im)
        draw.ellipse((1,1,size-2,size-2),fill=(255,255,255,255))
        if hole:
            r=size/2*hole;draw.ellipse((size/2-r,size/2-r,size/2+r,size/2+r),fill=(255,255,255,0))
        im.save(OUT/(name+'.png'),optimize=True)


def rle(channel):
    """Radiance new-style run-length encoding of one scanline channel."""
    out=bytearray();i=0;n=len(channel)
    while i<n:
        run=1
        while i+run<n and run<127 and channel[i+run]==channel[i]:run+=1
        if run>=3:out.extend((128+run,int(channel[i])));i+=run;continue
        j=i
        while j<n and j-i<128:
            if j+2<n and channel[j]==channel[j+1]==channel[j+2]:break
            j+=1
        out.append(j-i);out.extend(channel[i:j].tobytes());i=j
    return out


def studio_light():
    """Small original linear HDR studio probe, with broad softboxes and a dark floor.
    256x128 is plenty for soft reflections and prefilters four times faster than 512x256."""
    width,height=256,128
    yy,xx=np.mgrid[:height,:width];u=xx/width;v=yy/height
    rgb=np.zeros((height,width,3))+np.array([.055,.065,.085])
    for cx,cy,sx,sy,intensity,tint in [(.22,.36,.075,.19,3.5,(.86,.93,1)),(.72,.32,.10,.13,2.8,(1,.92,.8)),(.48,.17,.19,.045,1.5,(1,1,1))]:
        dx=np.minimum(abs(u-cx),1-abs(u-cx))
        strength=np.exp(-((dx/sx)**6+((v-cy)/sy)**6))
        rgb+=strength[:,:,None]*intensity*np.array(tint)
    mantissa,exponent=np.frexp(rgb.max(axis=2));factor=mantissa*256/np.maximum(rgb.max(axis=2),1e-12)
    rgbe=np.zeros((height,width,4),dtype=np.uint8);rgbe[:,:,:3]=np.minimum(255,rgb*factor[:,:,None]).astype(np.uint8);rgbe[:,:,3]=exponent+128
    output=bytearray(f'#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {height} +X {width}\n'.encode())
    for row in rgbe:
        output.extend(bytes([2,2,width>>8,width&255]))
        for channel in range(4):output.extend(rle(row[:,channel]))
    (OUT/'studio.hdr').write_bytes(output)
    print(f'studio.hdr: {width}x{height}, {len(output)//1024} KiB')

if __name__=='__main__':
    OUT.mkdir(parents=True,exist_ok=True)
    turntable();cassette();handheld(False);handheld(True);cd();artwork_masks();studio_light()
