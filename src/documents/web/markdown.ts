import {Muya,InlineFormatToolbar,CodeBlockLanguageSelector,ParagraphFrontButton,
  ParagraphFrontMenu,ParagraphQuickInsertMenu,TableColumnToolbar,TableDragBar,
  TableRowColumMenu,LinkTools,ImageToolBar,ImageResizeBar,ImageEditTool,FootnoteTool,PreviewToolBar} from '@zeroslack/muya';
import {DocumentBridge,pasteEvent,type EditorAdapter} from './bridge';
import {marked} from 'marked';
import * as json1 from 'ot-json1';
import * as unicode from 'ot-text-unicode';
import {MarkdownSession} from './markdown-session';
import './markdown.css';

let root=document.querySelector<HTMLElement>('#editor')!;
let bridge:DocumentBridge;
let editor:Muya|undefined;
let original='';
let changed=false;
let protectedContent=false;
let shared:MarkdownSession|undefined;
let receiving=false;
let boundary=false;
let previousInput='';
let savedSelection:any=null;
function captureSelection(){
  if(receiving||!document.hasFocus()||!root.contains(document.getSelection()?.anchorNode??null))return;
  const current=editor?.editor.selection.getSelection();
  if(current)savedSelection={anchor:{path:[...current.anchor.path],offset:current.anchor.offset},focus:{path:[...current.focus.path],offset:current.focus.offset}};
}
function restoreSelection(){
  if(!savedSelection||!editor)return false;
  const resolve=(edge:any)=>{
    if(!Array.isArray(edge?.path)||edge.path.length>64||!Number.isSafeInteger(edge.offset)||edge.offset<0)return null;
    const block=editor!.editor.scrollPage?.queryBlock([...edge.path]);
    return block?.isContent()?{path:[...edge.path],block,offset:Math.min(edge.offset,block.text.length)}:null;
  };
  const anchor=resolve(savedSelection.anchor),focus=resolve(savedSelection.focus);
  if(!anchor||!focus)return false;
  editor.editor.selection.setSelection(anchor,focus);return true;
}
document.addEventListener('selectionchange',()=>{captureSelection();bridge?.viewChanged()});
window.addEventListener('focus',()=>{if(editor&&!receiving){const top=document.scrollingElement?.scrollTop??0;restoreSelection();if(document.scrollingElement)document.scrollingElement.scrollTop=top}});
document.addEventListener('beforeinput',(event:InputEvent)=>{
  const kind=event.inputType.startsWith('insert')?'insert':event.inputType.startsWith('delete')?'delete':'';
  boundary||=!!kind&&((!!previousInput&&previousInput!==kind)||!!event.data?.match(/\s/));previousInput=kind;
},true);
for(const name of ['pointerdown','paste','cut'])document.addEventListener(name,()=>{boundary=true},true);
function remoteOperation(op:any){
  const scroll=[document.scrollingElement?.scrollTop??0,document.scrollingElement?.scrollLeft??0];
  captureSelection();
  let selection=savedSelection?structuredClone(savedSelection):null;
  const before=editor!.editor.jsonState.getState();
  if(selection)for(const edge of [selection.anchor,selection.focus]){
    const path=[...edge.path];
    let text=path.reduce((value:any,key:any)=>value?.[key],before);
    const cursor=new json1.ReadCursor(op);
    cursor.traverse(null,component=>{if(component.es&&typeof text==='string'&&JSON.stringify(cursor.getPath())===JSON.stringify(path)){
      // Muya/DOM offsets count UTF-16 code units; ot-text-unicode counts code points.
      const offset=unicode.type.transformPosition([...text.slice(0,edge.offset)].length,component.es as any);
      text=unicode.type.apply(text,component.es as any);edge.offset=[...text].slice(0,offset).join('').length;
    }});
    const updated=json1.type.transformPosition(edge.path,op);if(updated)edge.path=updated as any;else selection=null;
  }
  receiving=true;
  try{
    editor!.editor.history.suppressRecording(()=>editor!.editor.updateContents(op,null,'api' as any));editor!.clearHistory();
    savedSelection=selection;if(document.hasFocus())restoreSelection();
  }finally{receiving=false;if(document.scrollingElement){document.scrollingElement.scrollTop=scroll[0];document.scrollingElement.scrollLeft=scroll[1]}bridge.viewChanged()}
}
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
  if(cut)boundary=true;
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
  async initialize(content,options){
    shared?.close();shared=undefined;
    const container=document.createElement('main');container.id='editor';root.replaceWith(container);
    editor?.destroy();root=container;original=content;changed=false;savedSelection=null;protectedContent=unsupported(content);
    editor=new Muya(root,{markdown:content,footnote:true,frontMatter:true,disableHtml:true,plantumlServer:'',
      readOnly:protectedContent,codeBlockLineNumbers:true,fontSize:16,
      fetchLinkHtml:async()=>'',imageAction:image=>bridge.track(storeImage(image)),
      clipboardWriteText:async text=>{await navigator.clipboard.writeText(text);return true}});
    // Muya replaces the supplied element with its own editable container.
    root=editor.domNode;root.id='editor';
    const paste=editor.editor.clipboard.pasteHandler.bind(editor.editor.clipboard);
    editor.editor.clipboard.pasteHandler=(...args)=>bridge.track(paste(...args));
    editor.init({focus:false});editor.clearHistory();
    if(!protectedContent&&JSON.stringify(semanticTokens(marked.lexer(content.replace(/\r\n?/g,'\n'))))!==JSON.stringify(semanticTokens(marked.lexer(editor.getMarkdown())))){
      protectedContent=true;editor.setReadOnly(true);
    }
    shared=new MarkdownSession('markdown:'+options!.session,()=>editor?.flush(),remoteOperation,
      revision=>{changed=true;bridge.changed(revision)},error=>{if(editor){editor.setReadOnly(true);bridge.report(String(error))}},options!.view);
    const joined=await shared.join(editor.editor.jsonState.getState());
    if(JSON.stringify(joined.state)!==JSON.stringify(editor.editor.jsonState.getState()))editor.setContent(joined.state);
    changed=joined.revision>0;
    bridge.setRevision(joined.revision);
    editor.on('json-change',({op}:any)=>{
      if(receiving||!op)return;
      changed=true;shared!.submit(op,boundary);boundary=false;editor!.clearHistory();
    });
    editor.editor.history.undo=()=>{void bridge.track(shared!.command('undo')).catch(error=>bridge.report(String(error)))};
    editor.editor.history.redo=()=>{void bridge.track(shared!.command('redo')).catch(error=>bridge.report(String(error)))};
    return {protected:protectedContent,notice:protectedContent?'This document contains syntax without a safe visual round-trip. It is read-only; the original file is preserved.':''};
  },
  detachView(view){shared?.detachView(view)},
  viewState(){captureSelection();return {selection:savedSelection,top:document.scrollingElement?.scrollTop??0,left:document.scrollingElement?.scrollLeft??0}},
  restoreViewState(state){
    savedSelection=state.selection??null;if(document.hasFocus())restoreSelection();
    if(document.scrollingElement){document.scrollingElement.scrollTop=Math.max(0,Number(state.top)||0);document.scrollingElement.scrollLeft=Math.max(0,Number(state.left)||0)}
  },
  flush(){editor?.flush()},
  async settle(){await shared?.settle()},
  async snapshot(){
    editor!.flush();const content=changed?editor!.getMarkdown():original;const images=new Set<string>();
    marked.walkTokens(marked.lexer(content),token=>{if(token.type==='image')images.add(token.href)});
    return {content,images:[...images]};
  },
  async command(name,clipboard){
    if(['paste','cut','insertImage'].includes(name))boundary=true;
    if(name==='undo'||name==='redo'){editor?.flush();await shared?.command(name)}
    else if(name==='focus'){if(!restoreSelection())editor?.focus()}
    else if(name==='paste'){
      if(!root.contains(document.getSelection()?.anchorNode??null)&&!restoreSelection())editor?.focus();
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
  dispose(){const previous=shared;shared=undefined;editor?.destroy();editor=undefined;previous?.close()}
};
bridge=new DocumentBridge(adapter);
