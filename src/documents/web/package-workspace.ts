import * as Y from 'yjs';
import {Subject} from 'rxjs';
import {Awareness} from 'y-protocols/awareness.js';
import {AwarenessStore, StoreContainer, nanoid, type Doc, type Workspace, type WorkspaceMeta,
  type DocMeta, type DocsPropertiesMeta, type ExtensionType, type GetStoreOptions, type YBlock, type Store} from '@blocksuite/affine/store';
import {BlobEngine, MemoryBlobSource} from '@blocksuite/affine/sync';
import {NoopLogger} from '@blocksuite/affine/global/utils';

class PackageMeta implements WorkspaceMeta {
  readonly docMetaAdded=new Subject<string>();
  readonly docMetaRemoved=new Subject<string>();
  readonly docMetaUpdated=new Subject<void>();
  private readonly values:Y.Map<DocMeta>;
  private readonly options:Y.Map<DocsPropertiesMeta>;
  constructor(doc:Y.Doc){this.values=doc.getMap('documentMeta');this.options=doc.getMap('properties')}
  get docMetas(){return [...this.values.values()]}
  get docs(){return this.docMetas}
  get properties(){return this.options.get('value')??{}}
  initialize(){}
  getDocMeta(id:string){return this.values.get(id)}
  addDocMeta(props:DocMeta){if(this.values.has(props.id))throw new Error('Duplicate document identity');this.values.set(props.id,props);this.docMetaAdded.next(props.id);this.docMetaUpdated.next()}
  setDocMeta(id:string,props:Partial<DocMeta>){const old=this.values.get(id);if(!old)throw new Error('Missing document identity');this.values.set(id,{...old,...props,id});this.docMetaUpdated.next()}
  removeDocMeta(id:string){this.values.delete(id);this.docMetaRemoved.next(id);this.docMetaUpdated.next()}
  setProperties(value:DocsPropertiesMeta){this.options.set('value',value);this.docMetaUpdated.next()}
  dispose(){this.docMetaAdded.complete();this.docMetaRemoved.complete();this.docMetaUpdated.complete()}
}

export class PackageDoc implements Doc {
  readonly spaceDoc:Y.Doc;
  readonly rootDoc:Y.Doc;
  readonly yBlocks:Y.Map<YBlock>;
  readonly awarenessStore:AwarenessStore;
  private readonly stores:StoreContainer;
  private readonly liveStores=new Set<Store>();
  ready=false;
  loaded=true;
  constructor(readonly id:string,readonly workspace:PackageWorkspace,bytes?:Uint8Array){
    this.rootDoc=workspace.doc;
    this.spaceDoc=new Y.Doc({guid:id});
    if(bytes)Y.applyUpdate(this.spaceDoc,bytes);
    this.yBlocks=this.spaceDoc.getMap('blocks');
    this.awarenessStore=new AwarenessStore(new Awareness(this.spaceDoc));
    this.stores=new StoreContainer(this);
  }
  get meta(){return this.workspace.meta.getDocMeta(this.id)}
  load(init?:()=>void){if(!this.ready){this.ready=true;init?.()}}
  getStore(options:GetStoreOptions={}){const store=this.stores.getStore({...options,id:options.id??this.id,extensions:[...this.workspace.extensions,...(options.extensions??[])]});this.liveStores.add(store);return store}
  removeStore=(options:Parameters<StoreContainer['removeStore']>[0])=>this.stores.removeStore(options);
  clear(){this.yBlocks.clear()}
  remove(){this.workspace.removeDoc(this.id)}
  dispose(){for(const store of this.liveStores)store.dispose();this.liveStores.clear();this.awarenessStore.destroy();this.spaceDoc.destroy();this.ready=false;this.loaded=false}
}

export class PackageWorkspace implements Workspace {
  readonly id:string;
  readonly idGenerator=nanoid;
  readonly doc:Y.Doc;
  readonly meta:PackageMeta;
  readonly docs=new Map<string,PackageDoc>();
  readonly slots={docListUpdated:new Subject<void>()};
  readonly blobs=new MemoryBlobSource();
  readonly blobSync=new BlobEngine(this.blobs,[],new NoopLogger());
  constructor(readonly extensions:ExtensionType[],id=nanoid(),rootBytes?:Uint8Array){
    this.id=id;this.doc=new Y.Doc({guid:id});if(rootBytes)Y.applyUpdate(this.doc,rootBytes);
    this.meta=new PackageMeta(this.doc);
    this.meta.docMetaUpdated.subscribe(()=>this.slots.docListUpdated.next());
  }
  createDoc(id=nanoid()):PackageDoc{
    if(this.docs.has(id))throw new Error('Duplicate document identity');
    if(!this.meta.getDocMeta(id))this.meta.addDocMeta({id,title:'',tags:[],createDate:Date.now()});
    const doc=new PackageDoc(id,this);this.docs.set(id,doc);return doc;
  }
  restoreDoc(id:string,bytes:Uint8Array){
    if(this.docs.has(id)||!this.meta.getDocMeta(id))throw new Error('Invalid document identity');
    const doc=new PackageDoc(id,this,bytes);this.docs.set(id,doc);return doc;
  }
  getDoc(id:string){return this.docs.get(id)??null}
  removeDoc(id:string){const doc=this.docs.get(id);if(!doc)return;this.docs.delete(id);doc.dispose();this.meta.removeDocMeta(id)}
  dispose(){for(const doc of this.docs.values())doc.dispose();this.docs.clear();this.blobSync.stop();this.meta.dispose();this.slots.docListUpdated.complete();this.doc.destroy();this.blobs.map.clear()}
}

export function bytesToBase64(bytes:Uint8Array){let text='';for(let i=0;i<bytes.length;i+=32768)text+=String.fromCharCode(...bytes.subarray(i,i+32768));return btoa(text)}
export function base64ToBytes(value:string){return Uint8Array.from(atob(value),c=>c.charCodeAt(0))}

export function validateDocument(workspace:PackageWorkspace,document:PackageDoc){
  const allowed=new Set(['affine:page','affine:surface','affine:note','affine:paragraph','affine:list','affine:code','affine:image','affine:table','affine:divider','affine:latex','affine:callout']);
  const versions=document.getStore().schema.versions;
  const parents=new Map<string,string>();const roots:string[]=[];
  for(const [id,block] of document.yBlocks){
    const flavour=String(block.get('sys:flavour'));
    if(!allowed.has(flavour))throw new Error(`Unsupported block type: ${flavour}. Undo this insertion or use the application that created the document.`);
    if(block.get('sys:id')!==id||block.get('sys:version')!==versions[flavour])throw new Error(`Unsupported or invalid ${flavour} block version. The original package is preserved.`);
    if(flavour==='affine:page')roots.push(id);
    const children=block.get('sys:children');
    if(!(children instanceof Y.Array))throw new Error(`Invalid children in block ${id}.`);
    for(const child of children){
      if(typeof child!=='string'||!document.yBlocks.has(child)||parents.has(child))throw new Error('The block tree has a missing or shared child.');
      parents.set(child,id);
    }
    const sourceId=block.get('prop:sourceId');
    if(sourceId&&!workspace.blobs.map.has(String(sourceId)))throw new Error(`Missing attachment: ${sourceId}`);
  }
  if(roots.length!==1||parents.has(roots[0]))throw new Error('The package must contain one page root.');
  const pending=[roots[0]],seen=new Set<string>();
  while(pending.length){const id=pending.pop()!;if(seen.has(id))throw new Error('The block tree contains a cycle.');seen.add(id);pending.push(...document.yBlocks.get(id)!.get('sys:children').toArray())}
  if(seen.size!==document.yBlocks.size)throw new Error('The block tree contains unreachable content.');
}

export async function snapshotPackage(workspace:PackageWorkspace,document:PackageDoc){
  validateDocument(workspace,document);
  const attachments=[];
  for(const [id,blob] of workspace.blobs.map){
    const bytes=new Uint8Array(await blob.arrayBuffer());
    const hash=new Uint8Array(await crypto.subtle.digest('SHA-256',bytes));
    attachments.push({id,mime:blob.type,size:bytes.length,sha256:[...hash].map(n=>n.toString(16).padStart(2,'0')).join(''),data:bytesToBase64(bytes)});
  }
  return {format:'zeroslack.blocks',version:1,engine:'blocksuite-0.22.4',workspaceId:workspace.id,documentId:document.id,
    root:bytesToBase64(Y.encodeStateAsUpdate(workspace.doc)),state:bytesToBase64(Y.encodeStateAsUpdate(document.spaceDoc)),attachments};
}

export async function restorePackage(text:string,extensions:ExtensionType[]) {
  const value=JSON.parse(text);
  if(value.format!=='zeroslack.blocks'||value.version!==1||value.engine!=='blocksuite-0.22.4')throw new Error('Unsupported block document format or engine.');
  if(typeof value.workspaceId!=='string'||!value.workspaceId||typeof value.documentId!=='string'||!value.documentId||!Array.isArray(value.attachments))throw new Error('Incomplete block document.');
  const workspace=new PackageWorkspace(extensions,value.workspaceId,base64ToBytes(value.root));
  try{
    if(workspace.meta.docMetas.length!==1||!workspace.meta.getDocMeta(value.documentId))throw new Error('The package must contain exactly one document.');
    for(const attachment of value.attachments){
      if(workspace.blobs.map.has(attachment.id))throw new Error('Duplicate attachment.');
      const bytes=base64ToBytes(attachment.data);
      const hash=new Uint8Array(await crypto.subtle.digest('SHA-256',bytes));
      if(bytes.length!==attachment.size||[...hash].map(n=>n.toString(16).padStart(2,'0')).join('')!==attachment.sha256)throw new Error(`Damaged attachment: ${attachment.id}`);
      workspace.blobs.map.set(attachment.id,new Blob([bytes],{type:attachment.mime}));
    }
    const document=workspace.restoreDoc(value.documentId,base64ToBytes(value.state));
    validateDocument(workspace,document);
    return {workspace,document};
  }catch(error){workspace.dispose();throw error}
}
