import * as json1 from 'ot-json1';
import * as text from 'ot-text-unicode';
import History from '@zeroslack/muya-history';

json1.type.registerSubtype(text.type);
type Client={port:MessagePort;view:string;seen:number};
const clients=new Map<MessagePort,Client>();
let state:any;
let revision=0;
let previousView='';
let action:{view:string;id?:string}|undefined;
const operations=new Map<number,any>();
const listeners:Array<(change:any)=>void>=[];
const model:any={canMutate:true,eventCenter:{on:(_name:string,callback:(change:any)=>void)=>listeners.push(callback)},editor:{
  jsonState:{getState:()=>structuredClone(state)},selection:{getSelection:()=>null},
  updateContents:apply,rebuildContents:apply
}};
const history=new History(model);
function apply(op:any){
  if(!op)return;
  const prevDoc=state;state=json1.type.apply(state,op);
  for(const callback of listeners)callback({op,prevDoc,doc:state,source:'user'});
  operations.set(++revision,op);
  for(const client of clients.values())client.port.postMessage({type:'update',op,revision,...action});
}
function prune(){
  const through=Math.min(...[...clients.values()].map(client=>client.seen));
  for(const version of operations.keys())if(version<=through)operations.delete(version);
}
(self as any).onconnect=({ports}:MessageEvent)=>{
  const port=ports[0];
  port.onmessage=({data})=>{
    try{
      if(data.type==='leave'){clients.delete(port);port.close();prune();if(!clients.size)(self as any).close();return}
      if(data.type==='join'){
        if(clients.has(port))throw new Error('This view already joined its document session.');
        if(state===undefined)state=data.state;
        clients.set(port,{port,view:data.view,seen:revision});
        port.postMessage({request:data.request,state,revision});return;
      }
      const client=clients.get(port);if(!client||client.view!==data.view)throw new Error('Unknown document view.');
      if(data.type==='seen'){client.seen=Math.max(client.seen,Math.min(revision,data.revision));prune();return}
      if(data.type==='forget'){for(const [key,value] of clients)if(value.view===data.target){clients.delete(key);key.close()}prune();return}
      action={view:client.view,id:data.id};
      if(data.type==='edit'){
        if(!Number.isInteger(data.base)||data.base<client.seen||data.base>revision)throw new Error('Stale document transaction.');
        let op=data.op;
        for(let version=data.base+1;version<=revision;version++){
          if(!operations.has(version))throw new Error('The document transaction is no longer available.');
          op=json1.type.transform(op,operations.get(version),'right');
        }
        if(previousView!==client.view||data.boundary)history.cutoff();
        previousView=client.view;
        if(op)apply(op);else port.postMessage({type:'update',op:null,revision,...action});
      }else if(data.type==='undo'||data.type==='redo'){
        history.cutoff();history[data.type]();previousView='';
      }else if(data.type!=='fence')throw new Error('Unknown document transaction.');
      if(data.request)port.postMessage({request:data.request,revision});
    }catch(error){port.postMessage({request:data.request,error:String(error)})}
    finally{action=undefined}
  };
  port.start();
};
