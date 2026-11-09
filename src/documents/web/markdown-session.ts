import * as json1 from 'ot-json1';
import * as text from 'ot-text-unicode';
import {SessionPort} from './session-port';

json1.type.registerSubtype(text.type);
// One outstanding transaction plus a composed buffer preserves local typing
// while remote updates are transformed against both, in the worker's order.
export class MarkdownSession {
  private readonly port:SessionPort;
  private revision=0;
  private closed=false;
  private outstanding:{id:string;op:any}|undefined;
  private buffer:any=null;
  private boundary=false;
  private error:Error|undefined;
  private waiters:Array<()=>void>=[];
  constructor(session:string,private readonly flush:()=>void,private readonly apply:(op:any)=>void,
    private readonly changed:(revision:number)=>void,private readonly failed:(error:Error)=>void,view?:string){
    this.port=new SessionPort(session,event=>this.receive(event),error=>this.fail(error),view);
  }
  async join(state:any){const value=await this.port.request('join',{state});this.revision=value.revision;return value}
  submit(op:any,boundary=false){
    if(!op)return;
    this.boundary||=boundary;
    if(this.outstanding)this.buffer=json1.type.compose(this.buffer,op);
    else this.send(op);
  }
  private send(op:any){
    const id=crypto.randomUUID();this.outstanding={id,op};
    this.port.request('edit',{id,op,base:this.revision,boundary:this.boundary}).catch(error=>this.fail(error));this.boundary=false;
  }
  private receive(event:any){
    if(event.type!=='update')return;
    try{
      this.flush();
      if(this.outstanding&&event.view===this.port.view&&event.id===this.outstanding.id){
        this.outstanding=undefined;this.revision=event.revision;
        if(this.buffer){const op=this.buffer;this.buffer=null;this.send(op)}
      }else{
        if(event.revision!==this.revision+1)throw new Error('Document update sequence is incomplete.');
        let remote=event.op;
        if(this.outstanding){const original=this.outstanding.op;
          this.outstanding.op=json1.type.transform(original,remote,'right');remote=json1.type.transform(remote,original,'left');
        }
        if(this.buffer){const original=this.buffer;
          this.buffer=json1.type.transform(original,remote,'right');remote=json1.type.transform(remote,original,'left');
        }
        this.revision=event.revision;if(remote)this.apply(remote);
      }
      this.changed(this.revision);
      // Pending edits still refer to their submission version; retain that log.
      if(!this.outstanding&&!this.buffer){this.port.send('seen',{revision:this.revision});for(const resolve of this.waiters.splice(0))resolve()}
    }catch(error){this.fail(error instanceof Error?error:new Error(String(error)))}
  }
  private fail(error:Error){if(this.closed)return;this.error=error;for(const resolve of this.waiters.splice(0))resolve();this.failed(error)}
  async settle(){
    this.flush();
    if(this.outstanding||this.buffer)await new Promise<void>(resolve=>{
      const timer=setTimeout(()=>this.fail(new Error('The document transaction has not completed. Retry saving.')),25000);
      this.waiters.push(()=>{clearTimeout(timer);resolve()});
    });
    if(this.error)throw this.error;
    await this.port.request('fence');
  }
  async command(name:'undo'|'redo'){await this.settle();await this.port.request(name)}
  detachView(view:string){this.port.send('forget',{target:view})}
  close(){this.closed=true;this.port.close()}
}
