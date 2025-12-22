# 데이터 파일 안내

이 폴더는 RDF 그래프 데이터를 저장하는 위치입니다.

## 필요한 파일

프로그램을 실행하려면 다음 파일들이 필요합니다:

### 1. `rdfstore.db`
- **형식**: SQLite 데이터베이스 (KB 형식)
- **용도**: RDF 트리플 데이터 저장
- **구조**:
  - `kb_*_type_statements`: 리소스 타입 정의
  - `kb_*_asserted_statements`: URI 참조 관계
  - `kb_*_literal_statements`: 리터럴 값

### 2. `topology_data.xml` (선택사항)
- **형식**: RDF/XML
- **용도**: 원본 토폴로지 데이터 (참고용)

## 샘플 데이터 준비 방법

### 방법 1: 기존 데이터베이스 사용
기존에 준비된 `rdfstore.db` 파일을 이 폴더에 배치하세요.

### 방법 2: XML에서 데이터베이스 생성
`topology_data.xml` 파일이 있다면, RDF 파서를 사용하여 SQLite 데이터베이스로 변환할 수 있습니다.

## 파일 위치

프로그램은 기본적으로 `data/rdfstore.db` 경로에서 데이터베이스를 찾습니다.
`main.cpp`에서 경로를 수정할 수 있습니다:

```cpp
std::string dbPath = "data/rdfstore.db";
```

## 주의사항

- 데이터베이스 파일은 민감한 정보를 포함할 수 있으므로 Git에 커밋하지 마세요.
- 대용량 파일이므로 `.gitignore`에 포함되어 있습니다.
- 실제 데이터 파일은 별도로 관리하세요.

