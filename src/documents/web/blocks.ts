import {BlockStdScope,BlockSelection,type EditorHost} from '@blocksuite/affine/std';
import {Text,type Store,getAssetName} from '@blocksuite/affine/store';
import {MarkdownAdapter} from '@blocksuite/affine/shared/adapters';
import {DocModeService,DocModeExtension,EditorSettingExtension} from '@blocksuite/affine/shared/services';
import {signal} from '@preact/signals-core';
import {marked} from 'marked';
import '@toeverything/theme/style.css';
import '@toeverything/theme/fonts.css';
import {StoreExtensionManager,ViewExtensionManager} from '@blocksuite/affine/ext-loader';
import {getInternalStoreExtensions} from '@blocksuite/affine/extensions/store';
import {FoundationViewExtension} from '@blocksuite/affine/foundation/view';
import {RootViewExtension} from '@blocksuite/affine/blocks/root/view';
import {SurfaceViewExtension} from '@blocksuite/affine/blocks/surface/view';
import {NoteViewExtension} from '@blocksuite/affine/blocks/note/view';
import {ParagraphViewExtension} from '@blocksuite/affine/blocks/paragraph/view';
import {ListViewExtension} from '@blocksuite/affine/blocks/list/view';
import {CodeBlockViewExtension} from '@blocksuite/affine/blocks/code/view';
import {ImageViewExtension} from '@blocksuite/affine/blocks/image/view';
import {TableViewExtension} from '@blocksuite/affine/blocks/table/view';
import {DividerViewExtension} from '@blocksuite/affine/blocks/divider/view';
import {LatexViewExtension} from '@blocksuite/affine/blocks/latex/view';
import {CalloutViewExtension} from '@blocksuite/affine/blocks/callout/view';
import {InlinePresetViewExtension} from '@blocksuite/affine/inlines/preset/view';
import {LinkViewExtension} from '@blocksuite/affine/inlines/link/view';
import {FootnoteViewExtension} from '@blocksuite/affine/inlines/footnote/view';
import {ReferenceViewExtension} from '@blocksuite/affine/inlines/reference/view';
import {MentionViewExtension} from '@blocksuite/affine/inlines/mention/view';
import {LatexViewExtension as InlineLatexViewExtension} from '@blocksuite/affine/inlines/latex/view';
import {DragHandleViewExtension} from '@blocksuite/affine/widgets/drag-handle/view';
import {SlashMenuViewExtension} from '@blocksuite/affine/widgets/slash-menu/view';
import {ToolbarViewExtension} from '@blocksuite/affine/widgets/toolbar/view';
import {ScrollAnchoringViewExtension} from '@blocksuite/affine/widgets/scroll-anchoring/view';
import {PageDraggingAreaViewExtension} from '@blocksuite/affine/widgets/page-dragging-area/view';
import {ViewportOverlayViewExtension} from '@blocksuite/affine/widgets/viewport-overlay/view';
import {DocTitleViewExtension} from '@blocksuite/affine/fragments/doc-title/view';
import {PackageWorkspace,PackageDoc,snapshotPackage,restorePackage,validateDocument,base64ToBytes,bytesToBase64} from './package-workspace';
import {DocumentBridge,pasteEvent,type EditorAdapter} from './bridge';
import './blocks.css';

const storeManager=new StoreExtensionManager(getInternalStoreExtensions());
class PageModeService extends DocModeService {
  override getEditorMode(){return 'page' as const}
}
const viewManager=new ViewExtensionManager([FoundationViewExtension,RootViewExtension,SurfaceViewExtension,
  NoteViewExtension,ParagraphViewExtension,ListViewExtension,CodeBlockViewExtension,ImageViewExtension,
  TableViewExtension,DividerViewExtension,LatexViewExtension,CalloutViewExtension,InlinePresetViewExtension,
  LinkViewExtension,FootnoteViewExtension,ReferenceViewExtension,MentionViewExtension,InlineLatexViewExtension,DragHandleViewExtension,SlashMenuViewExtension,
  ToolbarViewExtension,ScrollAnchoringViewExtension,PageDraggingAreaViewExtension,ViewportOverlayViewExtension,DocTitleViewExtension]);
const root=document.querySelector<HTMLElement>('#editor')!;root.classList.add('affine-page-viewport');
viewManager.configure(FoundationViewExtension,{fontConfig:[]});
let bridge:DocumentBridge;
window.zeroslackWriteClipboard=items=>bridge.track(bridge.pauseEditing(async()=>{
  const value:Record<string,string>={};
  for(const [type,blob] of Object.entries(items)){
    if(blob.size>24*1024*1024)throw new Error('The clipboard item exceeds 24 MiB.');
    if(type==='text/plain')value.text=await blob.text();
    else if(type==='text/html')value.html=await blob.text();
    else if(type==='image/png')value.image=bytesToBase64(new Uint8Array(await blob.arrayBuffer()));
  }
  await bridge.request('write-clipboard',value);
}));
let workspace:PackageWorkspace|undefined;
let doc:PackageDoc|undefined;
let store:Store|undefined;
let host:EditorHost|undefined;
let std:BlockStdScope|undefined;
const uploadingAssets=new Set<any>();
function dispose(){root.replaceChildren();host=undefined;std=undefined;store=undefined;doc=undefined;workspace?.dispose();workspace=undefined;uploadingAssets.clear()}
const adapter:EditorAdapter={
  async initialize(content,options){
    dispose();
    const extensions=storeManager.get('store');
    if(content&&!options?.importMarkdown){const restored=await restorePackage(content,extensions);workspace=restored.workspace;doc=restored.document}
    else {workspace=new PackageWorkspace(extensions);doc=workspace.createDoc()}
    store=doc.getStore();store.load();
    const conversionNotices:string[]=[];
    if(options?.importMarkdown){
      let hasHtml=false;marked.walkTokens(marked.lexer(content),token=>{if(token.type==='html')hasHtml=true});
      if(hasHtml)throw new Error('Markdown import cannot safely represent raw HTML. Keep this content in Markdown or replace the HTML before importing. The source file is unchanged.');
      const frontMatter=content.match(/^---\r?\n[\s\S]*?\r?\n(?:---|\.\.\.)(?:\r?\n|$)/);
      if(frontMatter){content='````yaml\n'+frontMatter[0]+'\n````\n'+content.slice(frontMatter[0].length);conversionNotices.push('YAML front matter is preserved as a code block.')}
      const job=store.getTransformer();const converter=new MarkdownAdapter(job,store.provider);
      try{
      const images=new Set<string>();marked.walkTokens(marked.lexer(content),token=>{if(token.type==='image')images.add(token.href)});
      for(const reference of images){
        if(/^[a-z][a-z\d+.-]*:/i.test(reference)&&!reference.startsWith('data:image/'))throw new Error(`Copy the remote image into the source document folder before importing: ${reference}`);
        const response=reference.startsWith('data:image/')?null:await bridge.request('read-import-image',{path:reference});
        const blob=response?new Blob([base64ToBytes(response.data)],{type:response.mime}):await(await fetch(reference)).blob();
        if(!blob.type.startsWith('image/')||blob.size>24*1024*1024)throw new Error('An imported image is invalid or exceeds 24 MiB.');
        const id=await workspace!.blobSync.set(blob);job.assetsManager.getPathBlobIdMap().set(decodeURIComponent(reference),id);job.assets.set(id,blob);
      }
      const converted=await converter.toDocSnapshot({file:content,assets:job.assetsManager});
      const replacement=await job.snapshotToDoc(converted);
      if(!replacement?.root)throw new Error('Markdown conversion did not produce a valid document.');
      const old=doc;store=replacement;doc=workspace!.getDoc(replacement.id)!;
      workspace!.removeDoc(old!.id);
      }finally{job[Symbol.dispose]()}
    }else if(!content)store.transact(()=>{
      const page=store!.addBlock('affine:page',{title:new Text('')});
      store!.addBlock('affine:surface',{},page);
      const note=store!.addBlock('affine:note',{},page);
      store!.addBlock('affine:paragraph',{text:new Text('')},note);
    });
    if(!store.root)throw new Error('The block document has no page root.');
    validateDocument(workspace!,doc!);
    // Drag/drop inserts asynchronously through transformers, outside clipboard.
    const getTransformer=store.getTransformer.bind(store);
    store.getTransformer=(...args)=>{
      const job=getTransformer(...args);
      const snapshotToSlice=job.snapshotToSlice.bind(job);
      job.snapshotToSlice=(...parameters)=>bridge.track(snapshotToSlice(...parameters));
      return job;
    };
    std=new BlockStdScope({store,extensions:[...viewManager.get('page'),DocModeExtension(new PageModeService()),EditorSettingExtension({setting$:signal({})})]});
    const copy=std.clipboard.copy;
    std.clipboard.copy=(...args)=>bridge.track(bridge.pauseEditing(()=>copy(...args)));
    const paste=std.clipboard.paste;
    std.clipboard.paste=(...args)=>bridge.track(paste(...args));
    const pasteBlock=std.clipboard.pasteBlockSnapshot;
    std.clipboard.pasteBlockSnapshot=(...args)=>bridge.track(pasteBlock(...args));
    std.clipboard.use(({assetsManager})=>{uploadingAssets.add(assetsManager);return()=>uploadingAssets.delete(assetsManager)});
    std.clipboard.use(({assetsManager,slots})=>{
      const subscription=slots.afterExport.subscribe(payload=>{
        if(payload.type!=='block')return;
        const sourceId=payload.snapshot.props.sourceId;
        if(typeof sourceId==='string'){
          const blob=workspace!.blobs.map.get(sourceId);
          if(blob)assetsManager.getAssets().set(sourceId,blob);
        }
      });
      return()=>subscription.unsubscribe();
    });
    host=std.render();const title=document.createElement('doc-title');title.doc=store;root.append(title,host);
    await Promise.all([host.updateComplete,title.updateComplete]);
    // Mount-time defaults belong to initialization, not to the user's first undo step.
    await new Promise(resolve=>setTimeout(resolve,0));store.resetHistory();
    doc.spaceDoc.on('update',()=>bridge.changed());workspace.doc.on('update',()=>bridge.changed());
    if(options?.importMarkdown)return {notice:['Imported as an independent block document. Markdown source layout is normalized; fenced diagrams remain code blocks.',...conversionNotices].join(' ')};
  },
  flush(){},
  async settle(){
    const deadline=performance.now()+25000;
    const imagesPending=()=>[...doc!.yBlocks.values()].some(block=>block.get('sys:flavour')==='affine:image'
      &&(!block.get('prop:sourceId')||!workspace!.blobs.map.has(String(block.get('prop:sourceId')))));
    while([...uploadingAssets].some(value=>value.uploadingAssetsMap.size)||imagesPending()){
      if(performance.now()>deadline)throw new Error('An image is still unavailable. Finish inserting it, restore it, or remove its placeholder before saving.');
      await new Promise(resolve=>setTimeout(resolve,20));
    }
  },
  async snapshot(){return JSON.stringify(await snapshotPackage(workspace!,doc!))},
  async exportMarkdown(){
    const job=store!.getTransformer();
    try{
      const snapshot=job.docToSnapshot(store!);if(!snapshot)throw new Error('The block document could not be converted.');
      const converted=await new MarkdownAdapter(job,store!.provider).fromDocSnapshot({snapshot,assets:job.assetsManager});
      const attachments=[];
      for(const id of converted.assetsIds){
        const blob=job.assets.get(id);if(!blob)throw new Error(`Missing attachment: ${id}`);
        const bytes=new Uint8Array(await blob.arrayBuffer());
        attachments.push({path:`assets/${getAssetName(job.assets,id)}`,mime:blob.type,data:bytesToBase64(bytes)});
      }
      return {content:converted.file,attachments,warnings:['Block identities, undo history, layout and formatting without a Markdown equivalent are not exported.','Images are copied to the assets folder beside the Markdown file.']};
    }finally{job[Symbol.dispose]()}
  },
  async command(name,clipboard){
    if(name==='undo')store?.undo();else if(name==='redo')store?.redo();
    else if(name==='focus')host?.querySelector('affine-page-root')?.focusFirstParagraph();
    else if(name==='paste'&&clipboard){
      if(!root.contains(document.getSelection()?.anchorNode??null))host?.querySelector('affine-page-root')?.focusFirstParagraph();
      const target=document.getSelection()?.anchorNode?.parentElement??host!;
      target.dispatchEvent(pasteEvent(clipboard));await new Promise(resolve=>setTimeout(resolve,0));
    }else if(name==='copy'||name==='cut'){
      const target=document.getSelection()?.anchorNode?.parentElement??host!;
      target.dispatchEvent(new ClipboardEvent(name,{clipboardData:new DataTransfer(),bubbles:true,cancelable:true}));
      await new Promise(resolve=>setTimeout(resolve,0));
    }else if(name==='selectAll'){
      const blocks=store!.root!.children.filter(model=>model.flavour==='affine:note').flatMap(model=>model.children);
      std!.selection.setGroup('note',blocks.map(model=>std!.selection.create(BlockSelection,{blockId:model.id})));
    }
  },
  theme(dark){document.documentElement.classList.toggle('dark',dark);document.documentElement.dataset.theme=dark?'dark':'light'},
  appearance(value){const style=document.documentElement.style;style.setProperty('--affine-font-family',value.family);style.setProperty('--affine-font-base',`${value.size}px`);style.setProperty('--affine-line-height',String(value.lineHeight))},
  dispose
};
bridge=new DocumentBridge(adapter);
