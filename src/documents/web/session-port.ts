// A session outlives any one view. Only the worker owns document history.
export class SessionPort {
  private readonly worker:SharedWorker;
  private readonly pending=new Map<string,{resolve:(value:any)=>void;reject:(error:Error)=>void;timer:number}>();
  private closed=false;
  readonly view:string;
  constructor(session:string,private readonly event:(value:any)=>void,private readonly failed:(error:Error)=>void,view?:string){
    this.view=view||crypto.randomUUID();
    this.worker=new SharedWorker(new URL('./session-worker.js',location.href),{name:session});
    this.worker.port.onmessage=({data})=>{
      if(data.request){const pending=this.pending.get(data.request);if(!pending)return;
        this.pending.delete(data.request);clearTimeout(pending.timer);
        data.error?pending.reject(new Error(data.error)):pending.resolve(data);
      }else if(data.error)this.failed(new Error(data.error));else this.event(data);
    };
    this.worker.onerror=()=>this.fail(new Error('The shared document session could not be loaded. Reopen the document after restoring its local resources.'));
    this.worker.port.start();
  }
  request(type:string,data:Record<string,unknown>={}):Promise<any>{
    if(this.closed)return Promise.reject(new Error('The document session is closed.'));
    const request=crypto.randomUUID();
    return new Promise((resolve,reject)=>{
      const timer=window.setTimeout(()=>{this.pending.delete(request);reject(new Error('The shared document session did not respond. The document remains open.'))},25000);
      this.pending.set(request,{resolve,reject,timer});this.worker.port.postMessage({type,request,view:this.view,...data});
    });
  }
  send(type:string,data:Record<string,unknown>={}){if(!this.closed)this.worker.port.postMessage({type,view:this.view,...data})}
  private fail(error:Error){for(const value of this.pending.values()){clearTimeout(value.timer);value.reject(error)}this.pending.clear();this.failed(error)}
  close(){if(this.closed)return;this.send('leave');this.closed=true;this.worker.port.close();for(const value of this.pending.values()){clearTimeout(value.timer);value.reject(new Error('Document view closed.'))}this.pending.clear()}
}
