#ifndef RDF_GRAPH_GENERATOR_HPP
#define RDF_GRAPH_GENERATOR_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <sqlite3.h>

/**
 * RDF 트리플 구조체
 */
struct RDFTriple {
    std::string subject;
    std::string predicate;
    std::string object;
    std::string context;
    int termcomb;
    
    // 리터럴 전용 필드
    std::string objlanguage;
    std::string objdatatype;
    
    bool isLiteral;  // 리터럴 여부
    
    RDFTriple() : termcomb(0), isLiteral(false) {}
};

/**
 * RDF 그래프 엣지 구조
 * (source URI) --[predicate]--> (target URI)
 */
struct RDFEdge {
    std::string source;      // 출발 노드 URI
    std::string predicate;   // 엣지 타입 (predicate)
    std::string target;      // 도착 노드 URI (또는 리터럴 값)
    bool isLiteral;          // 리터럴 여부
    
    // 리터럴 전용 필드
    std::string objlanguage;
    std::string objdatatype;
    
    RDFEdge() : isLiteral(false) {}
};

/**
 * RDF 그래프 클래스
 * SQLite 데이터베이스에서 RDF 데이터를 로드하여 메모리에 그래프 구조로 저장
 */
class RDFGraph {
public:
    RDFGraph();
    ~RDFGraph();
    
    /**
     * SQLite 데이터베이스에서 RDF 데이터 로드
     * @param dbPath 데이터베이스 파일 경로
     * @return 성공 여부
     */
    bool loadFromDatabase(const std::string& dbPath);
    
    /**
     * 모든 노드 URI 조회
     * @return 노드 URI 집합
     */
    const std::unordered_set<std::string>& getAllNodes() const {
        return nodes_;
    }
    
    /**
     * 특정 노드에서 나가는 엣지 조회
     * @param nodeUri 노드 URI
     * @return 엣지 리스트
     */
    std::vector<RDFEdge> getOutgoingEdges(const std::string& nodeUri) const;
    
    /**
     * 특정 노드로 들어오는 엣지 조회
     * @param nodeUri 노드 URI
     * @return 엣지 리스트
     */
    std::vector<RDFEdge> getIncomingEdges(const std::string& nodeUri) const;
    
    /**
     * 특정 predicate를 가진 엣지 조회
     * @param predicate 엣지 타입 (predicate)
     * @return 엣지 리스트
     */
    std::vector<RDFEdge> getEdgesByPredicate(const std::string& predicate) const;
    
    /**
     * 두 노드 간의 엣지 조회
     * @param source 출발 노드 URI
     * @param target 도착 노드 URI
     * @return 엣지 리스트
     */
    std::vector<RDFEdge> getEdges(const std::string& source, const std::string& target) const;
    
    /**
     * 모든 엣지 조회
     * @return 엣지 리스트
     */
    const std::vector<RDFEdge>& getAllEdges() const {
        return edges_;
    }
    
    /**
     * 특정 타입의 모든 리소스 조회 (엣지 기반)
     * @param type 클래스 URI (예: "cim:Switch", "cim:ACLineSegment")
     * @return 해당 타입의 리소스 URI 리스트
     */
    std::vector<std::string> getResourcesByType(const std::string& type) const;
    
    /**
     * 리소스의 속성 값 조회 (URI 참조, 엣지 기반)
     * @param uri 리소스 URI
     * @param predicate 속성 URI
     * @return 속성 값 리스트 (URI)
     */
    std::vector<std::string> getPropertyValues(const std::string& uri, const std::string& predicate) const;
    
    /**
     * 리소스의 리터럴 속성 값 조회 (엣지 기반)
     * @param uri 리소스 URI
     * @param predicate 속성 URI
     * @return 속성 값 리스트 (value, datatype, language)
     */
    std::vector<std::tuple<std::string, std::string, std::string>> 
    getLiteralPropertyValues(const std::string& uri, const std::string& predicate) const;
    
    /**
     * 모든 트리플 조회
     * @return 트리플 리스트
     */
    const std::vector<RDFTriple>& getAllTriples() const {
        return allTriples_;
    }
    
    /**
     * 타입 트리플 조회
     * @return 타입 트리플 리스트
     */
    const std::vector<RDFTriple>& getTypeTriples() const {
        return typeTriples_;
    }
    
    /**
     * 어설션 트리플 조회
     * @return 어설션 트리플 리스트
     */
    const std::vector<RDFTriple>& getAssertedTriples() const {
        return assertedTriples_;
    }
    
    /**
     * 리터럴 트리플 조회
     * @return 리터럴 트리플 리스트
     */
    const std::vector<RDFTriple>& getLiteralTriples() const {
        return literalTriples_;
    }
    
    /**
     * 그래프 통계 정보
     */
    struct Statistics {
        size_t totalNodes;        // 총 노드 수 (URI + 리터럴)
        size_t totalEdges;        // 총 엣지 수
        size_t totalTriples;
        size_t typeTriples;
        size_t assertedTriples;
        size_t literalTriples;
        
        // 타입별 리소스 수
        std::unordered_map<std::string, size_t> resourcesByType;
        
        // Predicate별 엣지 수
        std::unordered_map<std::string, size_t> edgesByPredicate;
    };
    
    Statistics getStatistics() const;
    
    /**
     * TrackingAllocator 를 통해 추적된 그래프 메모리 사용량 조회
     * @return RDFGraph 내부 컨테이너들이 allocate 한 바이트 수
     */
    std::size_t getTrackedMemoryBytes() const;

    /**
     * 데이터베이스 연결 해제 및 메모리 정리
     */
    void clear();

private:
    sqlite3* db_;
    
    // 그래프 구조: 노드와 엣지
    std::unordered_set<std::string> nodes_;  // 모든 노드 URI 집합
    std::vector<RDFEdge> edges_;             // 모든 엣지 리스트
    
    // 인접 리스트: 노드 URI -> 나가는 엣지 인덱스 리스트
    std::unordered_map<std::string, std::vector<size_t>> outgoingEdges_;
    // 역방향 인접 리스트: 노드 URI -> 들어오는 엣지 인덱스 리스트
    std::unordered_map<std::string, std::vector<size_t>> incomingEdges_;
    
    // 모든 트리플 저장
    std::vector<RDFTriple> allTriples_;
    std::vector<RDFTriple> typeTriples_;
    std::vector<RDFTriple> assertedTriples_;
    std::vector<RDFTriple> literalTriples_;
    
    /**
     * 타입 트리플 로드 (kb_*_type_statements)
     */
    bool loadTypeStatements();
    
    /**
     * 어설션 트리플 로드 (kb_*_asserted_statements)
     */
    bool loadAssertedStatements();
    
    /**
     * 리터럴 트리플 로드 (kb_*_literal_statements)
     */
    bool loadLiteralStatements();
    
    /**
     * 테이블 이름 찾기 (kb_*_type_statements 등)
     */
    std::string findTableName(const std::string& pattern);
    
    /**
     * 노드 추가 (URI를 노드로 등록)
     */
    void addNode(const std::string& uri);
    
    /**
     * 엣지 추가 (predicate를 엣지로 등록)
     * @param source 출발 노드 URI
     * @param predicate 엣지 타입
     * @param target 도착 노드 URI 또는 리터럴 값
     * @param isLiteral 리터럴 여부
     * @param objlanguage 언어 (리터럴인 경우)
     * @param objdatatype 데이터 타입 (리터럴인 경우)
     */
    void addEdge(const std::string& source, const std::string& predicate, 
                 const std::string& target, bool isLiteral = false,
                 const std::string& objlanguage = "", const std::string& objdatatype = "");
};

#endif // RDF_GRAPH_GENERATOR_HPP

