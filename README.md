# rdf_to_graph

배전 계통 CIM 데이터(SQLite에 저장된 RDF)를 메모리 그래프로 적재한 뒤, 스위치 ID 입력 시 고장 선로와 인접 Level1/Level2 선로를 탐색하여 RDF/XML로 추출하는 C++ 프로그램임.
장비 간 직접 연결 방식을 적용하여 Terminal 및 ConnectivityNode는 런타임 객체로 생성하지 않음.

## 구조

```text
main.cpp                        DB 로드 후 switch_id 입력 받아 탐색 반복 수행
module/rdf_graph_generator.*    SQLite(kb_*_type/asserted/literal_statements) -> 메모리 그래프 변환
module/string_pool.hpp          URI 및 리터럴을 단일 저장하고 ID로 참조
module/line_finder_v2.*         장비 그래프 구성, 고장선로 DFS, Level1/Level2 탐색, RDF/XML 출력
module/log_util.*               run_log.txt에 실행 시간 기록
scripts/gen_synthetic_db.py     스키마를 맞춘 합성 DB 생성 스크립트
DB_STRUCTURE_ANALYSIS.md        DB 테이블 및 CIM 연결 구조 정리 문서
```

탐색 흐름은 다음과 같음.

1. `Terminal.ConductingEquipment`와 `Terminal.ConnectivityNode`를 기준으로 동일 CN에 연결된 장비들을 이웃으로 묶음.
2. 시작 스위치에서 DFS를 수행함. 개방된 스위치(`Switch.open=1`)나 Feeder 도달 시 탐색을 중단하고, 탐색된 범위를 고장 선로로 판정함.
3. 고장 선로에 연결된 상시연계점 반대쪽을 Level1 선로, Level1의 연계점 반대쪽을 Level2 선로로 정의함.

## 빌드, 실행

SQLite 개발 헤더가 필요함.

```bash
make
./graph_viewer data/rdfstore.db
```

CMake 사용 시 `cmake -B build && cmake --build build -j` 명령으로 동일한 바이너리를 생성함. 컴파일 최적화 옵션은 `-O2`를 적용함.

```text
graph_viewer [db_path] [base_uri]
graph_viewer --bench <db> <switch_uri> [N]
```

인자 생략 시 `data/rdfstore.db`와 base URI `[http://www.desktop-eqkvgq5.net/2019/cim#](http://www.desktop-eqkvgq5.net/2019/cim#)`를 기본값으로 사용함.
실행 시 `switch_id`를 반복 입력받음 (`11921400000062` 또는 `_11921400000062`). `q` 입력 시 종료되며, `stdin` 종료 시에도 그대로 프로그램을 종료함.
실행 결과는 `fault_line_v2_<id>.xml`, `_level1.xml`, `_level2.xml` 3개 파일로 출력됨.

실데이터(약 460만 트리플)는 보안상 저장소에 포함하지 않음. 성능 측정은 테이블 구조가 동일한 합성 DB를 생성하여 진행함.

```bash
python3 scripts/gen_synthetic_db.py data/rdfstore.db --feeders 100 --units 100
```

## 성능

최적화 및 변경 사항:

- 트리플을 `vector` 4개와 엣지에 중복 저장하던 구조를 단일 엣지로 통합하고 개수만 유지함
- URI와 리터럴은 `StringPool`에 1회만 저장하고 엣지는 `uint32` ID만 보유하도록 변경함
- 리터럴 값은 노드로 생성하지 않도록 제외함
- 엣지 조회 시 복사 대신 인덱스로 참조하며, predicate별 인덱스를 구성함
- 장비 그래프는 프로세스 시작 시 1회만 생성하도록 수정함
- 현재 RSS와 peak RSS를 분리하여 계측하도록 수정함

측정 환경: Apple M3, 메모리 16GB, macOS 26.5.2, Apple clang 17.0.0, `g++ -O2`.

동일 DB 및 동일 스위치(`_10000000000007`) 기준으로 측정하였으며, 첫 실행 제외 후 3회 측정한 중간값을 기재함. 검색 1회는 선로 탐색 및 RDF/XML 3종 파일 쓰기를 포함하며, 50회 반복 측정하여 평균과 p95를 산출함. 수정 전 코드는 검색 시마다 장비 그래프를 재생성하므로, 검색 칸의 괄호에 해당 그래프 구성 시간을 합산하여 표기함.

| 트리플 | 구분 | 로드 | 장비 그래프 | 현재 RSS | peak RSS | 검색 평균 | 검색 p95 |
|---|---|---|---|---|---|---|---|
| 343,969 | 수정 전 | 611.0 ms | 211.2 ms (검색마다) | 442.2 MB | 555.8 MB | 13.8 ms (225.0 ms) | 16.5 ms (227.7 ms) |
| 343,969 | 수정 후 | 155.4 ms | 27.1 ms (1회) | 67.0 MB | 67.0 MB | 8.0 ms | 10.8 ms |
| 1,003,569 | 수정 전 | 1893.3 ms | 725.3 ms (검색마다) | 1084.5 MB | 1312.0 MB | 53.6 ms (778.9 ms) | 70.5 ms (795.8 ms) |
| 1,003,569 | 수정 후 | 599.4 ms | 94.5 ms (1회) | 168.5 MB | 189.5 MB | 28.6 ms | 37.4 ms |
| 3,006,769 | 수정 전 | 6543.4 ms | 2630.7 ms (검색마다) | 3501.4 MB | 4210.5 MB | 89.6 ms (2720.3 ms) | 99.2 ms (2729.9 ms) |
| 3,006,769 | 수정 후 | 2204.6 ms | 304.6 ms (1회) | 518.3 MB | 575.1 MB | 42.1 ms | 48.6 ms |

수정 전 현재 RSS는 실행마다 변동 폭이 컸음(100만 트리플 기준 3회 측정치가 각각 916.5MB, 1084.5MB, 1221.4MB로 기록됨). 표에는 중간값을 반영함.

벤치마크 재실행 명령:

```bash
./graph_viewer --bench data/rdfstore.db \
  'http://www.desktop-eqkvgq5.net/2019/cim#_10000000000007' 50
```
