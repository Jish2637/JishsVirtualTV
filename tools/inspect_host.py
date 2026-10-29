"""Read-only PE helpers for the pinned official host."""
import pathlib,struct
data=pathlib.Path('C:/Games/ReSkate/ReSkate.dll').read_bytes();pe=struct.unpack_from('<I',data,0x3c)[0];n=struct.unpack_from('<H',data,pe+6)[0];opt=struct.unpack_from('<H',data,pe+20)[0];sections=[]
for i in range(n):
 o=pe+24+opt+i*40;size,va,rawsize,raw=struct.unpack_from('<IIII',data,o+8);sections.append((va,rawsize,raw))
def rva_at(raw):
 for va,size,offset in sections:
  if offset<=raw<offset+size:return va+raw-offset
def file_at(rva):
 for va,size,offset in sections:
  if va<=rva<va+size:return offset+rva-va
def fn_at(rva):
 directory=pe+24+112+3*8;start,size=struct.unpack_from('<II',data,directory);off=file_at(start)
 for i in range(size//12):
  a,b,_=struct.unpack_from('<III',data,off+i*12)
  if a<=rva<b:return hex(a),hex(b)
def refs(target):
 for va,size,off in sections:
  for i in range(off,off+size-4):
   if data[i]==0xe8 and (rva_at(i)+5+struct.unpack_from('<i',data,i+1)[0])==target: print('call',hex(rva_at(i)),'function',fn_at(rva_at(i)))
if __name__=='__main__':
 refs(0xe54f80)
