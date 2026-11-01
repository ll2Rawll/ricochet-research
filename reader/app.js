'use strict';
const REPO = 'https://github.com/ll2Rawll/ricochet-research';
const RAW = 'https://raw.githubusercontent.com/ll2Rawll/ricochet-research/main/ricochet.jsonl';
const PAGE_SIZE = 18;
const SNAPSHOT_REVISION = 15;
const LEVELS = {
  V: {name:'Vendor statement', description:'A statement supported by the cited vendor or policy source. This does not independently reproduce the vendor’s runtime or performance claims.'},
  P: {name:'Platform reference', description:'Documented platform, protocol, upstream UI or sample-code behavior. It is not evidence that a particular RICOCHET build uses that API or mechanism.'},
  O: {name:'Static observation', description:'Reproduced contents of a hash-identified public diagnostic artifact. These findings inspect the utility, not the proprietary game or kernel driver.'},
  R: {name:'Historical study', description:'An author-reported historical experiment with its original test period and limitations. It is not a current-build effectiveness measurement.'},
  A: {name:'Artifact contents', description:'Contents of a pinned public or user-provided artifact, without authenticating its producer or inferring anti-cheat runtime behavior.'}
};
const CATEGORIES = [
  {id:'all',name:'All research',symbol:'▦',topics:[]},
  {id:'kernel',name:'Windows & kernel',symbol:'⌘',topics:['architecture','kernel','coverage','kernel_names','os_privilege','os_memory','os_callbacks','os_communication','os_integrity','hvci','performance','driver_reference_code','os_handle_access','os_notifications','driver_io','kernel_integrity','driver_provenance','driver_disclosure','driver_architecture','thread_callbacks','registry_callbacks','registry_context','callback_lifecycle','client_hardening','driver_identity','driver_signature_scope','driver_blocking','code_integrity_diagnostics','kernel_detection_boundaries']},
  {id:'attestation',name:'Boot & attestation',symbol:'◇',topics:['attestation','tpm_mechanics','boot_security','troubleshooting','attestation_client','attestation_api','attestation_code','remote_attestation','attestation_vulnerability','attestation_metadata','attestation_token_validation','attestation_claims','attestation_policy','attestation_deployment_security','attestation_service_trust','attestation_sdk_validation']},
  {id:'server',name:'Server protections',symbol:'▤',topics:['mitigations','server_culling','client_server_boundary']},
  {id:'unlocks',name:'Unlocks & progression',symbol:'›',topics:['content_entitlements','modding_support','modding_scope','unlockall_history','boosting','commerce_reference','license_validation','inventory_reference','inventory_authority','inventory_concurrency','inventory_transactions','steam_inventory','steam_commerce','progression_state','content_compatibility']},
  {id:'engineering',name:'Menu engineering',symbol:'⟨/⟩',topics:['menu_architecture','ui_backends','input_routing','rendering_reference','graphics_lifecycle','graphics_resources','menu_reference','mod_tools_reference','capture_api_limits']},
  {id:'uploads',name:'Uploaded artifacts',symbol:'▣',topics:['uploaded_artifact_packaging','uploaded_artifact_quality','uploaded_artifact_export_coverage','uploaded_artifact_provenance','artifact_interpretation']},
  {id:'devices',name:'DMA & input devices',symbol:'⌁',topics:['dma','dma_mechanics','input_devices']},
  {id:'detection',name:'Detection & enforcement',symbol:'◎',topics:['ml','replay','detection_pipeline','telemetry','enforcement','reports','lmm','ranked','enforcement_strategy','evasion_defense','detection_component_updates','security_pipeline','replay_architecture','enforcement_workflow','gameplay_context']},
  {id:'artifact',name:'Diagnostic code',symbol:'⟨⟩',topics:['artifact_packaging','artifact_provenance','attestation_native','attestation_managed','attestation_keys']},
  {id:'accounts',name:'Accounts & privacy',symbol:'◫',topics:['account_security','privacy','appeals','organization','ecosystem']},
  {id:'context',name:'History & studies',symbol:'◷',topics:['history','metrics','research','research_methods','limitations','false_positives','historical_experiments','research_artifacts','research_boundaries','security_research']}
];
const $ = id => document.getElementById(id);
const escapeHTML = value => String(value ?? '').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const normalize = value => String(value ?? '').toLowerCase().normalize('NFKD').replace(/\brangrid\b/g,'randgrid').replace(/unlock[\s-]*all/g,'unlock all').replace(/[^\p{L}\p{N}]+/gu,' ').trim();
const topicName = value => value.replaceAll('_',' ');
const categoryFor = claim => CATEGORIES.slice(1).find(g=>g.topics.includes(claim.topic))?.id || 'context';
const safeURL = value => {try {const u=new URL(value);return u.protocol==='https:' ? u.href : '#';} catch{return '#';}};
const params = new URLSearchParams(location.search);
const state = {query:params.get('q')||'', category:params.get('category')||'all', level:params.get('level')||'all', view:params.get('view')==='sources'?'sources':'findings', sort:params.get('sort')||'relevance', page:Math.max(1,Number.parseInt(params.get('page')||'1',10)||1)};
if(!CATEGORIES.some(c=>c.id===state.category)) state.category='all';
if(!LEVELS[state.level]) state.level='all';
if(!['relevance','newest','oldest','id'].includes(state.sort)) state.sort='relevance';
let meta, claims=[], sources=[], sourceMap={}, claimMap={}, usage={}, visible=[], matchedClaims=[], timer, toastTimer, loaded=false;

function dateLabel(date) {
  if(!date) return 'Date not established';
  const chunks=date.split('-');
  if(chunks.length===1) return date;
  const d=new Date(date+(chunks.length===2?'-01':'')+'T12:00:00Z');
  if(Number.isNaN(d.getTime())) return date;
  return new Intl.DateTimeFormat('en-GB',{...(chunks.length===3?{day:'numeric'}:{}),month:'short',year:'numeric',timeZone:'UTC'}).format(d);
}
function tokenize(query){return (query.match(/"[^"]+"|[^\s"]+/g)||[]).map(s=>normalize(s.replaceAll('"',''))).filter(Boolean);}
function highlight(value) {
  const terms=(state.query.match(/"[^"]+"|[^\s"]+/g)||[]).map(t=>t.replaceAll('"','')).filter(t=>t.length>1).sort((a,b)=>b.length-a.length);
  if(!terms.length) return escapeHTML(value);
  const regex=new RegExp(terms.map(t=>t.replace(/[.*+?^${}()|[\]\\]/g,'\\$&')).join('|'),'gi');
  let out='',previous=0;
  for(const m of String(value).matchAll(regex)){out+=escapeHTML(String(value).slice(previous,m.index))+'<mark>'+escapeHTML(m[0])+'</mark>';previous=m.index+m[0].length;}
  return out+escapeHTML(String(value).slice(previous));
}
function badge(level){return `<span class="badge badge-${level}" title="${escapeHTML(LEVELS[level].description)}">${LEVELS[level].name}</span>`;}
function toast(message){$('toast').textContent=message;$('toast').classList.add('visible');clearTimeout(toastTimer);toastTimer=setTimeout(()=>$('toast').classList.remove('visible'),2400);}
async function copy(text,message){
  try{await navigator.clipboard.writeText(text);toast(message);}catch{
    const input=document.createElement('textarea');input.value=text;input.setAttribute('readonly','');input.className='sr-only';document.body.append(input);input.select();
    const ok=document.execCommand('copy');input.remove();toast(ok?message:'Copy is unavailable in this browser.');
  }
}
function writeURL(){
  const p=new URLSearchParams();
  if(state.query)p.set('q',state.query);
  if(state.category!=='all')p.set('category',state.category);
  if(state.level!=='all')p.set('level',state.level);
  if(state.view==='sources')p.set('view','sources');
  if(state.sort!=='relevance')p.set('sort',state.sort);
  if(state.page>1)p.set('page',String(state.page));
  history.replaceState(null,'',location.pathname+(p.size?'?'+p:'')+location.hash);
}
function setState(next){Object.assign(state,next);state.page=1;render();}
function scoreClaim(c,terms){
  let score=0;
  for(const term of terms){
    if(c._primary.includes(term))score+=12;
    else if(c._secondary.includes(term))score+=4;
    else if(c._artifact.includes(term))score+=1;
    else return -1;
    if(normalize(c.id)===term)score+=100;
  }
  return score;
}
function filteredClaims(){
  const terms=tokenize(state.query);
  const alias=meta.topic_aliases?.[state.query.toLowerCase().trim().replaceAll(' ','_')] || [];
  return claims.filter(c=>(state.category==='all'||c._category===state.category)&&(state.level==='all'||c.level===state.level))
    .map(c=>({c,score:alias.includes(c.topic)?Math.max(8,scoreClaim(c,terms)):scoreClaim(c,terms)}))
    .filter(r=>r.score>=0).sort((a,b)=>{
      if(state.sort==='newest')return b.c.date.localeCompare(a.c.date)||a.c.id.localeCompare(b.c.id);
      if(state.sort==='oldest')return a.c.date.localeCompare(b.c.date)||a.c.id.localeCompare(b.c.id);
      if(state.sort==='id')return a.c.id.localeCompare(b.c.id);
      return b.score-a.score||(state.category==='uploads'?Number(b.c.id.startsWith('F'))-Number(a.c.id.startsWith('F')):0)||a.c.id.localeCompare(b.c.id);
    }).map(r=>r.c);
}
function filteredSources(){
  const terms=tokenize(state.query);
  return sources.filter(s=>{
    const linked=usage[s.id]||[];
    const context=linked.some(c=>(state.category==='all'||c._category===state.category)&&(state.level==='all'||c.level===state.level)) || (s.bibliography_only && state.level==='all' && ['all','context'].includes(state.category));
    return context&&terms.every(t=>s._search.includes(t)||linked.some(c=>c._primary.includes(t)));
  }).sort((a,b)=>{
    if(state.sort==='newest')return (b.published||'').localeCompare(a.published||'')||a.id.localeCompare(b.id);
    if(state.sort==='oldest')return (a.published||'9999').localeCompare(b.published||'9999')||a.id.localeCompare(b.id);
    return a.id.localeCompare(b.id);
  });
}
function findingCard(c){return `<article class="result-card" data-id="${c.id}"><div class="card-meta"><span class="finding-id">${c.id}</span>${badge(c.level)}<time class="card-date" datetime="${c.date}">${escapeHTML(dateLabel(c.date))}</time></div><p class="card-text">${highlight(c.text)}</p><p class="card-scope"><span>SCOPE</span>${escapeHTML(c.scope)}</p><div class="card-bottom"><div class="citation-group"><span>SOURCES</span>${c.src.map(s=>`<button class="citation" data-source="${s}" title="${escapeHTML(sourceMap[s].title)}">${s}</button>`).join('')}</div><button class="detail-button" data-finding="${c.id}" aria-label="Read details for ${c.id}">Read finding <span aria-hidden="true">›</span></button></div></article>`;}
function sourceCard(s){return `<article class="result-card" data-id="${s.id}"><div class="card-meta"><span class="finding-id">${s.id}</span><span class="source-kind">${escapeHTML(topicName(s.kind))}</span><span class="card-date">${s.published?escapeHTML(dateLabel(s.published)):'Publication date not established'}</span></div><h3 class="source-title">${highlight(s.title)}</h3><a class="source-url" href="${safeURL(s.url)}" target="_blank" rel="noopener">${escapeHTML(new URL(s.url).hostname)} ›</a>${s.note?`<p class="card-scope">${escapeHTML(s.note)}</p>`:''}<div class="card-bottom"><span class="finding-id">${s.community_report?'UNVERIFIED COMMUNITY SOURCE':s.bibliography_only?'SOURCE-ONLY REFERENCE':`CITED BY ${(usage[s.id]||[]).length} FINDING${usage[s.id]?.length===1?'':'S'}`} </span><button class="detail-button" data-source="${s.id}" aria-label="View source ${s.id}">Source details ›</button></div></article>`;}
function renderCategories(){
  $('categories').innerHTML=CATEGORIES.map((g,i)=>`${i===1?'<div class="category-divider"></div>':''}<button class="category" data-category="${g.id}" aria-current="${state.category===g.id}"><span class="category-symbol" aria-hidden="true">${g.symbol}</span><span>${g.name}</span><span class="category-count">${g.id==='all'?claims.length:claims.filter(c=>c._category===g.id).length}</span></button>`).join('');
}
function render(){
  if(!loaded)return;
  $('search').value=state.query;$('level').value=state.level;$('sort').value=state.sort;
  $('clear-search').hidden=!state.query;
  $('findings-tab').setAttribute('aria-pressed',String(state.view==='findings'));
  $('sources-tab').setAttribute('aria-pressed',String(state.view==='sources'));
  document.querySelectorAll('[data-category]').forEach(b=>b.setAttribute('aria-current',String(b.dataset.category===state.category)));
  matchedClaims=filteredClaims();visible=state.view==='sources'?filteredSources():matchedClaims;
  const pageCount=Math.max(1,Math.ceil(visible.length/PAGE_SIZE));state.page=Math.min(state.page,pageCount);
  const start=(state.page-1)*PAGE_SIZE;const page=visible.slice(start,start+PAGE_SIZE);
  const category=CATEGORIES.find(c=>c.id===state.category);
  $('category-title').textContent=state.view==='sources'?(state.category==='all'?'Source library':category.name+' sources'):category.name;
  $('result-count').textContent=visible.length+' '+(state.view==='sources'?'source':'finding')+(visible.length===1?'':'s');
  $('findings-tab-count').textContent=claims.length;$('sources-tab-count').textContent=sources.length;
  $('export').disabled=!visible.length;
  const active=[];
  if(state.query)active.push(`<button class="filter-pill" data-reset="query">${escapeHTML(state.query)}<span>×</span></button>`);
  if(state.category!=='all')active.push(`<button class="filter-pill" data-reset="category">${category.name}<span>×</span></button>`);
  if(state.level!=='all')active.push(`<button class="filter-pill" data-reset="level">${LEVELS[state.level].name}<span>×</span></button>`);
  $('active-filters').hidden=!active.length;
  $('active-filters').innerHTML=active.join('')+(active.length?'<button class="text-button reset" data-reset="all">Clear all</button>':'');
  $('results').innerHTML=page.length?page.map(state.view==='sources'?sourceCard:findingCard).join(''):`<div class="empty-state"><div class="empty-symbol" aria-hidden="true">⌕</div><h3>No matching ${state.view}</h3><p>Try fewer words, another topic, or a different evidence filter.</p><button class="small-button" data-reset="all">Reset search and filters</button></div>`;
  $('pagination').innerHTML=visible.length?`<span>${start+1}–${Math.min(start+PAGE_SIZE,visible.length)} of ${visible.length}</span><div class="page-buttons"><button class="small-button" data-page="prev" ${state.page===1?'disabled':''} aria-label="Previous page">← Previous</button><button class="small-button" data-page="next" ${state.page===pageCount?'disabled':''} aria-label="Next page">Next →</button></div>`:'';
  writeURL();
}
function showDialog(title,kicker,body){
  $('detail-kicker').textContent=kicker;$('detail-body').innerHTML=`<h2 id="detail-title" class="detail-title">${escapeHTML(title)}</h2>${body}`;
  if(!$('detail').open)$('detail').showModal();
  $('detail').scrollTop=0;
}
function showFinding(id){
  const c=claimMap[id];if(!c)return;
  showDialog(c.id+' · '+topicName(c.topic),'FINDING DETAILS',`${badge(c.level)}<p class="detail-text">${escapeHTML(c.text)}</p><div class="detail-scope"><span class="detail-label">As of ${escapeHTML(dateLabel(c.date))}</span>${escapeHTML(c.scope)}</div><div class="detail-section"><h3>Original sources</h3>${c.src.map(id=>{const s=sourceMap[id];return `<a class="source-ref" href="${safeURL(s.url)}" target="_blank" rel="noopener"><strong>${s.id}</strong> · ${escapeHTML(s.title)} ›<small>${escapeHTML(s.published?dateLabel(s.published):'Publication date not established')} · ${escapeHTML(s.kind.replaceAll('_',' '))}</small></a>`;}).join('')}</div><div class="detail-actions"><button class="small-button" data-copy="${c.id}">Copy citation</button><button class="small-button" data-copy-link="${c.id}">Copy finding link ›</button></div><details><summary>Evidence locations and provenance</summary><p>Publisher section headings identify documentation sections. Paragraph IDs refer to reviewed extraction, not publisher line numbers. Static file offsets and code locations retain their component scope.</p><pre>${escapeHTML(JSON.stringify(c.evidence||c.at,null,2))}</pre></details>`);
  history.replaceState(null,'',location.pathname+location.search+'#'+c.id);
}
function showSource(id){
  const s=sourceMap[id];if(!s)return;
  showDialog(s.title,'SOURCE '+s.id,`<span class="source-kind">${escapeHTML(topicName(s.kind))}</span><div class="detail-scope"><span class="detail-label">Publication / update date</span>${escapeHTML(dateLabel(s.published))}${s.sections?`<br><br><span class="detail-label">Reviewed sections</span>${escapeHTML(s.sections)}`:''}</div>${s.note?`<p class="detail-text">${escapeHTML(s.note)}</p>`:''}<a class="source-ref" href="${safeURL(s.url)}" target="_blank" rel="noopener">Open original source ›<small>${escapeHTML(s.url)}</small></a><div class="detail-section"><h3>${s.community_report?'Unverified community source':s.bibliography_only?'Bibliography entry':'Cited findings'}</h3>${s.bibliography_only?'<p class="detail-scope">Source-only research reference. No current MW4/Warzone bypass is established by this entry.</p>':''}<div class="citation-group">${(usage[id]||[]).map(c=>`<button class="citation" data-finding="${c.id}" title="${escapeHTML(c.text)}">${c.id}</button>`).join('')}</div></div><details><summary>Source metadata</summary><pre>${escapeHTML(JSON.stringify(s._original,null,2))}</pre></details>`);
  history.replaceState(null,'',location.pathname+location.search+'#'+s.id);
}
function evidenceGuide(){showDialog('Read the evidence, with its scope.','EVIDENCE GUIDE',Object.entries(LEVELS).map(([id,l])=>`<div class="level-explanation">${badge(id)}<p>${escapeHTML(l.description)}</p></div>`).join('')+'<p class="detail-scope">Source support is not a universal current-build guarantee. Preserve the game, date, test period and artifact identity when interpreting a finding.</p>');}
function plainClaim(c){return Object.fromEntries(Object.entries(c).filter(([k])=>!k.startsWith('_')));}
function exportView(){
  const initial=state.view==='sources'?visible.flatMap(s=>usage[s.id]||[]):matchedClaims;
  const selected=new Map(initial.map(c=>[c.id,c]));const queue=[...selected.values()];
  while(queue.length){const c=queue.pop();for(const id of [...(c.updates||[]),...(c.contrasts_with||[]),...(c.basis_claims||[])])if(!selected.has(id)&&claimMap[id]){selected.set(id,claimMap[id]);queue.push(claimMap[id]);}}
  const selectedClaims=[...selected.values()];const needed=new Set([...selectedClaims.flatMap(c=>c.src),...(state.view==='sources'?visible.map(s=>s.id):[])]);
  const subsetMeta={...meta,subset:true,sources:needed.size,claims:selectedClaims.length,source_supported_claims:selectedClaims.filter(c=>c.level!=='O'&&!c.id.startsWith('F')).length,uploaded_artifact_observations:selectedClaims.filter(c=>c.id.startsWith('F')).length,static_artifact_observations:selectedClaims.filter(c=>c.level==='O').length,community_sources:sources.filter(s=>needed.has(s.id)&&s.community_report).length,bibliography_sources:sources.filter(s=>needed.has(s.id)&&s.bibliography_only).length,selection:{view:state.view,query:state.query,category:state.category,level:state.level}};
  const output=[subsetMeta,...sources.filter(s=>needed.has(s.id)).map(s=>s._original),...selectedClaims.map(plainClaim)];
  const url=URL.createObjectURL(new Blob([output.map(r=>JSON.stringify(r)).join('\n')+'\n'],{type:'application/x-ndjson'}));
  const a=document.createElement('a');a.href=url;a.download='ricochet-research-selection.jsonl';document.body.append(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(url),1000);
  toast(`Exported ${selectedClaims.length} findings and ${needed.size} sources.`);
}
function initialize(records){
  const m=records.find(r=>r.type==='meta');const ss=records.filter(r=>r.type==='source');const cs=records.filter(r=>r.type==='claim');
  if(!m||m.format!=='ricochet-research/2.0'||m.claims!==cs.length||m.sources!==ss.length||m.revision<SNAPSHOT_REVISION)throw new Error('Research format/count/revision mismatch');
  const sm=Object.fromEntries(ss.map(s=>[s.id,s]));
  if(new Set(cs.map(c=>c.id)).size!==cs.length||cs.some(c=>!LEVELS[c.level]||!c.src.every(id=>sm[id])))throw new Error('Citation or identity mismatch');
  meta=m;claims=cs;sourceMap=sm;sources=ss;claimMap=Object.fromEntries(cs.map(c=>[c.id,c]));usage={};
  sources.forEach(s=>{s._original=JSON.parse(JSON.stringify(s));s._search=normalize([s.id,s.title,s.url,s.note,s.summary,s.scope,s.authors,s.author,s.sections,s.published].join(' '));});
  claims.forEach(c=>{
    c._category=categoryFor(c);c._primary=normalize([c.id,c.text,c.topic.replaceAll('_',' ')].join(' '));
    c._secondary=normalize([c.scope,c.date,...c.src,...c.src.map(s=>sourceMap[s].title)].join(' '));c._artifact=normalize(JSON.stringify(c.evidence||{}));
    c.src.forEach(s=>(usage[s]||=[]).push(c));
  });
  $('total-findings').textContent=claims.length;$('total-sources').textContent=sources.length;$('reviewed').textContent=dateLabel(meta.reviewed).toUpperCase();$('revision').textContent='Research revision '+meta.revision;
  loaded=true;renderCategories();render();
  const id=location.hash.slice(1);if(claimMap[id])showFinding(id);else if(sourceMap[id])showSource(id);
}
async function load(){
  try{
    try{
      const response=await fetch(RAW,{signal:AbortSignal.timeout(4500)});if(!response.ok)throw new Error('Source unavailable');
      initialize((await response.text()).trim().split(/\r?\n/).map(l=>JSON.parse(l)));
    }catch{
      const response=await fetch('data.json');if(!response.ok)throw new Error('Snapshot unavailable');initialize(await response.json());$('revision').textContent='Saved snapshot · revision '+meta.revision;
    }
  }catch{
    $('result-count').textContent='Research unavailable';$('results').innerHTML=`<div class="empty-state"><h3>The library could not be loaded.</h3><p>Retry, or open the complete research on GitHub.</p><button class="small-button" id="retry">Try again</button> <a class="small-button" href="${REPO}#readme">Open GitHub ›</a></div>`;
  }
}
$('search').value=state.query;
$('search').addEventListener('input',e=>{clearTimeout(timer);timer=setTimeout(()=>setState({query:e.target.value}),70);});
$('search-form').addEventListener('submit',e=>{e.preventDefault();clearTimeout(timer);setState({query:$('search').value});});
$('clear-search').addEventListener('click',()=>{setState({query:''});$('search').focus();});
$('level').addEventListener('change',e=>setState({level:e.target.value}));
$('sort').addEventListener('change',e=>setState({sort:e.target.value}));
$('findings-tab').addEventListener('click',()=>setState({view:'findings'}));
$('sources-tab').addEventListener('click',()=>setState({view:'sources'}));
$('evidence-guide').addEventListener('click',evidenceGuide);
$('close-detail').addEventListener('click',()=>$('detail').close());
$('detail').addEventListener('click',e=>{if(e.target===$('detail')){const r=$('detail').getBoundingClientRect();if(e.clientX<r.left||e.clientX>r.right||e.clientY<r.top||e.clientY>r.bottom)$('detail').close();}});
$('detail').addEventListener('close',()=>{history.replaceState(null,'',location.pathname+location.search);});
$('share').addEventListener('click',()=>copy(location.href,'View link copied.'));
$('export').addEventListener('click',()=>{if(loaded)exportView();});
$('search-help').addEventListener('click',()=>{const open=$('search-tips').hidden;$('search-tips').hidden=!open;$('search-help').setAttribute('aria-expanded',String(open));});
document.addEventListener('click',e=>{
  const b=e.target.closest('button');if(!b)return;
  if(b.dataset.category)setState({category:b.dataset.category});
  if(b.dataset.query!==undefined)setState({query:b.dataset.query,category:'all',level:'all',view:'findings'});
  if(b.dataset.finding)showFinding(b.dataset.finding);
  if(b.dataset.source)showSource(b.dataset.source);
  if(b.dataset.reset){const next=b.dataset.reset==='all'?{query:'',category:'all',level:'all'}:b.dataset.reset==='query'?{query:''}:{[b.dataset.reset]:'all'};setState(next);}
  if(b.dataset.page){state.page+=b.dataset.page==='next'?1:-1;render();$('results').scrollIntoView({block:'start',behavior:'instant'});$('results').focus({preventScroll:true});}
  if(b.dataset.copy){const c=claimMap[b.dataset.copy];copy(`${c.id} · ${LEVELS[c.level].name} · As of ${c.date}\n${c.text}\nScope: ${c.scope}\n`+c.src.map(s=>`${s}: ${sourceMap[s].title}\n${sourceMap[s].url}`).join('\n'),'Citation copied.');}
  if(b.dataset.copyLink){const url=new URL(location.href);url.hash=b.dataset.copyLink;copy(url.href,'Finding link copied.');}
  if(b.id==='retry')load();
});
document.addEventListener('keydown',e=>{if(e.key==='/'&&!$('detail').open&&!['INPUT','TEXTAREA','SELECT'].includes(document.activeElement.tagName)){e.preventDefault();$('search').focus();}if(e.key==='Escape'&&!$('detail').open&&document.activeElement===$('search')){setState({query:''});$('search').blur();}});
try{if(localStorage.getItem('ricochet-theme')==='light')document.body.classList.add('light');}catch{}
function themeLabel(){$('theme').setAttribute('aria-label',document.body.classList.contains('light')?'Switch to dark theme':'Switch to light theme');}
themeLabel();$('theme').addEventListener('click',()=>{document.body.classList.toggle('light');themeLabel();try{localStorage.setItem('ricochet-theme',document.body.classList.contains('light')?'light':'dark');}catch{}});
const mobile=matchMedia('(max-width:760px)');function topicsLayout(){document.querySelector('.topic-disclosure').open=!mobile.matches;}topicsLayout();mobile.addEventListener('change',topicsLayout);
load();
