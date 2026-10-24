import {Muya,InlineFormatToolbar,CodeBlockLanguageSelector,ParagraphFrontButton,
  ParagraphFrontMenu,ParagraphQuickInsertMenu,TableColumnToolbar,TableDragBar,
  TableRowColumMenu,LinkTools,ImageToolBar,ImageResizeBar,ImageEditTool,FootnoteTool,PreviewToolBar} from '@zeroslack/muya';
import {DocumentBridge,pasteEvent,type EditorAdapter} from './bridge';
import {marked} from 'marked';
import './markdown.css';

let root=document.querySelector<HTMLElement>('#editor')!;
let bridge:DocumentBridge;
let editor:Muya|undefined;
let original='';
let changed=false;
let protectedContent=false;
const base=document.createElement('base');base.href='https://assets.zeroslack.invalid/';document.head.append(base);
for(const plugin of [InlineFormatToolbar,CodeBlockLanguageSelector,ParagraphFrontButton,ParagraphFrontMenu,
  ParagraphQuickInsertMenu,TableColumnToolbar,TableDragBar,TableRowColumMenu,ImageToolBar,ImageResizeBar,FootnoteTool,PreviewToolBar])Muya.use(plugin);
Muya.use(LinkTools,{jumpClick:(value:any)=>bridge.send({type:'open-link',url:typeof value==='string'?value:value.href??value.url??''})});

async function storeImage(image:{src:string}) {
  if(!/^(data:image\/|blob:)/i.test(image.src))return image.src;
  const blob=await (await fetch(image.src)).blob();
  if(!blob.type.startsWith('image/')||blob.size>24*1024*1024)throw new Error('Choose an image smaller than 24 MiB.');
  const bytes=new Uint8Array(await blob.arrayBuffer());let binary='';
  for(let i=0;i<bytes.length;i+=32768)binary+=String.fromCharCode(...bytes.subarray(i,i+32768));
  const response=await bridge.request('store-image',{mime:blob.type,data:btoa(binary)});return response.path as string;
}
Muya.use(ImageEditTool,{imageAction:(image:{src:string})=>bridge.track(storeImage(image))});

function unsupported(text:string) {
  // HTML is not executed. Extensions without an upstream lossless contract remain read-only.
  let html=false;marked.walkTokens(marked.lexer(text),token=>{if(token.type==='html')html=true});
  const withoutCode=text.replace(/^(`{3,}|~{3,}).*\n[\s\S]*?^\1\s*$/gm,'');
  return html||/^\s*:{3,}\w|^\s*\{[%#]|^\s*\[[^\]]+\]:\s*\{/m.test(withoutCode);
}
function semanticTokens(value:any):any {
  if(Array.isArray(value))return value.filter(token=>token?.type!=='space').map(semanticTokens);
  if(!value||typeof value!=='object')return value;
  return Object.fromEntries(Object.entries(value).filter(([key])=>key!=='raw'&&key!=='loose'&&!(key==='text'&&Array.isArray(value.tokens))).map(([key,item])=>[key,semanticTokens(item)]));
}
async function copySelection(cut:boolean){
  if(!editor||(cut&&editor.isReadOnly))return;
  const clipboard=editor.editor.clipboard;
  const value=clipboard.getClipboardData();if(!value.text&&!value.html)return;
  await bridge.pauseEditing(async()=>{
    await bridge.request('write-clipboard',value);
    if(cut){
      editor!.flush();editor!.editor.history.cutoff(editor!.editor.selection.getSelection()??undefined);
      clipboard.cutHandler();editor!.flush();editor!.editor.history.cutoff();
    }
  });
}
for(const name of ['copy','cut'])document.addEventListener(name,event=>{
  if(!root.contains(document.getSelection()?.anchorNode??null))return;
  event.preventDefault();event.stopImmediatePropagation();
  void bridge.track(copySelection(name==='cut')).catch(error=>bridge.report(String(error)));
},true);
const adapter:EditorAdapter={
  async initialize(content){
    const container=document.createElement('main');container.id='editor';root.replaceWith(container);
    editor?.destroy();root=container;original=content;changed=false;protectedContent=unsupported(content);
    editor=new Muya(root,{markdown:content,footnote:true,frontMatter:true,disableHtml:true,plantumlServer:'',
      readOnly:protectedContent,codeBlockLineNumbers:true,fontSize:16,
      fetchLinkHtml:async()=>'',imageAction:image=>bridge.track(storeImage(image)),
      clipboardWriteText:async text=>{await navigator.clipboard.writeText(text);return true}});
    // Muya replaces the supplied element with its own editable container.
    root=editor.domNode;
    const paste=editor.editor.clipboard.pasteHandler.bind(editor.editor.clipboard);
    editor.editor.clipboard.pasteHandler=(...args)=>bridge.track(paste(...args));
    editor.init({focus:false});editor.clearHistory();
    if(!protectedContent&&JSON.stringify(semanticTokens(marked.lexer(content.replace(/\r\n?/g,'\n'))))!==JSON.stringify(semanticTokens(marked.lexer(editor.getMarkdown())))){
      protectedContent=true;editor.setReadOnly(true);
    }
    editor.on('json-change',()=>{changed=true;bridge.changed()});
    return {protected:protectedContent,notice:protectedContent?'This document contains syntax without a safe visual round-trip. It is read-only; the original file is preserved.':''};
  },
  flush(){editor?.flush()},
  async snapshot(){
    editor!.flush();const content=changed?editor!.getMarkdown():original;const images=new Set<string>();
    marked.walkTokens(marked.lexer(content),token=>{if(token.type==='image')images.add(token.href)});
    return {content,images:[...images]};
  },
  async command(name,clipboard){
    if(name==='undo')editor?.undo();else if(name==='redo')editor?.redo();
    else if(name==='focus')editor?.focus();
    else if(name==='paste'){
      if(!root.contains(document.getSelection()?.anchorNode??null))editor?.focus();
      if(clipboard)await editor?.editor.clipboard.pasteHandler(pasteEvent(clipboard));else await editor?.pasteAsPlainText();
    }
    else if(name==='copy'||name==='cut')await copySelection(name==='cut');
    else if(name==='selectAll'){
      if(!root.contains(document.getSelection()?.anchorNode??null))editor?.focus();
      // The native menu means the whole document; Muya's keyboard command
      // progresses from cell/block to table and then to the whole document.
      editor?.selectAll();editor?.selectAll();editor?.selectAll();
    }
    else if(name==='insertImage'){
      const input=document.createElement('input');input.type='file';input.accept='image/*';
      input.addEventListener('change',()=>{const file=input.files?.[0];if(!file)return;
        const reader=new FileReader();reader.addEventListener('load',()=>void bridge.track(editor!.pasteImage(String(reader.result))));reader.readAsDataURL(file)});input.click();
    }
  },
  theme(dark){document.documentElement.classList.toggle('dark',dark)},
  appearance(value){editor?.setOptions({editorFontFamily:value.family,fontSize:value.size,lineHeight:value.lineHeight})},
  dispose(){editor?.destroy();editor=undefined}
};
bridge=new DocumentBridge(adapter);
