#pragma once
// 테스트 결과와 함께 배포하는 독립 HTML. 네트워크 없이 로컬 PNG를 비교한다.
inline constexpr const char* kCloudDetailComparisonPage = R"HTML(<!doctype html>
<html lang="ko"><meta charset="utf-8"><title>Detail 32³ / 64³ 비교</title>
<style>
body{margin:24px;background:#101722;color:#e9eff8;font:16px system-ui}h1{font-size:26px}
p{line-height:1.65;max-width:1150px}select,button,input{font:inherit;padding:7px;margin:4px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:16px}figure{margin:0;min-width:0}
img{width:100%;cursor:crosshair}figcaption{padding:10px 0}canvas{width:100%;max-width:640px;background:#222}
a{color:#91caff}.diff{max-width:960px}small{color:#b7c6d9}
</style>
<h1>Detail 32³ / 64³ — 해상도만 비교</h1>
<p>왼쪽은 기존 32³, 오른쪽은 후보 64³입니다. 둘 다 동일한 노이즈 함수에서 새로 생성했습니다.
Base 128³ · Weather 256² · Detail 2,000m · 기존 High/대기/조명 유지.
일반 실행은 아직 32³입니다. F5 중앙 검은 물체는 기존 진단 건물입니다.</p>
<label>카메라 <select id="view"><option>F5</option><option>F6</option></select></label>
<label>화면 <select id="mode"><option value="composite">Composite</option><option value="without-air">구름 대기 제외</option>
<option value="cloud-T">구름 투과율</option><option value="cloud-tau">구름 광학 깊이</option>
<option value="rotation">회전 경로</option><option value="forward">전진 경로</option></select></label>
<div id="motion" hidden><button id="play">재생</button><input id="frame" type="range" min="0" max="23" value="0">
<span id="counter">0 / 23</span><small>동일한 24개 카메라 위치, 시간/바람 고정. 10fps 슬라이드 재생이며 실제 실행 프레임률이 아닙니다.</small></div>
<div class="grid"><figure><figcaption>기존 Detail 32³</figcaption><img id="left" alt="32³"></figure>
<figure><figcaption>후보 Detail 64³</figcaption><img id="right" alt="64³"></figure></div>
<p id="description"></p>
<p>위 이미지의 구름 경계를 클릭하면 두 화면의 같은 좌표를 아래에 원본 크기로 표시합니다.
<small>640×360픽셀 영역이며 좁은 창에서는 축소 표시됩니다.</small>
<a id="original32" target="_blank">32³ 원본</a> · <a id="original64" target="_blank">64³ 원본</a></p>
<div class="grid"><figure><figcaption id="cropLabel">32³ 경계 확대</figcaption><canvas id="crop32" width="640" height="360"></canvas></figure>
<figure><figcaption>64³ 같은 위치</figcaption><canvas id="crop64" width="640" height="360"></canvas></figure></div>
<figure id="difference" class="diff"><figcaption>화면 RGB 절대 차이 ×8 (밝을수록 차이 큼, 품질 점수가 아님)</figcaption><img id="diff" alt="차이"></figure>
<p>확인: ① 같은 경계에서 작은 굴곡이 더 자연스러운가 ② 작은 구름이 갑자기 없어지지 않는가
③ 회전/전진에서 경계가 더 깜빡이지 않는가. 구름 투과율은 밝을수록 뒤가 비치고, 광학 깊이는 밝을수록 누적 차폐가 큽니다.
대기 제외 화면은 구름 앞 공기 효과만 제외하며 배경과 일반 Tone은 유지합니다.</p>
<p><a href="README.md">수치·조건 보고서</a> · <a href="paths.csv">동일 카메라 목록</a> · <a href="gpu-samples.csv">GPU 원시 측정</a>.
해상도 선택 후 원경 대기 조절로 진행합니다. 사용자 화면 승인은 아직 없습니다.</p>
<script>
const $=id=>document.getElementById(id),v=$('view'),m=$('mode'),f=$('frame');
let x=960,y=300,timer=null,serial=0;
function crop(){const sx=Math.max(0,Math.min(1280,x-320)),sy=Math.max(0,Math.min(720,y-180));
for(const [im,cv] of [['left','crop32'],['right','crop64']]){
const image=$(im),c=$(cv).getContext('2d');if(image.complete&&image.naturalWidth)c.drawImage(image,sx,sy,640,360,0,0,640,360);
}$('cropLabel').textContent='32³ 원본 영역 ('+sx+', '+sy+') / 640×360';}
async function show(){
const id=++serial,moving=['rotation','forward'].includes(m.value);
$('motion').hidden=!moving;$('difference').hidden=moving;$('counter').textContent=f.value+' / 23';
const suffix=v.value+'-'+m.value+(moving?'-'+f.value:'')+'.png';
const sources=['32-'+suffix,'64-'+suffix];
const loaded=await Promise.all(sources.map(src=>new Promise((resolve,reject)=>{const image=new Image();image.onload=()=>resolve(image);image.onerror=reject;image.src=src;}))).catch(()=>null);
if(!loaded||id!==serial)return;
$('left').src=loaded[0].src;$('right').src=loaded[1].src;
$('original32').href=sources[0];$('original64').href=sources[1];$('diff').src='diff-'+v.value+'-'+(moving?'composite':m.value)+'.png';
$('description').textContent=moving?'두 해상도의 이미지를 모두 읽은 뒤 같은 프레임으로 함께 전환합니다.':'정지 화면은 같은 카메라·시간·프리셋입니다. 차이 이미지는 표시 후 RGB의 절대 차이를 8배 강조합니다.';
requestAnimationFrame(crop);
}
function stop(){clearInterval(timer);timer=null;$('play').textContent='재생';}
v.onchange=()=>{stop();show();};m.onchange=()=>{stop();f.value=0;show();};f.oninput=()=>{stop();show();};
$('play').onclick=()=>{if(timer){stop();return;}$('play').textContent='정지';timer=setInterval(()=>{f.value=(Number(f.value)+1)%24;show();},100);};
for(const id of ['left','right']){$(id).onload=crop;$(id).onclick=e=>{const r=e.target.getBoundingClientRect();x=Math.round((e.clientX-r.left)*1920/r.width);y=Math.round((e.clientY-r.top)*1080/r.height);crop();};}show();
</script></html>)HTML";