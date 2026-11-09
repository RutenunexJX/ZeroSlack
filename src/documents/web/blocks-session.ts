import * as Y from 'yjs';
import {SessionPort} from './session-port';
import {PackageWorkspace,PackageDoc,base64ToBytes,bytesToBase64} from './package-workspace';

export class BlocksSession {
  private readonly port:SessionPort;
  private readonly pending=new Set<Promise<unknown>>();
  private workspace:PackageWorkspace|undefined;
  private document:PackageDoc|undefined;
  private error:Error|undefined;
  private closed=false;
  private queued:any[]=[];
  private boundary=true;
  private readonly remote={};
  private readonly boundaryHandler=()=>{this.boundary=true};
  constructor(session:string,private readonly changed:(revision:number)=>void,
    private readonly history:(undo:boolean,redo:boolean)=>void,private readonly failed:(error:Error)=>void,view?:string,
    private readonly preserveView?:()=>()=>void){
    this.port=new SessionPort(session,event=>this.receive(event),error=>this.fail(error),view);
    for(const name of ['pointerdown','paste','cut'])window.addEventListener(name,this.boundaryHandler,true);
  }
  join(content:string){return this.port.request('join',{content})}
  attach(workspace:PackageWorkspace,document:PackageDoc){
    this.workspace=workspace;this.document=document;
    for(const [scope,doc] of [['root',workspace.doc],['document',document.spaceDoc]] as const)doc.on('update',(update,origin)=>{
      if(origin===this.remote||this.closed)return;
      const boundary=this.boundary;this.boundary=false;this.track(this.port.request('edit',{scope,update,boundary}));
    });
    for(const event of this.queued.splice(0))this.receive(event);
    const set=workspace.blobs.set.bind(workspace.blobs);
    workspace.blobs.set=(id,blob)=>this.track((async()=>{
      const data=new Uint8Array(await blob.arrayBuffer());
      const hash=new Uint8Array(await crypto.subtle.digest('SHA-256',data));
      await this.port.request('asset',{asset:{id,mime:blob.type,size:data.length,data:bytesToBase64(data),sha256:[...hash].map(n=>n.toString(16).padStart(2,'0')).join('')}});
      return set(id,blob);
    })());
  }
  private track<T>(promise:Promise<T>){
    this.pending.add(promise);promise.then(()=>this.pending.delete(promise),error=>{this.pending.delete(promise);this.fail(error)});return promise;
  }
  private fail(error:Error){if(this.closed)return;this.error=error;this.failed(error)}
  private receive(event:any){
    if(!this.workspace){this.queued.push(event);return}
    try{
      if(event.type==='history'){this.history(event.canUndo,event.canRedo);return}
      if(event.type==='update'){
        const doc=event.scope==='root'?this.workspace?.doc:this.document?.spaceDoc;
        if(!doc)throw new Error('The block view is not initialized.');
        const restore=this.preserveView?.();Y.applyUpdate(doc,event.update,this.remote);restore?.();
      }else if(event.type==='asset'){
        const asset=event.asset;this.workspace?.blobs.map.set(asset.id,new Blob([base64ToBytes(asset.data)],{type:asset.mime}));
      }else return;
      this.changed(event.revision);
    }catch(error){this.fail(error instanceof Error?error:new Error(String(error)))}
  }
  async settle(){while(this.pending.size)await Promise.all([...this.pending]);if(this.error)throw this.error;await this.port.request('fence')}
  async command(name:'undo'|'redo'){await this.settle();await this.port.request(name)}
  detachView(view:string){this.port.send('forget',{target:view})}
  close(){this.closed=true;for(const name of ['pointerdown','paste','cut'])window.removeEventListener(name,this.boundaryHandler,true);this.port.close()}
}
