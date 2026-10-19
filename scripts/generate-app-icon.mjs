// Optional artwork regeneration: npm install --no-save sharp, then run this file.
// Normal builds use the committed PNG/ICO files and need no Node dependencies.
import {createRequire} from 'node:module';
import {readFileSync,writeFileSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
const require=createRequire(import.meta.url);
const sharp=require(process.env.GAMELINK_SHARP_PATH || 'sharp');
const directory=fileURLToPath(new URL('../assets/icons/',import.meta.url));
const svg=readFileSync(directory+'gamelink.svg');
const sizes=[16,20,24,32,40,48,64,128,256];
const images=[];
for(const size of sizes){
  // Oversample the vector before downscaling to keep small edges smooth.
  const source=await sharp(svg).resize(1024,1024).png().toBuffer();
  const rgba=await sharp(source).resize(size,size).ensureAlpha().raw().toBuffer();
  if(size===256){
    const png=await sharp(rgba,{raw:{width:size,height:size,channels:4}}).png().toBuffer();
    writeFileSync(directory+'gamelink.png',png);images.push(png);
  }else{
    // Windows DIB + AND mask for small sizes, PNG for the 256px frame.
    const maskStride=Math.ceil(size/32)*4;
    const dib=Buffer.alloc(40+size*size*4+maskStride*size);
    dib.writeUInt32LE(40,0);dib.writeInt32LE(size,4);dib.writeInt32LE(size*2,8);
    dib.writeUInt16LE(1,12);dib.writeUInt16LE(32,14);dib.writeUInt32LE(size*size*4,20);
    for(let y=0;y<size;y++)for(let x=0;x<size;x++){
      const from=(y*size+x)*4,to=40+((size-1-y)*size+x)*4;
      dib[to]=rgba[from+2];dib[to+1]=rgba[from+1];dib[to+2]=rgba[from];dib[to+3]=rgba[from+3];
      if(rgba[from+3]===0)dib[40+size*size*4+(size-1-y)*maskStride+(x>>3)]|=0x80>>(x&7);
    }
    images.push(dib);
  }
}
const header=Buffer.alloc(6+sizes.length*16);
header.writeUInt16LE(1,2);header.writeUInt16LE(sizes.length,4);
let offset=header.length;
images.forEach((bytes,index)=>{
  const entry=6+index*16,size=sizes[index];
  header[entry]=size===256?0:size;header[entry+1]=header[entry];
  header.writeUInt16LE(1,entry+4);header.writeUInt16LE(32,entry+6);
  header.writeUInt32LE(bytes.length,entry+8);header.writeUInt32LE(offset,entry+12);offset+=bytes.length;
});
writeFileSync(directory+'gamelink.ico',Buffer.concat([header,...images]));
console.log('Generated GameLink PNG and nine-size Windows ICO from SVG.');
