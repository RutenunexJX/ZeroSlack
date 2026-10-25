import {BlockStdScope,BlockViewIdentifier,BlockFlavourIdentifier} from '@blocksuite/affine/std';
import {GfxControllerIdentifier,generateKeyBetweenV2} from '@blocksuite/affine/std/gfx';
import {BlockSchemaIdentifier,Text,type Store} from '@blocksuite/affine/store';
import {BookmarkBlockSchema,BookmarkBlockModel,LocalConnectorElementModel,ConnectorMode,StrokeStyle} from '@blocksuite/affine/model';
import {ConnectorPathGenerator} from '@blocksuite/affine/gfx/connector';
import {RefNodeSlotsProvider} from '@blocksuite/affine/inlines/reference';
import {ImageProxyService} from '@blocksuite/affine/shared/adapters';
import {literal} from 'lit/static-html.js';
import {nothing} from 'lit';
import {computed,signal} from '@preact/signals-core';
import {StoreExtensionManager,ViewExtensionManager} from '@blocksuite/affine/ext-loader';
import {getInternalStoreExtensions} from '@blocksuite/affine/extensions/store';
import {getInternalViewExtensions} from '@blocksuite/affine/extensions/view';
import {FoundationViewExtension} from '@blocksuite/affine/foundation/view';
import {DocModeService,DocModeExtension,EditorSettingExtension,ToolbarModuleExtension,ActionPlacement,LinkPreviewServiceIdentifier} from '@blocksuite/affine/shared/services';
import {PackageWorkspace,type PackageDoc,snapshotPackage,restorePackage,bytesToBase64} from './package-workspace';
import type {} from './bridge';
import '@toeverything/theme/style.css';
import './linked.css';

type Reference={id:string;source:string;anchor:string;title:string;status:string;parent?:string};
type Preview={title?:string;text?:string;image?:string;error?:string;location?:string;links?:number;state?:string};
type Geometry={width?:number;height?:number;left:number;right:number;top:number;bottom:number;detail?:boolean;anchors:{id:string;y:number;visible:boolean;active:boolean}[]};
const refs=new Map<string,Reference>(),previews=new Map<string,Preview>(),wires=new Map<string,LocalConnectorElementModel>();
const sourceViews=new Map<string,SourceBookmark>();
const previewRevision=signal(0),relatedRevision=signal(0);
const root=document.querySelector<HTMLElement>('#editor')!;root.classList.add('affine-edgeless-viewport');
let session='',workspace:PackageWorkspace|undefined,doc:PackageDoc|undefined,store:Store|undefined,std:BlockStdScope|undefined;
let activeDoc:PackageDoc|undefined,readOnly=false,documentViews:Record<string,{zoom:number;centerX:number;centerY:number}>={};
const observedDocs=new Set<PackageDoc>(),adaptedStores=new WeakSet<Store>();
let workspaceSubscriptions:{unsubscribe():void}[]=[];
let geometry:Geometry={left:400,right:1000,top:0,bottom:800,anchors:[]};
let qtGeometry=geometry;
function setGeometry(value:Geometry){qtGeometry=value;const sx=innerWidth/(value.width||innerWidth),sy=innerHeight/(value.height||innerHeight);geometry={...value,left:value.left*sx,right:value.right*sx,top:value.top*sy,bottom:value.bottom*sy,anchors:value.anchors.map(a=>({...a,y:a.y*sy}))}}
let initialized=false,saveTimer=0,viewTimer=0,revision=0,lastDemand='',lastOcclusion='',selectedSource='';
let seenReferences=new Set<string>(),subscriptions:{unsubscribe():void}[]=[],pending=Promise.resolve();
const operations=new Set<Promise<unknown>>(),uploadingAssets=new Set<any>();
function track<T>(value:Promise<T>):Promise<T>{operations.add(value);value.finally(()=>operations.delete(value)).catch(()=>{});return value}
const emit=(type:string,value:Record<string,unknown>={})=>window.chrome.webview.postMessage({type,session,...value});
const gfx=()=>std!.get(GfxControllerIdentifier);
const sourceOf=(model:BookmarkBlockModel)=>model&&refs.get((model.props as any).zsReference);
const sourceModels=()=>store?.getBlocksByFlavour('affine:bookmark').map(b=>b.model as BookmarkBlockModel).filter(m=>(m.props as any).zsReference)??[];
const isSource=(model:any)=>typeof model?.props?.zsReference==='string'&&!!model.props.zsReference;

// Only source-binding metadata extends the native Bookmark schema/model.
const sourceSchema={setup(di:any){di.override(BlockSchemaIdentifier('affine:bookmark'),()=>({...BookmarkBlockSchema,model:{...BookmarkBlockSchema.model,
  props:(primitives:any)=>({...BookmarkBlockSchema.model.props!(primitives),zsReference:'',zsExpanded:null})}}))}};
const storeManager=new StoreExtensionManager(getInternalStoreExtensions());
const viewManager=new ViewExtensionManager(getInternalViewExtensions());
viewManager.configure(FoundationViewExtension,{fontConfig:[]});
const nativeViewExtensions=viewManager.get('edgeless');
const NativeBookmark=customElements.get('affine-edgeless-bookmark')!;
if(!NativeBookmark)throw new Error('The fixed BlockSuite Bookmark Edgeless component is unavailable.');
class SourceBookmark extends NativeBookmark {
  private sourceMounted=false;
  private sourceVisible=true;
  connectedCallback(){super.connectedCallback();this.sourceMounted=true;const model=(this as any).model;sourceViews.set(model.id,this);this.setSourceVisible(referenceVisible(model))}
  disconnectedCallback(){this.sourceMounted=false;sourceViews.delete((this as any).model.id);super.disconnectedCallback()}
  setSourceVisible(visible:boolean){if(this.sourceVisible!==visible){this.sourceVisible=visible;(this as any).requestUpdate()}}
  renderGfxBlock(){return this.sourceVisible||(this as any).gfx.selection.has((this as any).model.id)?super.renderGfxBlock():nothing}
  // Native rendering, styles, selection, drag, resize, and caption stay intact.
  // Resolved material is transient and never serialized into a separate source.
  linkPreview$=computed(()=>{
    previewRevision.value;relatedRevision.value;
    const model=(this as any).model as BookmarkBlockModel,r=sourceOf(model),p=r&&previews.get(r.source);
    this.classList.toggle('source-related',!!r&&(selectedSource===r.id||geometry.anchors.some(a=>a.id===r.anchor&&a.active)));
    const description=p?.error||p?.text||p?.state||(p?.image?'':r?'Preview loads when visible.':'This code link is no longer available.');
    return {title:p?.title||r?.title||model.props.title||'Linked reference',description:(r&&r.status!=='resolved'?r.status+'. ':'')+description,image:p?.image||null,icon:null};
  });
  refreshData=()=>{const r=sourceOf((this as any).model);if(this.sourceMounted&&initialized&&r&&JSON.parse(lastDemand||'[]').includes(r.id))emit('retry',{id:r.id})};
  open=()=>{const r=sourceOf((this as any).model);if(r)emit('open',{id:r.id});};
}
customElements.define('zeroslack-source-bookmark',SourceBookmark);
class LinkedMode extends DocModeService{override getEditorMode(){return 'edgeless' as const}}
function sourceAction(name:string){return(ctx:any)=>{const r=sourceOf(ctx.getCurrentModelByType(BookmarkBlockModel));if(r)emit(name,{id:r.id})}}
const sourceToolbar=ToolbarModuleExtension({id:BlockFlavourIdentifier('custom:affine:surface:bookmark'),config:{
  when:ctx=>ctx.getSurfaceModelsByType(BookmarkBlockModel).length===1&&isSource(ctx.getCurrentModelByType(BookmarkBlockModel)),actions:[
    {id:'source.code',label:'Go to code',showLabel:true,run:sourceAction('select')},
    {id:'source.enlarge',label:'Enlarge reference',showLabel:true,run:sourceAction('enlarge')},
    {id:'source.open',label:'Open original',placement:ActionPlacement.More,run:sourceAction('open')},
    {id:'source.retry',label:'Reload reference',placement:ActionPlacement.More,run:sourceAction('retry')},
    {id:'source.links',label:'Expand linked references',placement:ActionPlacement.More,
      when:ctx=>!!previews.get(sourceOf(ctx.getCurrentModelByType(BookmarkBlockModel))?.source||'')?.links,run:sourceAction('deeper')},
    {id:'source.collapse',label:'Collapse / expand reference',placement:ActionPlacement.More,run(ctx){
      const model=ctx.getCurrentModelByType(BookmarkBlockModel)!;const props=model.props as any,bounds=JSON.parse(model.xywh);
      if(props.zsExpanded){const old=props.zsExpanded;ctx.store.updateBlock(model,{style:old.style,xywh:JSON.stringify([bounds[0],bounds[1],old.width,old.height]),zsExpanded:null});}
      else ctx.store.updateBlock(model,{style:'cube',xywh:JSON.stringify([bounds[0],bounds[1],170,114]),zsExpanded:{style:props.style,width:bounds[2],height:bounds[3]}});
    }}
  ]}});

function detachView(){
  initialized=false;clearTimeout(saveTimer);clearTimeout(viewTimer);
  popupObserver.disconnect();observedRoots=new WeakSet();popupObserver.observe(document.body,{childList:true,subtree:true});
  for(const subscription of subscriptions)subscription.unsubscribe();subscriptions=[];
  root.replaceChildren();store=undefined;std=undefined;activeDoc=undefined;wires.clear();uploadingAssets.clear();lastDemand='';lastOcclusion='';
}
function dispose(){
  detachView();for(const subscription of workspaceSubscriptions)subscription.unsubscribe();workspaceSubscriptions=[];
  observedDocs.clear();workspace?.dispose();workspace=undefined;doc=undefined;documentViews={};
  refs.clear();previews.clear();wires.clear();uploadingAssets.clear();previewRevision.value++;lastDemand='';lastOcclusion='';
}
function scheduleView(){if(!initialized)return;clearTimeout(viewTimer);viewTimer=window.setTimeout(updateView,35)}
function changed(){if(!initialized)return;++revision;clearTimeout(saveTimer);saveTimer=window.setTimeout(()=>save().catch(error=>emit('error',{error:String(error)})),450)}
async function settle(){
  const deadline=performance.now()+25000;
  const imagesPending=()=>[...(workspace?.docs.values()??[])].some(document=>document.getStore().getBlocksByFlavour('affine:image').some(block=>!block.model.props.sourceId||!workspace?.blobs.map.has(String(block.model.props.sourceId))));
  while(operations.size||[...uploadingAssets].some(value=>value.uploadingAssetsMap.size)||imagesPending()){
    if(performance.now()>deadline)throw new Error('Finish the pending board import before switching files.');
    await new Promise(resolve=>setTimeout(resolve,25));
  }
}
async function save(explicit=false,request=''){
  if(!initialized||!workspace||!doc)return;
  await settle();
  const token=session,serial=revision;rememberView();const native=await snapshotPackage(workspace,doc,true);
  if(token!==session)return;
  emit('layout',{state:{version:2,native,referenceIds:[...seenReferences],viewport:documentViews[doc.id],documentViews},explicit,request,revision:serial});
  if(serial!==revision)changed();
}
function updateView(){
  if(!initialized)return;
  const viewport=gfx().viewport,surface=gfx().surface!,visible:string[]=[],live=new Set<string>();
  const anchors=new Map(geometry.anchors.map(a=>[a.id,a]));
  for(const model of sourceModels()){
    const r=sourceOf(model);if(!r)continue;
    const b=model.elementBound,[x,y]=viewport.toViewCoord(b.x,b.y),w=b.w*viewport.zoom,h=b.h*viewport.zoom;
    const onScreen=referenceVisible(model);sourceViews.get(model.id)?.setSourceVisible(onScreen);
    if(onScreen&&!(model.props as any).zsExpanded)visible.push(r.id);
    const a=anchors.get(r.anchor);if(!onScreen||!a)continue;
    live.add(model.id);let wire=wires.get(model.id);
    if(!wire){wire=new LocalConnectorElementModel(surface);wire.id='code-anchor:'+model.id;wire.index='a0';wire.includesPoint=()=>false;wire.isLocked=()=>true;wires.set(model.id,wire);surface.addLocalElement(wire);}
    const edge=x+w/2<(geometry.left+geometry.right)/2?geometry.left:geometry.right;
    wire.mode=ConnectorMode.Straight;wire.source={position:viewport.toModelCoord(edge,Math.max(geometry.top,Math.min(geometry.bottom,a.y)))};wire.target={id:model.id};
    wire.stroke=a.active?'#3478d4':'#a6b4c8';wire.strokeWidth=a.active?2:1;wire.strokeStyle=a.visible?StrokeStyle.Solid:StrokeStyle.Dashed;wire.rough=false;
    ConnectorPathGenerator.updatePath(wire,null,id=>gfx().getElementById(id));
    surface.localElementUpdated.next({model:wire,props:{xywh:wire.xywh},oldValues:{}});
  }
  for(const [id,wire]of wires)if(!live.has(id)){surface.deleteLocalElement(wire);wires.delete(id)}
  const demand=JSON.stringify([...new Set(visible)].sort());if(demand!==lastDemand){lastDemand=demand;emit('visible',{ids:JSON.parse(demand)})}
  relatedRevision.value++;reportOcclusion();
}
function referenceVisible(model:BookmarkBlockModel){
  if(!std)return false;const b=model.elementBound,v=gfx().viewport,[x,y]=v.toViewCoord(b.x,b.y),w=b.w*v.zoom,h=b.h*v.zoom;
  return !geometry.detail&&y+h>0&&y<innerHeight&&x+w>0&&x<innerWidth&&(x<geometry.left||x+w>geometry.right||y+h>geometry.bottom);
}
function addReferences(items:Reference[],restore=false){
  for(const item of items)refs.set(item.id,item);
  if(activeDoc!==doc){previewRevision.value++;return}
  const surface=gfx().surface!,existing=new Set(sourceModels().map(model=>(model.props as any).zsReference));let n=existing.size,index=generateKeyBetweenV2(null,null);
  for(const r of items){
    if(seenReferences.has(r.id)&&!restore)continue;seenReferences.add(r.id);
    if(existing.has(r.id))continue;existing.add(r.id);
    const singleColumn=geometry.left<240,side=singleColumn?1:n%2,row=singleColumn?n++:Math.floor(n++/2);
    const width=Math.max(300,Math.min(360,(side?innerWidth-geometry.right:geometry.left)-32));
    const height=width*390/364;
    const [x,y]=gfx().viewport.toModelCoord(side?geometry.right+16:16,35+row*(height+24));index=generateKeyBetweenV2(index,null);
    store!.addBlock('affine:bookmark',{zsReference:r.id,url:r.source,title:r.title,style:'vertical',xywh:JSON.stringify([x,y,width,height]),index},surface.id);
  }
  previewRevision.value++;
}
function registerEditingKeysOnly(scope:BlockStdScope){
  const bind=scope.event.bindHotkey.bind(scope.event);
  // Filter registration rather than DOM input. Keep native editing/navigation.
  scope.event.bindHotkey=(keymap,options)=>bind(Object.fromEntries(Object.entries(keymap).filter(([key])=>{
    const last=key.split('-').at(-1)!.toLowerCase();
    return ['backspace','delete','enter','escape','tab','space','arrowup','arrowdown','arrowleft','arrowright','home','end','pageup','pagedown'].includes(last)
      ||(/mod|ctrl|meta/i.test(key)&&['a','c','v','x','z','y','b','i','u'].includes(last));
  })),options);
}
async function initialize(message:any){
  await settle();
  dispose();session=message.session;setGeometry(message.geometry||qtGeometry);selectedSource='';
  const extensions=[...storeManager.get('store'),sourceSchema];
  if(message.state?.native){const restored=await restorePackage(JSON.stringify(message.state.native),extensions,true);workspace=restored.workspace;doc=restored.document;}
  else {workspace=new PackageWorkspace(extensions);doc=workspace.createDoc()}
  store=doc.getStore();store.load();
  if(!store.root){const page=store.addBlock('affine:page',{title:new Text('Linked context')});store.addBlock('affine:surface',{},page);}
  seenReferences=new Set(message.state?.referenceIds||[]);
  readOnly=!!message.readOnly;documentViews=message.state?.documentViews||{};
  if(message.state?.viewport)documentViews[doc.id]=message.state.viewport;
  for(const reference of message.references||[])refs.set(reference.id,reference);
  const observeDocs=()=>{
    for(const current of workspace!.docs.values())if(!observedDocs.has(current)){
      observedDocs.add(current);current.spaceDoc.on('update',()=>{changed();scheduleView()});
    }
    changed();
  };
  workspace.doc.on('update',()=>{queueMicrotask(observeDocs)});
  workspaceSubscriptions.push(workspace.slots.docListUpdated.subscribe(()=>queueMicrotask(observeDocs)));
  observeDocs();await mountDocument(doc);store!.readonly=false;addReferences([...refs.values()]);store!.readonly=readOnly;store!.resetHistory();updateView();changed();
}
function rememberView(){if(activeDoc&&std){const view=gfx().viewport;documentViews[activeDoc.id]={zoom:view.zoom,centerX:view.centerX,centerY:view.centerY}}}
async function openDocument(id:string){
  const next=workspace?.getDoc(id);if(!next)throw new Error('This internal board document is unavailable.');
  await settle();rememberView();detachView();await mountDocument(next);changed();
}
async function mountDocument(next:PackageDoc){
  activeDoc=next;store=next.getStore();store.load();store.readonly=readOnly;
  store.get(ImageProxyService).setImageProxyURL('');
  std=new BlockStdScope({store,extensions:[...nativeViewExtensions,
    {setup(di){di.override(BlockViewIdentifier('affine:bookmark'),()=>((model:any)=>isSource(model)&&model.parent?.flavour==='affine:surface'?literal`zeroslack-source-bookmark`:model.parent?.flavour==='affine:surface'?literal`affine-edgeless-bookmark`:literal`affine-bookmark`))}},
    sourceToolbar,DocModeExtension(new LinkedMode()),EditorSettingExtension({setting$:signal({})})]});
  registerEditingKeysOnly(std);
  if(!adaptedStores.has(store)){
    adaptedStores.add(store);const getTransformer=store.getTransformer.bind(store);
    store.getTransformer=(...args)=>{const job=getTransformer(...args);
      for(const name of ['snapshotToSlice','snapshotToBlock','snapshotToDoc'] as const){const convert=(job[name] as any).bind(job);(job as any)[name]=(...parameters:any[])=>track(convert(...parameters))}return job};
  }
  const copy=std.clipboard.copy,paste=std.clipboard.paste,pasteBlock=std.clipboard.pasteBlockSnapshot;
  std.clipboard.copy=(...args)=>track(copy(...args));std.clipboard.paste=(...args)=>track(paste(...args));
  std.clipboard.pasteBlockSnapshot=(...args)=>track(pasteBlock(...args));
  std.clipboard.use(({assetsManager})=>{uploadingAssets.add(assetsManager);return()=>uploadingAssets.delete(assetsManager)});
  std.get(LinkPreviewServiceIdentifier).query=async url=>({title:url,description:'Open this link in its source application.'});
  const host=std.render();root.append(host);await host.updateComplete;
  await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
  const v=documentViews[next.id];gfx().viewport.setViewport(v?.zoom||1,v?[v.centerX,v.centerY]:[innerWidth/2,innerHeight/2]);
  subscriptions.push(gfx().viewport.viewportUpdated.subscribe(()=>{changed();scheduleView()}));
  subscriptions.push(gfx().selection.slots.updated.subscribe(()=>{
    const model=gfx().selection.selectedElements.find(isSource) as BookmarkBlockModel|undefined,r=model&&sourceOf(model);
    if(r){selectedSource=r.id;emit('select',{id:r.id})}else selectedSource='';relatedRevision.value++;scheduleView();
  }));
  const refSlots=std.getOptional(RefNodeSlotsProvider);
  if(refSlots)subscriptions.push(refSlots.docLinkClicked.subscribe(({pageId})=>{pending=pending.then(()=>openDocument(pageId)).catch(error=>emit('error',{error:String(error)}))}));
  initialized=true;emit('initialized',{engine:'BlockSuite 0.22.4 Edgeless',cards:sourceModels().length});
  emit('internal-document',{primary:next===doc,title:next.meta?.title||'Linked board document'});updateView();
}
async function command(name:string,request=''){
  if(!initialized)return;
  if(name==='save')await save(true,request);else if(name==='undo')store!.undo();else if(name==='redo')store!.redo();
  else if(name==='primaryDocument')await openDocument(doc!.id);
  else if(name==='restoreReferences'&&!store!.readonly){addReferences([...refs.values()],true);changed();scheduleView()}
}
function reportOcclusion(){
  if(!initialized)return;const rectangles:number[][]=[];
  function visit(scope:Document|ShadowRoot|Element){
    for(const element of scope.querySelectorAll<HTMLElement>('*')){
      if(element.shadowRoot){if(!observedRoots.has(element.shadowRoot)){observedRoots.add(element.shadowRoot);popupObserver.observe(element.shadowRoot,{childList:true,subtree:true})}visit(element.shadowRoot)}
      // The native menu renders its items in a slotted content div. Include the
      // whole panel in Qt's airspace hole and bound its scroll area to this view.
      if(element.localName==='editor-menu-action'&&element.parentElement){
        const panel=element.parentElement,r=panel.getBoundingClientRect();
        if(r.width&&r.height){panel.style.maxHeight=`${Math.max(100,innerHeight-Math.max(0,r.y)-12)}px`;panel.style.overflowY='auto';
          rectangles.push([Math.floor(r.x)-8,Math.floor(r.y)-8,Math.ceil(r.width)+16,Math.ceil(Math.min(r.height,innerHeight-r.y))+16]);}
      }
      if(!['editor-toolbar','affine-toolbar','edgeless-toolbar-widget','affine-edgeless-zoom-toolbar-widget','editor-menu-content','affine-menu','affine-context-menu','affine-modal'].includes(element.localName)&&!['dialog','menu'].includes(element.getAttribute('role')||''))continue;
      const rect=element.getBoundingClientRect(),style=getComputedStyle(element);
      if(rect.width>0&&rect.height>0&&style.visibility!=='hidden'&&style.display!=='none'&&rect.width<innerWidth*.98&&rect.height<innerHeight*.95)
        rectangles.push([Math.floor(rect.x)-4,Math.floor(rect.y)-4,Math.ceil(rect.width)+8,Math.ceil(rect.height)+8]);
    }
  }
  visit(document);const value=JSON.stringify([innerWidth,innerHeight,rectangles]);if(value!==lastOcclusion){lastOcclusion=value;emit('occlusion',{rectangles,width:innerWidth,height:innerHeight})}
}
let popupTimer=0;const observePopups=()=>{clearTimeout(popupTimer);popupTimer=window.setTimeout(reportOcclusion,70)};
let observedRoots=new WeakSet<ShadowRoot>();const popupObserver=new MutationObserver(observePopups);
popupObserver.observe(document.body,{childList:true,subtree:true});
document.addEventListener('pointerup',observePopups,true);document.addEventListener('contextmenu',observePopups,true);
window.addEventListener('resize',()=>{setGeometry(qtGeometry);emit('metrics',{dpr:devicePixelRatio});scheduleView();observePopups()});
window.zeroslackWriteClipboard=async items=>{
  const value:Record<string,string>={};for(const [type,blob] of Object.entries(items)){
    if(blob.size>24*1024*1024)throw new Error('The clipboard item exceeds 24 MiB.');
    if(type==='text/plain')value.text=await blob.text();else if(type==='text/html')value.html=await blob.text();else if(type==='image/png')value.image=bytesToBase64(new Uint8Array(await blob.arrayBuffer()));
  }emit('write-clipboard',{value});
};
async function receive(m:any){
  if(m.type==='initialize'){await initialize(m);return}if(m.session!==session||!initialized)return;
  if(m.type==='geometry'){setGeometry(m.geometry);updateView()}
  if(m.type==='preview'){previews.set(m.source,m.preview);previewRevision.value++}
  if(m.type==='release'){for(const key of m.sources)previews.delete(key);previewRevision.value++}
  if(m.type==='references'){refs.clear();addReferences(m.references);changed();scheduleView()}
  if(m.type==='command')await command(m.name,m.request);
  if(m.type==='focus'){selectedSource=m.id||'';relatedRevision.value++;scheduleView()}
  if(m.type==='theme'){document.documentElement.classList.toggle('dark',m.dark);document.documentElement.dataset.theme=m.dark?'dark':'light'}
  if(m.type==='inspect')emit('inspection',{cards:sourceModels().length,visible:JSON.parse(lastDemand||'[]'),wires:wires.size,
    elements:gfx().gfxElements.map(e=>({id:e.id,type:(e as any).type||e.flavour,xywh:e.xywh})),viewport:{width:innerWidth,height:innerHeight,zoom:gfx().viewport.zoom},
    engine:!!root.querySelector('affine-edgeless-root'),nativeToolbar:!!root.querySelector('edgeless-toolbar-widget'),undo:store!.canUndo,redo:store!.canRedo});
}
window.chrome.webview.addEventListener('message',({data:m})=>{pending=pending.then(()=>receive(m)).catch(error=>emit('error',{error:String(error),stack:error?.stack}))});
window.addEventListener('error',e=>emit('error',{error:e.message,stack:e.error?.stack}));
window.addEventListener('unhandledrejection',e=>emit('error',{error:String(e.reason),stack:e.reason?.stack}));
emit('ready',{dpr:devicePixelRatio});
