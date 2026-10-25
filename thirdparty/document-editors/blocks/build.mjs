import {build} from 'esbuild';
import {vanillaExtractPlugin} from '@vanilla-extract/esbuild-plugin';
import {copyFile,writeFile,mkdir,readFile} from 'node:fs/promises';
import path from 'node:path';
const litOnlyIcons={name:'lit-only-icon-entry',setup(builder){
 builder.onLoad({filter:/affine-widget-drag-handle[\\/]src[\\/]watchers[\\/]pointer-event-watcher\.ts$/},async args=>{
  const source=await readFile(args.path,'utf8');
  const entry='if (this._isPointerDown) return;';
  if(!source.includes(entry))throw new Error('Pinned BlockSuite pointer watcher changed');
  const contents=source.replace(entry,"// Scoped pointerDown may be consumed before this watcher receives it.\n      if (ctx.get('pointerState').raw.buttons) return;");
  await mkdir('patched-sources',{recursive:true});await writeFile('patched-sources/pointer-event-watcher.ts',contents);
  return {contents,loader:'ts',resolveDir:path.dirname(args.path)};
 });
 builder.onLoad({filter:/std[\\/]src[\\/]clipboard[\\/]clipboard\.ts$/},async args=>{
  const source=await readFile(args.path,'utf8');
  const browserWrite='await navigator.clipboard.write([new ClipboardItem(clipboardItems)]);';
  if(!source.includes(browserWrite))throw new Error('Pinned BlockSuite clipboard writer changed');
  const contents=source.replace(browserWrite,'await (window as any).zeroslackWriteClipboard(clipboardItems);');
  await mkdir('patched-sources',{recursive:true});await writeFile('patched-sources/clipboard.ts',contents);
  return {contents,loader:'ts',resolveDir:path.dirname(args.path)};
 });
 builder.onLoad({filter:/affine-block-code[\\/]src[\\/]code-block\.ts$/},async args=>{
  const source=await readFile(args.path,'utf8');
  const reset='this.model.props.language$.value = null;';
  const label="return matchedInfo ? matchedInfo.name : 'Plain Text';";
  if(!source.includes(reset)||!source.includes(label))throw new Error('Pinned BlockSuite language fallback changed');
  const contents=source.replace(reset,'/* Preserve unrecognized language metadata while displaying plain text. */')
    .replace(label,'return matchedInfo ? matchedInfo.name : `${lang} (plain text)`;');
  await mkdir('patched-sources',{recursive:true});await writeFile('patched-sources/code-block.ts',contents);
  return {contents,loader:'ts',resolveDir:path.dirname(args.path)};
 });
 builder.onLoad({filter:/affine-components[\\/]src[\\/]icons[\\/]index\.ts$/},async args=>{
  const source=await readFile(args.path,'utf8');
  const unused="export * from './file-icons-rc';";
  if(!source.includes(unused))throw new Error('Pinned BlockSuite icon barrel changed');
  return {contents:source.replace(unused,''),loader:'ts',resolveDir:path.dirname(args.path)};
 });
}};
const product=path.join(process.env.ZEROSLACK_SOURCE_ROOT,'src/documents/web');
const output=process.env.ZEROSLACK_WEB_OUTPUT;
const options={nodePaths:[path.resolve('node_modules')],bundle:true,entryNames:'editor',format:'esm',splitting:true,minify:true,legalComments:'external',
 target:'chrome120',loader:{'.svg':'text','.png':'file','.jpg':'file','.woff':'file','.woff2':'file','.ttf':'file','.wasm':'file'},
 tsconfigRaw:{compilerOptions:{experimentalDecorators:false,useDefineForClassFields:false}},
 define:{'process.env.NODE_ENV':'"production"'},plugins:[litOnlyIcons,vanillaExtractPlugin()],metafile:true,logLevel:'info'};
const result=await build({...options,entryPoints:[path.join(product,'blocks.ts')],outdir:output});
await copyFile(path.join(product,'index.html'),path.join(output,'index.html'));
const linkedOutput=path.join(path.dirname(output),'linked');
const linked=await build({...options,entryPoints:[path.join(product,'linked.ts')],outdir:linkedOutput});
await copyFile(path.join(product,'index.html'),path.join(linkedOutput,'index.html'));
Object.assign(result.metafile.inputs,linked.metafile.inputs);
Object.assign(result.metafile.outputs,linked.metafile.outputs);
await writeFile('build-meta.json',JSON.stringify(result.metafile));
