# rdf_to_graph

배전 계통 CIM 데이터(RDF, SQLite에 저장된 것)를 메모리 그래프로 올려 놓고, 스위치 ID를 넣으면 고장 선로와 그 옆 Level1/Level2 선로를 찾아서 RDF/XML로 뽑아 주는 C++ 프로그램이다.
장비끼리 직접 연결하는 방식이라 Terminal, ConnectivityNode는 런타임 객체로 만들지 않는다.

## 구조

```
main.cpp                        DB 로드 후 switch_id 입력 받아서 탐색 반복
module/rdf_graph_generator.*    SQLite(kb_*_type/asserted/literal_statements) -> 메모리 그래프
module/string_pool.hpp          URI, 리터럴을 한 번만 저장하고 id로 참조
module/line_finder_v2.*         장비 그래프 구성, 고장선로 DFS, Level1/Level2 탐색, RDF/XML 출력
module/log_util.*               run_log.txt에 시간 기록
scripts/gen_synthetic_db.py     스키마만 맞춘 합성 DB
DB_STRUCTURE_ANALYSIS.md        DB 테이블이랑 CIM 연결 구조 정리
```

탐색 흐름은 이렇다.

1. Terminal.ConductingEquipment와 Terminal.ConnectivityNode로 같은 CN에 붙은 장비끼리 이웃으로 묶는다.
2. 시작 스위치에서 DFS를 돈다. 열린 스위치(Switch.open=1)나 Feeder를 만나면 거기서 멈추고, 지나간 범위를 고장 선로로 본다.
3. 고장 선로에 붙은 상시연계점 반대쪽이 Level1 선로, Level1의 연계점 반대쪽이 Level2 선로다.

## 빌드, 실행

SQLite 개발 헤더가 필요하다.

```bash
make
./graph_viewer data/rdfstore.db
```

cmake가 있으면 `cmake -B build && cmake --build build -j` 로 같은 바이너리를 만든다. 최적화는 `-O2`.

```text
graph_viewer [db_path] [base_uri]
graph_viewer --bench <db> <switch_uri> [N]
```

인자 없이 돌리면 `data/rdfstore.db`와 base URI `http://www.desktop-eqkvgq5.net/2019/cim#`를 쓴다.
실행되면 switch_id를 계속 입력받는다 (`11921400000062` 또는 `_11921400000062`). `q`를 치면 끝난다. stdin이 끝나면 그대로 종료한다.
결과는 `fault_line_v2_<id>.xml`, `_level1.xml`, `_level2.xml` 세 개로 나온다.

실데이터(약 460만 트리플)는 보안상 레포에 넣을 수 없다. 측정은 테이블 구성이 같은 합성 DB로 했다.

```bash
python3 scripts/gen_synthetic_db.py data/rdfstore.db --feeders 100 --units 100
```

## 성능

바꾼 것:

- 트리플을 vector 4개와 엣지에 중복으로 두던 것을 엣지 하나로 합치고 개수만 남겼다
- URI, 리터럴은 StringPool에 한 번만 넣고 엣지는 uint32 id만 가진다
- 리터럴 값은 노드로 만들지 않는다
- 엣지 조회는 복사 대신 인덱스로 보고, predicate별 인덱스를 둔다
- 장비 그래프는 시작할 때 한 번만 만든다
- 현재 RSS와 peak RSS를 따로 찍는다
- stdin EOF에서 프롬프트가 무한히 찍히던 것을 고쳤다

측정 환경: Apple M3, 메모리 16GB, macOS 26.5.2, Apple clang 17.0.0, `g++ -O2`.

같은 DB, 같은 스위치(`_10000000000007`)로 재고, 첫 실행은 버린 뒤 3번의 중간값이다. 검색 1회는 탐색과 RDF/XML 3개 쓰기이고 50번 반복해서 평균과 p95를 냈다. 수정 전 코드는 검색할 때마다 장비 그래프를 다시 만든다. 검색 칸의 괄호는 그 구성 시간을 더한 값이다.

| 트리플 | | 로드 | 장비 그래프 | 현재 RSS | peak RSS | 검색 평균 | 검색 p95 |
|---|---|---|---|---|---|---|---|
| 343,969 | 수정 전 | 611.0 ms | 211.2 ms (검색마다) | 442.2 MB | 555.8 MB | 13.8 ms (225.0 ms) | 16.5 ms (227.7 ms) |
| 343,969 | 수정 후 | 155.4 ms | 27.1 ms (1회) | 67.0 MB | 67.0 MB | 8.0 ms | 10.8 ms |
| 1,003,569 | 수정 전 | 1893.3 ms | 725.3 ms (검색마다) | 1084.5 MB | 1312.0 MB | 53.6 ms (778.9 ms) | 70.5 ms (795.8 ms) |
| 1,003,569 | 수정 후 | 599.4 ms | 94.5 ms (1회) | 168.5 MB | 189.5 MB | 28.6 ms | 37.4 ms |
| 3,006,769 | 수정 전 | 6543.4 ms | 2630.7 ms (검색마다) | 3501.4 MB | 4210.5 MB | 89.6 ms (2720.3 ms) | 99.2 ms (2729.9 ms) |
| 3,006,769 | 수정 후 | 2204.6 ms | 304.6 ms (1회) | 518.3 MB | 575.1 MB | 42.1 ms | 48.6 ms |

수정 전 현재 RSS는 실행마다 많이 움직였다. 100만 트리플에서 3회가 916.5, 1084.5, 1221.4MB였다. 표에는 중간값을 넣었다.

다시 재는 명령:

```bash
./graph_viewer --bench data/rdfstore.db \
  'http://www.desktop-eqkvgq5.net/2019/cim#_10000000000007' 50
```

`BENCH` 줄이 stderr에 나온다.

## 남은 것

- 인접 리스트를 CSR로 눌러서 메모리를 더 줄이기
- 로더와 탐색을 Rust로 옮겨 같은 벤치로 비교해 보기
