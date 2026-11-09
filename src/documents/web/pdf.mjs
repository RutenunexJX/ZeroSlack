import * as pdfjs from './vendor/build/pdf.mjs';
globalThis.pdfjsLib=pdfjs;
const {EventBus,PDFViewer,PDFLinkService,PDFFindController,LinkTarget}=await import('./vendor/web/pdf_viewer.mjs');
pdfjs.GlobalWorkerOptions.workerSrc=new URL('./vendor/build/pdf.worker.mjs',import.meta.url).href;
pdfjs.GlobalWorkerOptions.rendererSrc=new URL('./vendor/build/pdf.renderer.mjs',import.meta.url).href;
const container=document.querySelector('#viewerContainer');
const eventBus=new EventBus();
const links=new PDFLinkService({eventBus,externalLinkTarget:LinkTarget.BLANK});
const findController=new PDFFindController({eventBus,linkService:links});
const viewer=new PDFViewer({container,eventBus,linkService:links,findController,
  enableScripting:false,
  annotationMode:pdfjs.AnnotationMode.ENABLE,annotationEditorMode:pdfjs.AnnotationEditorType.DISABLE,
  maxCanvasPixels:16*1024*1024,imageResourcesPath:new URL('./vendor/web/images/',import.meta.url).href});
links.setViewer(viewer);
let session='',loading,transport,restore,disposed=false,generation=0;
let resizeFrame=0;
let lastViewState,resizeScrollPosition,resizing=false,changingView=false,viewportWidth=container.clientWidth,viewportHeight=container.clientHeight;
function beginViewChange(){
  // An explicit navigation supersedes the anchor captured before a resize.
  lastViewState=undefined;
  resizeScrollPosition=undefined;
  viewportWidth=container.clientWidth;viewportHeight=container.clientHeight;
}
const resizeObserver=new ResizeObserver(()=>{
  cancelAnimationFrame(resizeFrame);
  resizeFrame=requestAnimationFrame(()=>{
    if(disposed||!container.clientWidth||!container.clientHeight||!viewer.pdfDocument)return;
    const anchor=lastViewState;
    resizing=true;
    try{
      if(['auto','page-fit','page-width','page-height'].includes(viewer.currentScaleValue)){
        viewer.currentScaleValue=viewer.currentScaleValue;
      }
      if(anchor)viewer.scrollPageIntoView({pageNumber:anchor.page,
        destArray:[null,{name:'XYZ'},anchor.left,anchor.top,null],allowNegativeOffset:true});
      viewer.update();
    }finally{
      viewportWidth=container.clientWidth;viewportHeight=container.clientHeight;
      // A short final page may clamp the scroll range in a tall, narrow pane.
      // Keep the requested anchor until scrolling or explicit navigation changes it.
      resizeScrollPosition=anchor?{left:container.scrollLeft,top:container.scrollTop}:undefined;
      resizing=false;
    }
  });
});
resizeObserver.observe(container);
const pending=new Map();
const decode=value=>Uint8Array.from(atob(value),c=>c.charCodeAt(0));
const send=value=>{if(!disposed)window.chrome.webview.postMessage({...value,session})};
async function stop(){pending.clear();viewer.setDocument(null);links.setDocument(null);const previous=loading;loading=undefined;transport=undefined;await previous?.destroy()}
function error(value){send({type:'error',message:String(value)});void stop()}
class FileRanges extends pdfjs.PDFDataRangeTransport {
  requestDataRange(begin,end){
    if(!Number.isSafeInteger(begin)||!Number.isSafeInteger(end)||end<=begin||end>this.length){error('Invalid PDF file range.');return}
    const request=crypto.randomUUID();
    try{pending.set(request,{begin,end,received:0,bytes:new Uint8Array(end-begin)})}
    catch{error('This PDF requires more memory than is available.');return}
    send({type:'range',request,begin,end});
  }
  abort(){pending.clear()}
}
eventBus.on('pagesinit',()=>{
  beginViewChange();
  changingView=true;
  try{
    viewer.currentScaleValue='page-width';
    if(restore?.page)links.setHash('page='+restore.page+'&zoom='+encodeURIComponent(restore.scale||'page-width')+','+(restore.left||0)+','+(restore.top||0));
    restore=undefined;
  }finally{changingView=false}
  viewer.update();
});
eventBus.on('updateviewarea',({location})=>{
  if(resizing||changingView||container.clientWidth!==viewportWidth||container.clientHeight!==viewportHeight)return;
  if(resizeScrollPosition){
    if(container.scrollLeft===resizeScrollPosition.left&&container.scrollTop===resizeScrollPosition.top)return;
    resizeScrollPosition=undefined;
  }
  const page=viewer.currentPageNumber,view=viewer.getPageView(page-1);
  if(!view)return;
  const [left,top]=view.getPagePoint(container.scrollLeft-view.div.offsetLeft-view.div.clientLeft,container.scrollTop-view.div.offsetTop-view.div.clientTop);
  lastViewState={page,scale:location.scale,left,top};
  send({type:'state',state:lastViewState});
});
eventBus.on('pagerendered',({pageNumber,error:failure})=>{if(failure)error(failure);else send({type:'rendered',page:pageNumber})});
eventBus.on('updatefindmatchescount',({matchesCount})=>send({type:'matches',...matchesCount}));
window.chrome.webview.addEventListener('message',async({data})=>{
  try{
    if(data.type==='open'){
      const opening=++generation;session=data.session;lastViewState=undefined;await stop();if(opening!==generation)return;
      restore=data.state;document.documentElement.classList.toggle('dark',!!data.dark);
      const current=session;transport=new FileRanges(data.size,decode(data.initial),true,data.name);
      const task=pdfjs.getDocument({range:transport,length:data.size,disableStream:true,disableAutoFetch:true,rangeChunkSize:65536,
        cMapUrl:new URL('./vendor/cmaps/',import.meta.url).href,cMapPacked:true,
        standardFontDataUrl:new URL('./vendor/standard_fonts/',import.meta.url).href,
        wasmUrl:new URL('./vendor/wasm/',import.meta.url).href,iccUrl:new URL('./vendor/iccs/',import.meta.url).href,
        useWorkerFetch:true,enableXfa:false});
      loading=task;const pdf=await task.promise;if(current!==session||loading!==task)return;
      viewer.setDocument(pdf);links.setDocument(pdf);findController.setDocument(pdf);
      send({type:'ready',pages:pdf.numPages});return;
    }
    if(data.session!==session)return;
    if(data.type==='range'){
      const request=pending.get(data.request);if(!request)return;
      const bytes=decode(data.data);
      if(data.begin!==request.begin+request.received||request.received+bytes.length>request.bytes.length)throw new Error('Invalid PDF range response.');
      request.bytes.set(bytes,request.received);request.received+=bytes.length;
      if(request.received===request.bytes.length){pending.delete(data.request);transport?.onDataRange(request.begin,request.bytes)}return;
    }
    if(data.type==='stale'){++generation;session='';await stop();return}
    if(data.type==='theme'){document.documentElement.classList.toggle('dark',!!data.dark);return}
    if(data.type==='command'){
      const changesView=['page','scale','zoomIn','zoomOut','restore'].includes(data.command);
      if(changesView)beginViewChange();
      changingView=changesView;
      try{
      if(data.command==='page')viewer.currentPageNumber=data.page;
      else if(data.command==='scale')viewer.currentScaleValue=String(data.scale);
      else if(data.command==='zoomIn')viewer.increaseScale();
      else if(data.command==='zoomOut')viewer.decreaseScale();
      else if(data.command==='find')eventBus.dispatch('find',{source:findController,type:'again',query:data.query||'',caseSensitive:false,entireWord:false,highlightAll:true,findPrevious:!!data.previous});
      else if(data.command==='copy')send({type:'copy',text:document.getSelection()?.toString()||''});
      else if(data.command==='selectAll'){const selection=document.getSelection();const range=document.createRange();range.selectNodeContents(document.querySelector('#viewer'));selection.removeAllRanges();selection.addRange(range)}
      else if(data.command==='restore'){restore=data.state;links.setHash('page='+restore.page+'&zoom='+encodeURIComponent(restore.scale||'page-width')+','+(restore.left||0)+','+(restore.top||0));restore=undefined}
      }finally{changingView=false}
      if(changesView)viewer.update();
    }
  }catch(value){if(data.session===session)error(value)}
});
window.addEventListener('error',event=>error(event.message));
window.addEventListener('unhandledrejection',event=>error(event.reason));
window.addEventListener('beforeunload',()=>{disposed=true;resizeObserver.disconnect();cancelAnimationFrame(resizeFrame);void stop()});
send({type:'hello'});
