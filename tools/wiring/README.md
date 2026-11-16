# 배선도 도구

`docs/wiring.html`의 배선 데이터(`scene-data`)를 고쳤을 때 쓰는 스크립트. Node.js와 Playwright가 필요하다.

| 스크립트 | 하는 일 |
|---|---|
| `node netcheck.js` | 브레드보드 세로줄·레일 연결을 따라가서 전기적 연결(넷)이 의도한 회로와 정확히 같은지 검사. 소켓 하나에 선 두 개, 구멍 중복도 잡는다 |
| `node precompute.js 60` | 선 경로를 여러 순서로 60번 시도해 가장 좋은 결과를 `ROUTE_CACHE`에 저장 (약 40초) |
| `node geomcheck.js` | 그려진 선이 모두 직각인지, 같은 방향 선끼리 겹치지 않는지 검사 |

데이터를 고친 뒤 `netcheck.js` → `precompute.js` → `geomcheck.js` 순서로 돌린다. 데이터가 바뀌면 페이지가 저장된 경로를 쓰지 않고 열릴 때 직접 계산한다(조금 느리고 덜 깔끔함).

`netcheck.js` 안의 기대 넷 목록은 회로를 바꿀 때 같이 고쳐야 한다.
