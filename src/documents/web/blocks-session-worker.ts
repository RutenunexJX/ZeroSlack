import * as Y from 'yjs';

type Client={port:MessagePort;view:string};
type Entry={manager:Y.UndoManager;item:Y.StackItem};
type Group={view:string;time:number;entries:Entry[]};
const clients=new Map<MessagePort,Client>();
const documents={root:new Y.Doc(),document:new Y.Doc()};
const managers=Object.values(documents).map(doc=>new Y.UndoManager(doc,{captureTimeout:0,trackedOrigins:new Set()}));
const undo:Group[]=[],redo:Group[]=[];
let group:Group|undefined;
let collected:Entry[]|undefined;
let manifest:any;
let revision=0;
let origin='';
const assets=new Map<string,any>();
function encode(bytes:Uint8Array){let result='';for(let i=0;i<bytes.length;i+=32768)result+=String.fromCharCode(...bytes.subarray(i,i+32768));return btoa(result)}
function decode(value:string){return Uint8Array.from(atob(value),c=>c.charCodeAt(0))}
function broadcast(message:any){for(const client of clients.values())client.port.postMessage(message)}
function history(){broadcast({type:'history',canUndo:!!undo.length,canRedo:!!redo.length})}
for(const manager of managers)manager.on('stack-item-added',({stackItem})=>{
  const entry={manager,item:stackItem};
  if(collected)collected.push(entry);
  else if(group){if(!group.entries.length)undo.push(group);group.entries.push(entry)}
});
for(const [scope,doc] of Object.entries(documents))doc.on('update',(update:Uint8Array)=>{
  if(manifest)broadcast({type:'update',scope,update,revision:++revision,view:origin});
});
function changeHistory(name:'undo'|'redo'){
  const source=name==='undo'?undo:redo,destination=name==='undo'?redo:undo;
  const step=source.pop();if(!step)return;
  collected=[];
  try{
    for(const entry of [...step.entries].reverse()){
      const stack=name==='undo'?entry.manager.undoStack:entry.manager.redoStack;
      if(stack.at(-1)!==entry.item)throw new Error('The shared block history is inconsistent.');
      entry.manager[name]();
    }
    if(collected.length)destination.push({view:step.view,time:0,entries:collected});
  }finally{collected=undefined;group=undefined}
}
function packageValue(){return {...manifest,root:encode(Y.encodeStateAsUpdate(documents.root)),state:encode(Y.encodeStateAsUpdate(documents.document)),attachments:[...assets.values()]}}
(self as any).onconnect=({ports}:MessageEvent)=>{
  const port=ports[0];
  port.onmessage=({data})=>{
    try{
      if(data.type==='leave'){clients.delete(port);port.close();if(!clients.size)(self as any).close();return}
      if(data.type==='join'){
        if(clients.has(port))throw new Error('This view already joined its block session.');
        if(!manifest){
          const value=JSON.parse(data.content);
          Y.applyUpdate(documents.root,decode(value.root));Y.applyUpdate(documents.document,decode(value.state));
          for(const asset of value.attachments)assets.set(asset.id,asset);
          manifest=value;
        }
        clients.set(port,{port,view:data.view});for(const manager of managers)manager.addTrackedOrigin(data.view);
        port.postMessage({request:data.request,content:JSON.stringify(packageValue()),revision});history();return;
      }
      const client=clients.get(port);if(!client||client.view!==data.view)throw new Error('Unknown block view.');
      if(data.type==='forget'){for(const [key,value] of clients)if(value.view===data.target){clients.delete(key);key.close()}return}
      origin=client.view;
      if(data.type==='edit'){
        const doc=documents[data.scope as keyof typeof documents];if(!doc)throw new Error('Unknown block update scope.');
        const now=Date.now();
        if(!group||group.view!==origin||data.boundary||now-group.time>500)group={view:origin,time:now,entries:[]};
        group.time=now;redo.length=0;for(const manager of managers)manager.clear(false,true);
        Y.applyUpdate(doc,data.update,origin);history();
      }else if(data.type==='asset'){
        if(!assets.has(data.asset.id)){assets.set(data.asset.id,data.asset);broadcast({type:'asset',asset:data.asset,revision:++revision,view:origin})}
      }else if(data.type==='undo'||data.type==='redo'){changeHistory(data.type);history()}
      else if(data.type!=='fence')throw new Error('Unknown block transaction.');
      if(data.request)port.postMessage({request:data.request,revision});
    }catch(error){port.postMessage({request:data.request,error:String(error)})}
    finally{origin=''}
  };
  port.start();
};
