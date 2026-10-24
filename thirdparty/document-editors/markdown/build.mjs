import {build} from 'esbuild';
import {copyFile,writeFile,mkdir,readFile,readdir} from 'node:fs/promises';
import path from 'node:path';
const prismFiles=await readdir('node_modules/prismjs/components');
const prismImports=Object.fromEntries(prismFiles.filter(n=>/^prism-[\w-]+\.js$/.test(n)).map(n=>[n.slice(6,-3),`()=>import('prismjs/components/${n}')`]));
const prismPlugin={name:'local-prism-grammars',setup(builder){
  builder.onLoad({filter:/[\\/]utils[\\/]image\.ts$/},async args=>{
    let text=await readFile(args.path,'utf8');
    const entry='export function getImageSrc(src: string) {';
    if(!text.includes(entry))throw new Error('Pinned Muya image resolver changed');
    text=text.replace(entry,entry+`
    if (!/^(?:[a-z][a-z\\d+.-]*:|[\\/\\\\])/i.test(src)) {
        try {
            const localPath = decodeURIComponent(src.split(/[?#]/)[0]);
            if (localPath.split(/[\\/\\\\]/).includes('..')) return { isUnknownType: false, src: '' };
            return { isUnknownType: false, src: new URL(src, 'https://assets.zeroslack.invalid/').href };
        } catch { return { isUnknownType: false, src: '' }; }
    }
`);
    await mkdir('patched-sources',{recursive:true});await writeFile('patched-sources/image.ts',text);
    return {contents:text,loader:'ts',resolveDir:path.dirname(args.path)};
  });
  builder.onLoad({filter:/[\\/]utils[\\/]prism[\\/]loadLanguage\.ts$/},async args=>{
    let text=await readFile(args.path,'utf8');
    const expression=/await import\(\s*`\.\.\/\.\.\/\.\.\/node_modules\/prismjs\/components\/prism-\$\{lang\}\.js`,?\s*\)/;
    if(!expression.test(text))throw new Error('Pinned Muya grammar loader changed');
    text=text.replace(expression,'await localGrammarImports[lang]()');
    const map='const localGrammarImports={'+Object.entries(prismImports).map(([k,v])=>JSON.stringify(k)+':'+v).join(',')+'};\n';
    return {contents:map+text,loader:'ts',resolveDir:path.dirname(args.path)};
  });
}};
const product=path.join(process.env.ZEROSLACK_SOURCE_ROOT,'src/documents/web');
const output=process.env.ZEROSLACK_WEB_OUTPUT;
const result=await build({entryPoints:[path.join(product,'markdown.ts')],nodePaths:[path.resolve('node_modules')],alias:{'@zeroslack/muya':path.resolve('muya/src/index.ts')},bundle:true,outdir:output,entryNames:'editor',format:'esm',splitting:true,minify:true,legalComments:'external',
  target:'chrome120',loader:{'.svg':'text','.png':'file','.jpg':'file','.woff':'file','.woff2':'file','.ttf':'file'},
  define:{'process.env.NODE_ENV':'"production"','window.MUYA_VERSION':'"marktext-0.23.0-18fdc1e"'},
  plugins:[prismPlugin],metafile:true,logLevel:'info'});
await copyFile(path.join(product,'index.html'),path.join(output,'index.html'));
await writeFile('build-meta.json',JSON.stringify(result.metafile));
