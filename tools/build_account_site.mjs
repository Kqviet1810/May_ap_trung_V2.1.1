import { copyFile, mkdir, readdir, rm } from 'node:fs/promises';
import { resolve, join } from 'node:path';
const root=resolve(import.meta.dirname,'..'), target=join(root,'cloudflare/public');
if(target!==resolve(root,'cloudflare','public')) throw new Error('Invalid generated asset path');
// Only this verified generated directory is replaced, preventing stale extra assets.
await rm(target,{recursive:true,force:true});
await mkdir(target,{recursive:true});
// An explicit public allowlist prevents firmware, source maps, secrets or repo metadata exposure.
const files=['index.html','styles.css','landing.css','config.js','account.js','app.js','push.js','protocol_v2.js',
  'sw.js','manifest.webmanifest','vendor/mqtt.min.js','vendor/jsQR.min.js','docs/MAYAP_Huong_dan_van_hanh_A5_v1.3_E503.pdf'];
for(const file of await readdir(join(root,'icons'))) if(/\.(png|svg|ico)$/.test(file)) files.push('icons/'+file);
for(const file of files){await mkdir(resolve(target,file,'..'),{recursive:true});await copyFile(join(root,file),join(target,file));}
console.log(`Built ${files.length} public assets for same-origin Worker hosting.`);
