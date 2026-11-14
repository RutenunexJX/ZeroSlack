export interface EditorAdapter {
  initialize(content:string,options?:Record<string,any>):Promise<{protected?:boolean;notice?:string}|void>;
  snapshot():Promise<string|{content:string;images?:string[]}>;
  flush():void;
  settle?():Promise<void>;
  command(name:string,clipboard?:ClipboardPayload):Promise<void>|void;
  dispose():void;
  detachView?(view:string):void;
  viewState?():Record<string,unknown>;
  restoreViewState?(state:Record<string,any>):void;
  theme?(dark:boolean):void;
  appearance?(value:{family:string;size:number;lineHeight:number}):void;
}
export interface ClipboardPayload {text:string;html:string;image?:string}
function applyUiTheme(value:Record<string,unknown>|undefined){
  if(!value)return;
  const properties:Record<string,string[]>={
    panel:['--ui-panel','--editor-bg-color'],
    subtle:['--ui-subtle','--float-bg-color'],
    text:['--ui-text','--editor-color','--editor-color-80'],
    secondary:['--ui-secondary','--editor-color-50','--icon-color'],
    muted:['--ui-muted','--editor-color-30'],
    border:['--ui-border','--float-border-color','--table-border-color'],
    hover:['--ui-hover','--float-hover-color'],
    accent:['--ui-accent','--link-color'],
  };
  for(const [key,names] of Object.entries(properties)){
    const color=value[key];if(typeof color!=='string'||!/^#[0-9a-f]{6}$/i.test(color))continue;
    for(const name of names)document.documentElement.style.setProperty(name,color);
  }
  if(typeof value.shellFont==='string')document.documentElement.style.setProperty('--ui-font',value.shellFont);
}
export function pasteEvent(value:ClipboardPayload){
  const clipboardData=new DataTransfer();
  if(value.text)clipboardData.setData('text/plain',value.text);
  if(value.html)clipboardData.setData('text/html',value.html);
  if(value.image)clipboardData.items.add(new File([Uint8Array.from(atob(value.image),c=>c.charCodeAt(0))],'clipboard.png',{type:'image/png'}));
  return new ClipboardEvent('paste',{clipboardData,bubbles:true,cancelable:true});
}
declare global { interface Window { chrome:{webview:{postMessage(message:unknown):void;addEventListener(name:string,callback:(event:{data:any})=>void):void}}; zeroslackWriteClipboard:(items:Record<string,Blob>)=>Promise<void>; } }

export class DocumentBridge {
  private session='';
  private documentId='';
  private revision=0;
  private initialized=false;
  private frozen=false;
  private pausedOperations=0;
  private composing=false;
  private disposed=false;
  private initializing=false;
  private barrier=0;
  private viewTimer:number|undefined;
  private changeTimer:number|undefined;
  private readonly pending=new Set<Promise<unknown>>();
  private readonly requests=new Map<string,{resolve:(value:any)=>void;reject:(error:Error)=>void;timer:number}>();
  constructor(private readonly adapter:EditorAdapter) {
    for(const name of ['scroll','selectionchange','focusout'])window.addEventListener(name,()=>this.viewChanged(),true);
    window.addEventListener('compositionstart',()=>{this.composing=true},true);
    window.addEventListener('compositionend',()=>{this.composing=false},true);
    window.addEventListener('wheel',event=>{
      if(event.ctrlKey&&event.shiftKey){
        event.preventDefault();event.stopImmediatePropagation();
        if(this.initialized&&event.deltaY)this.send({type:'font-zoom',steps:event.deltaY<0?1:-1});
      }
    },{capture:true,passive:false});
    window.addEventListener('keydown',event=>{
      if(this.frozen||this.pausedOperations){event.preventDefault();event.stopImmediatePropagation();return}
      if((event.ctrlKey||event.metaKey)&&!event.getModifierState('AltGraph')){
        const key=event.key.toLowerCase();
        if(event.altKey||!['a','c','x','v','z','y','b','i','u','arrowleft','arrowright','arrowup','arrowdown','home','end','backspace','delete','insert'].includes(key)){
          event.preventDefault();event.stopImmediatePropagation();
        }
      }
    },true);
    window.addEventListener('beforeinput',event=>{if(this.frozen||this.pausedOperations){event.preventDefault();event.stopImmediatePropagation()}},true);
    for(const name of ['pointerdown','mousedown','click','dblclick','paste','cut','drop'])
      window.addEventListener(name,event=>{if(this.frozen||this.pausedOperations){event.preventDefault();event.stopImmediatePropagation()}},true);
    window.addEventListener('beforeunload',()=>this.dispose());
    window.addEventListener('error',event=>this.report(String(event.error?.stack||event.message)));
    window.addEventListener('unhandledrejection',event=>this.report(String(event.reason?.stack||event.reason)));
    window.chrome.webview.addEventListener('message',event=>void this.receive(event.data));
    window.chrome.webview.postMessage({type:'hello'});
  }
  changed(sharedRevision?:number) {
    if(!this.initialized||this.initializing||this.disposed)return;
    this.revision=sharedRevision??this.revision+1;
    if(this.changeTimer===undefined)this.changeTimer=window.setTimeout(()=>{
      this.changeTimer=undefined;this.send({type:'changed',revision:this.revision});
    },16);
  }
  setRevision(value:number){this.revision=value}
  viewChanged(){
    if(!this.initialized||this.disposed||this.viewTimer!==undefined)return;
    this.viewTimer=window.setTimeout(()=>{this.viewTimer=undefined;if(this.initialized&&!this.disposed)this.send({type:'view-state',state:this.adapter.viewState?.()??{}})},50);
  }
  track<T>(promise:Promise<T>):Promise<T> {
    this.pending.add(promise);promise.then(()=>this.pending.delete(promise),()=>this.pending.delete(promise));return promise;
  }
  async pauseEditing<T>(operation:()=>Promise<T>):Promise<T>{
    this.pausedOperations++;
    try{return await operation()}finally{this.pausedOperations--}
  }
  request(type:string,data:Record<string,unknown>):Promise<any> {
    const request=crypto.randomUUID();
    return this.track(new Promise((resolve,reject)=>{
      const timer=window.setTimeout(()=>{this.requests.delete(request);reject(new Error('The host did not complete the attachment request.'))},20000);
      this.requests.set(request,{resolve,reject,timer});this.send({type,request,...data});
    }));
  }
  send(message:Record<string,unknown>) { if(!this.disposed)window.chrome.webview.postMessage({...message,session:this.session,document:this.documentId}); }
  report(message:string) { this.send({type:'error',message}); }
  private async settled() {
    const deadline=performance.now()+25000;
    while(this.composing||this.pending.size){
      if(performance.now()>deadline)throw new Error('Finish the current input or attachment operation, then save again.');
      await new Promise(resolve=>setTimeout(resolve,20));
    }
    // A task boundary drains compositionend/input and upstream queued operations even in hidden tabs.
    await new Promise(resolve=>setTimeout(resolve,0));this.adapter.flush();await this.adapter.settle?.();this.adapter.flush();
  }
  private async receive(message:any) {
    if(!message||typeof message!=='object'||this.disposed)return;
    if(message.type==='initialize'){
      if(this.initializing)return;
      ++this.barrier;this.initializing=true;this.initialized=false;this.session=message.session;this.documentId=message.document;
      this.revision=0;this.frozen=false;
      try{
        const result=await this.adapter.initialize(String(message.content??''),message);
        this.adapter.theme?.(!!message.dark);applyUiTheme(message.uiTheme);if(message.appearance?.size)this.adapter.appearance?.(message.appearance);this.initialized=true;
        if(message.viewState)this.adapter.restoreViewState?.(message.viewState);
        this.send({type:'ready',...result});this.viewChanged();
      }catch(error){this.report(String(error));}
      finally{this.initializing=false}
      return;
    }
    if(message.session!==this.session||message.document!==this.documentId)return;
    if(message.type==='dispose'){this.dispose();return}
    if(message.type==='detach-view'){this.adapter.detachView?.(message.view);return}
    if(message.type==='response'){
      const request=this.requests.get(message.request);if(!request)return;
      clearTimeout(request.timer);this.requests.delete(message.request);
      message.error?request.reject(new Error(message.error)):request.resolve(message);return;
    }
    if(message.type==='theme'){this.adapter.theme?.(!!message.dark);applyUiTheme(message.uiTheme);return}
    if(message.type==='appearance'){this.adapter.appearance?.(message.appearance);return}
    if(message.type==='release'){++this.barrier;this.frozen=false;return}
    if(!this.initialized)return;
    if(message.type==='restore-view'){this.adapter.restoreViewState?.(message.state??{});this.viewChanged();return}
    if(message.type==='prepare'){
      const session=this.session,barrier=++this.barrier;
      try{await this.settled();if(session!==this.session||barrier!==this.barrier)return;this.frozen=true;this.adapter.flush();await this.adapter.settle?.();
        if(session!==this.session||barrier!==this.barrier)return;
        this.send({type:'prepared',request:message.request,revision:this.revision});
      }catch(error){this.send({type:'snapshot-error',request:message.request,message:String(error)})}
      return;
    }
    if(message.type==='command'&&!this.frozen&&!this.pausedOperations){try{await this.track(Promise.resolve(this.adapter.command(message.command,message.clipboard)))}catch(error){this.report(String(error))}return}
    if(message.type==='snapshot'){
      const session=this.session;
      try{
        await this.settled();
        if(message.freeze){this.frozen=true;this.adapter.flush()}
        const before=this.revision;const value=await this.adapter.snapshot();
        if(session!==this.session)return;
        if(before!==this.revision)throw new Error('The document changed while preparing its snapshot. Retry saving.');
        this.send({type:'snapshot',request:message.request,revision:before,...(typeof value==='string'?{content:value}:value)});
      }catch(error){this.send({type:'snapshot-error',request:message.request,message:String(error)})}
    }
  }
  dispose(){if(this.disposed)return;this.disposed=true;++this.barrier;clearTimeout(this.viewTimer);clearTimeout(this.changeTimer);for(const value of this.requests.values()){clearTimeout(value.timer);value.reject(new Error('Document closed'))}this.requests.clear();this.adapter.dispose()}
}
