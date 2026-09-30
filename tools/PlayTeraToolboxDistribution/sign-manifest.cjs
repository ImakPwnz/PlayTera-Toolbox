'use strict';
// Offline tool: reads only an explicitly provided existing key; no key generation,
// credentials discovery, network, uploads or logging of private key material.
const fs=require('fs'), path=require('path'), crypto=require('crypto');
const [manifestFile,keyFile,outputFile]=process.argv.slice(2);
if(!manifestFile||!keyFile||!outputFile)throw Error('Usage: node sign-manifest.cjs <manifest.json> <existing-Ed25519-private-key.pem> <manifest.sig>');
if(fs.existsSync(outputFile))throw Error('Signature output exists; refusing to replace it');
for(const file of [manifestFile,keyFile]){const info=fs.lstatSync(file);if(!info.isFile()||info.isSymbolicLink())throw Error('Inputs must be regular local files');}
const raw=fs.readFileSync(manifestFile);if(raw.length>2*1024*1024)throw Error('Manifest too large');
const manifest=JSON.parse(raw);if(manifest.schemaVersion!==1||manifest.channel!=='stable')throw Error('Wrong manifest schema/channel');
const privateKey=crypto.createPrivateKey(fs.readFileSync(keyFile));if(privateKey.asymmetricKeyType!=='ed25519')throw Error('Existing Ed25519 key required');
const publicBytes=crypto.createPublicKey(privateKey).export({format:'der',type:'spki'}).subarray(-32);
if(publicBytes.toString('base64')!=='zq8BHotT+7pQ7ulpFyXXVX1bhWCJhnYynlvHFsFlyY4=')throw Error('Key does not match the pinned PlayTera update public key');
const signature=crypto.sign(null,Buffer.concat([Buffer.from('PlayTera.Toolbox.Manifest.v1\n'),raw]),privateKey);
fs.writeFileSync(path.resolve(outputFile),signature.toString('base64')+'\n',{flag:'wx'});
console.log('Manifest signature created locally; nothing uploaded.');
