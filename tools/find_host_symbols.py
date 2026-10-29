from inspect_host import *
for phrase in [b'Overlay exception; disabled.',b'The game\'s first presentation reached the overlay.']:
 pos=data.find(phrase);target=rva_at(pos);print(phrase,hex(target))
 for va,size,off in sections:
  if va>0x1300000:continue
  for i in range(off,off+size-4):
   if rva_at(i)+4+struct.unpack_from('<i',data,i)[0]==target:print('xref',hex(rva_at(i)),fn_at(rva_at(i)))
