# RDF Database Structure Analysis

## 데이터베이스 개요

**파일**: `data/rdfstore.db`  
**형식**: KB (Knowledge Base) 형식 SQLite 데이터베이스  
**총 트리플 수**: 약 4,598,934개

---

## 테이블 구조

### 1. `kb_*_type_statements`
**목적**: 리소스의 타입 정의 (rdf:type)

**스키마**:
- `id` (INTEGER): 고유 ID
- `member` (TEXT): 리소스 URI
- `klass` (TEXT): 클래스 URI (타입)
- `context` (TEXT): 컨텍스트
- `termcomb` (INTEGER): 용어 조합

**데이터 수**: 781,190개

**주요 CIM 클래스**:
- Terminal: 134,370개
- ConnectivityNode: 67,208개
- ACLineSegment: 28,690개
- ManualSwitch: 10,297개
- AutoSwitch: 5,236개
- CompositeSwitch: 1,916개

---

### 2. `kb_*_asserted_statements`
**목적**: URI 참조 관계 (Subject -> Predicate -> Object)

**스키마**:
- `id` (INTEGER): 고유 ID
- `subject` (TEXT): 주체 URI
- `predicate` (TEXT): 속성 URI
- `object` (TEXT): 객체 URI
- `context` (TEXT): 컨텍스트
- `termcomb` (INTEGER): 용어 조합

**데이터 수**: 1,028,060개

**주요 관계 (Predicates)**:
- `Terminal.ConductingEquipment`: 134,370개
- `Terminal.ConnectivityNode`: 134,370개
- `MeasurementValue.RemoteSource`: 208,378개
- `RemotePoint.RemoteUnit`: 208,378개
- `ACLineSegment.KEPCOLoad`: 28,690개

---

### 3. `kb_*_literal_statements`
**목적**: 리터럴 값 (숫자, 문자열, 불린 등)

**스키마**:
- `id` (INTEGER): 고유 ID
- `subject` (TEXT): 주체 URI
- `predicate` (TEXT): 속성 URI
- `object` (TEXT): 리터럴 값
- `context` (TEXT): 컨텍스트
- `termcomb` (INTEGER): 용어 조합
- `objlanguage` (VARCHAR): 언어
- `objdatatype` (VARCHAR): 데이터 타입

**데이터 수**: 2,789,684개

**주요 리터럴 속성**:
- `IdentifiedObject.name`: 752,500개
- `IdentifiedObject.aliasName`: 752,500개
- `Switch.open`: 16,414개 (0 또는 1)
- `Switch.normalOpen`: 16,414개 (0 또는 1)
- `AnalogValue.value`: 91,098개 (숫자)
- `DiscreteValue.value`: 117,280개 (0 또는 1)

---

## 데이터 모델 구조

### CIM (Common Information Model) 클래스 계층

```
GraphNode (기본)
  ├─ Equipment (전력 설비)
  │   ├─ Switch (스위치)
  │   │   ├─ ManualSwitch
  │   │   ├─ AutoSwitch
  │   │   └─ CompositeSwitch
  │   ├─ ACLineSegment (선로)
  │   └─ ConductingEquipment (기타 전도 설비)
  ├─ Terminal (터미널)
  ├─ ConnectivityNode (연결 노드)
  └─ Measurement (측정)
      └─ MeasurementValue
          ├─ AnalogValue (아날로그 값)
          └─ DiscreteValue (불린 값)
```

---

## 연결 구조 (Topology)

### 기본 연결 패턴

```
Equipment (Switch/Line)
    ↑
    | Terminal.ConductingEquipment
    |
Terminal
    ↓
    | Terminal.ConnectivityNode
    |
ConnectivityNode
    ↑
    | Terminal.ConnectivityNode
    |
Terminal
    ↓
    | Terminal.ConductingEquipment
    |
Equipment (다른 Switch/Line)
```

### 실제 예시

```
Switch (_11922100000118)
  ├─ Terminal (_1750218653248)
  │   └─ ConnectivityNode (_1752301427792)
  │       └─ Terminal (_1752303298240)
  │           └─ ACLineSegment (_11925100000097)
  │
  └─ Terminal (_1750218655536)
      └─ ConnectivityNode (_1750301236224)
          └─ Terminal (_1752321083328)
              └─ ACLineSegment (_11925100027404)
```

**핵심**: 같은 `ConnectivityNode`에 연결된 여러 `Terminal`들이 서로 다른 `Equipment`에 연결되어 있으면, 그 Equipment들은 전기적으로 연결되어 있습니다.

---

## 주요 속성 (Properties)

### Switch 속성
- `Switch.open`: 현재 상태 (0=닫힘, 1=열림) ⭐ **중요**
- `Switch.normalOpen`: 상시 개방 여부 (0=상시 닫힘, 1=상시 열림)
- `Switch.appOpen`: 외관상 개방 여부
- `Switch.isControllable`: 제어 가능 여부
- `IdentifiedObject.name`: 이름
- `IdentifiedObject.aliasName`: 별칭

### Measurement 속성
- `Measurement.measurementType`: 측정 타입 (예: "Voltage", "Current")
- `Measurement.MeasurementValue`: MeasurementValue 연결
- `MeasurementValue.RemoteSource`: Terminal 연결 (측정 위치)
- `MeasurementValue.phases`: 위상 정보
- `AnalogValue.value`: 숫자 값
- `AnalogValue.unit`: 단위 (예: "V", "A")
- `DiscreteValue.value`: 불린 값 (0 또는 1)

### Terminal 속성
- `Terminal.ConductingEquipment`: 연결된 Equipment
- `Terminal.ConnectivityNode`: 연결된 ConnectivityNode
- `Terminal.phases`: 위상 정보
- `Terminal.sequenceNumber`: 순서 번호

### ConnectivityNode 속성
- `ConnectivityNode.name`: 노드 이름
- `ConnectivityNode.Terminal`: 연결된 Terminal들 (역방향 관계)

---

## 데이터 의미 (Semantics)

### 1. 전력 시스템 위상 구조
- **Equipment**: 실제 전력 설비 (Switch, Line 등)
- **Terminal**: Equipment의 연결점 (입력/출력)
- **ConnectivityNode**: 전기적으로 같은 전위를 가진 노드
- **연결 규칙**: 같은 ConnectivityNode에 연결된 Equipment들은 전기적으로 연결됨

### 2. 측정 데이터
- **Measurement**: 측정 정의
- **MeasurementValue**: 실제 측정값 (AnalogValue 또는 DiscreteValue)
- **연결**: MeasurementValue는 Terminal에 연결되어 특정 위치의 측정값을 나타냄

### 3. 스위치 상태
- **Switch.open = 1**: 스위치가 열려있음 (전류 차단)
- **Switch.open = 0**: 스위치가 닫혀있음 (전류 통과)
- **Switch.normalOpen**: 설계상 상시 개방 여부

---

## 그래프 구축 전략

### Equipment-Oriented Graph

**목표**: Terminal과 ConnectivityNode를 런타임 객체로 생성하지 않고, Equipment들만 직접 연결

**알고리즘**:
1. 모든 Equipment 객체 생성 (Switch, ACLineSegment, ConductingEquipment)
2. Terminal 정보 수집: `Terminal.ConductingEquipment`, `Terminal.ConnectivityNode`
3. ConnectivityNode를 기준으로 Equipment 그룹화
4. 같은 ConnectivityNode에 연결된 Equipment들을 서로 이웃으로 연결

**결과**: 
```
Line_A ←→ Switch_1 ←→ Line_B
(중간에 Terminal/ConnectivityNode 없이 직접 연결)
```

---

## 탐색 알고리즘

### Level 1 선로 찾기
- **정의**: 고장 선로에 인접한 선로 중, **Open Switch**로 분리된 건전 선로
- **조건**: `Switch.open == 1` (현재 열려있음)

### Level 2 선로 찾기
- **정의**: Level 1 선로에 인접한 선로 중, **Open Switch**로 분리된 건전 선로
- **조건**: `Switch.open == 1` (현재 열려있음)

---

## 최적화 포인트

1. **메모리**: Terminal/ConnectivityNode 객체 생성 안 함
2. **속도**: 직접 포인터 연결로 빠른 탐색
3. **데이터 보존**: 모든 속성을 Equipment 객체에 임베딩
4. **RDF/XML 생성**: Equipment에 저장된 트리플 정보로 즉시 생성

---

## 다음 단계

이 분석을 바탕으로:
1. 깔끔한 그래프 구축 알고리즘 설계
2. 명확한 탐색 알고리즘 구현
3. 올바른 RDF/XML 생성 로직 작성

