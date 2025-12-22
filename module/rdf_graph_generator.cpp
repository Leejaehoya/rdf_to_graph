#include "rdf_graph_generator.hpp"
#include "graph_memory_tracker.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/task_info.h>
#elif __linux__
#include <sys/resource.h>
#endif

RDFGraph::RDFGraph() : db_(nullptr) {
}

RDFGraph::~RDFGraph() {
    clear();
}

void RDFGraph::clear() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
    nodes_.clear();
    edges_.clear();
    outgoingEdges_.clear();
    incomingEdges_.clear();
    allTriples_.clear();
    typeTriples_.clear();
    assertedTriples_.clear();
    literalTriples_.clear();
}

// 메모리 사용량 조회 함수 (플랫폼별)
static size_t getCurrentMemoryUsage() {
#ifdef __APPLE__
    struct task_basic_info info;
    mach_msg_type_number_t size = sizeof(info);
    kern_return_t kerr = task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info, &size);
    if (kerr == KERN_SUCCESS) {
        return info.resident_size;
    }
    return 0;
#elif __linux__
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return usage.ru_maxrss * 1024; // KB to bytes
    }
    return 0;
#else
    return 0;
#endif
}

bool RDFGraph::loadFromDatabase(const std::string& dbPath) {
    auto startTime = std::chrono::high_resolution_clock::now();
    // 그래프용 메모리 트래커 초기화
    GraphMemoryTracker::reset();
    size_t memoryBefore = getCurrentMemoryUsage();
    
    // 기존 연결이 있으면 닫기
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
    
    // 데이터베이스 열기
    int rc = sqlite3_open(dbPath.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::cerr << "데이터베이스 열기 실패: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }
    
    // 기존 데이터 정리
    nodes_.clear();
    edges_.clear();
    outgoingEdges_.clear();
    incomingEdges_.clear();
    allTriples_.clear();
    typeTriples_.clear();
    assertedTriples_.clear();
    literalTriples_.clear();
    
    // 각 테이블에서 데이터 로드
    if (!loadTypeStatements()) {
        std::cerr << "타입 트리플 로드 실패" << std::endl;
        return false;
    }
    
    if (!loadAssertedStatements()) {
        std::cerr << "어설션 트리플 로드 실패" << std::endl;
        return false;
    }
    
    if (!loadLiteralStatements()) {
        std::cerr << "리터럴 트리플 로드 실패" << std::endl;
        return false;
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    size_t memoryAfter = getCurrentMemoryUsage();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    size_t memoryUsed = memoryAfter > memoryBefore ? memoryAfter - memoryBefore : 0;
    
    // 로그 출력
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "========================================" << std::endl;
    std::cout << "RDF 그래프 생성 완료" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "소요 시간: " << duration.count() << " ms (" 
              << (duration.count() / 1000.0) << " 초)" << std::endl;
    std::cout << "메모리 사용량(프로세스 RSS 기준): " << (memoryUsed / 1024.0 / 1024.0) << " MB" << std::endl;
    std::cout << "그래프 추정 메모리(GraphMemoryTracker 기준): "
              << (getTrackedMemoryBytes() / 1024.0 / 1024.0) << " MB" << std::endl;
    std::cout << "총 트리플 수: " << allTriples_.size() << std::endl;
    std::cout << "  - 타입 트리플: " << typeTriples_.size() << std::endl;
    std::cout << "  - 어설션 트리플: " << assertedTriples_.size() << std::endl;
    std::cout << "  - 리터럴 트리플: " << literalTriples_.size() << std::endl;
    std::cout << "총 노드 수: " << nodes_.size() << std::endl;
    std::cout << "총 엣지 수: " << edges_.size() << std::endl;
    std::cout << "========================================" << std::endl;
    
    return true;
}

std::string RDFGraph::findTableName(const std::string& pattern) {
    if (!db_) {
        return "";
    }
    
    std::string query = "SELECT name FROM sqlite_master WHERE type='table' AND name LIKE '" + pattern + "'";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return "";
    }
    
    std::string tableName;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (name) {
            tableName = name;
        }
    }
    
    sqlite3_finalize(stmt);
    return tableName;
}

bool RDFGraph::loadTypeStatements() {
    if (!db_) {
        return false;
    }
    
    // 테이블 이름 찾기 (kb_*_type_statements)
    std::string tableName = findTableName("kb_%_type_statements");
    if (tableName.empty()) {
        std::cerr << "타입 스테이트먼트 테이블을 찾을 수 없습니다." << std::endl;
        return false;
    }
    
    std::string query = "SELECT id, member, klass, context, termcomb FROM " + tableName;
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "쿼리 준비 실패: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        RDFTriple triple;
        
        // member (subject) - 노드 URI
        const char* member = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (!member) continue;
        triple.subject = member;
        
        // klass (object, 타입) - 노드 URI
        const char* klass = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        if (!klass) continue;
        triple.object = klass;
        
        // context
        const char* context = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (context) {
            triple.context = context;
        }
        
        // termcomb
        triple.termcomb = sqlite3_column_int(stmt, 4);
        
        // predicate는 rdf:type
        triple.predicate = "rdf:type";
        triple.isLiteral = false;
        
        typeTriples_.push_back(triple);
        allTriples_.push_back(triple);
        // 트리플 저장에 따른 대략적인 메모리 증가 추적
        GraphMemoryTracker::add(sizeof(RDFTriple));
        
        // 그래프 구조: (member:uri) --[rdf:type]--> (klass:uri)
        addNode(triple.subject);  // member URI를 노드로 추가
        addNode(triple.object);  // klass URI를 노드로 추가
        addEdge(triple.subject, "rdf:type", triple.object, false);  // 엣지 추가
    }
    
    sqlite3_finalize(stmt);
    return true;
}

bool RDFGraph::loadAssertedStatements() {
    if (!db_) {
        return false;
    }
    
    // 테이블 이름 찾기 (kb_*_asserted_statements)
    std::string tableName = findTableName("kb_%_asserted_statements");
    if (tableName.empty()) {
        std::cerr << "어설션 스테이트먼트 테이블을 찾을 수 없습니다." << std::endl;
        return false;
    }
    
    std::string query = "SELECT id, subject, predicate, object, context, termcomb FROM " + tableName;
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "쿼리 준비 실패: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        RDFTriple triple;
        
        // subject - 노드 URI
        const char* subject = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (!subject) continue;
        triple.subject = subject;
        
        // predicate - 엣지 타입
        const char* predicate = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        if (!predicate) continue;
        triple.predicate = predicate;
        
        // object - 노드 URI
        const char* object = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (!object) continue;
        triple.object = object;
        
        // context
        const char* context = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (context) {
            triple.context = context;
        }
        
        // termcomb
        triple.termcomb = sqlite3_column_int(stmt, 5);
        
        triple.isLiteral = false;
        
        assertedTriples_.push_back(triple);
        allTriples_.push_back(triple);
        // 트리플 저장에 따른 대략적인 메모리 증가 추적
        GraphMemoryTracker::add(sizeof(RDFTriple));
        
        // 그래프 구조: (subject:uri) --[predicate]--> (object:uri)
        addNode(triple.subject);  // subject URI를 노드로 추가
        addNode(triple.object);  // object URI를 노드로 추가
        addEdge(triple.subject, triple.predicate, triple.object, false);  // 엣지 추가
    }
    
    sqlite3_finalize(stmt);
    return true;
}

bool RDFGraph::loadLiteralStatements() {
    if (!db_) {
        return false;
    }
    
    // 테이블 이름 찾기 (kb_*_literal_statements)
    std::string tableName = findTableName("kb_%_literal_statements");
    if (tableName.empty()) {
        std::cerr << "리터럴 스테이트먼트 테이블을 찾을 수 없습니다." << std::endl;
        return false;
    }
    
    std::string query = "SELECT id, subject, predicate, object, context, termcomb, objlanguage, objdatatype FROM " + tableName;
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "쿼리 준비 실패: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        RDFTriple triple;
        
        // subject - 노드 URI
        const char* subject = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (!subject) continue;
        triple.subject = subject;
        
        // predicate - 엣지 타입
        const char* predicate = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        if (!predicate) continue;
        triple.predicate = predicate;
        
        // object (리터럴 값) - 노드가 아님
        const char* object = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (!object) continue;
        triple.object = object;
        
        // context
        const char* context = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (context) {
            triple.context = context;
        }
        
        // termcomb
        triple.termcomb = sqlite3_column_int(stmt, 5);
        
        // objlanguage
        const char* objlanguage = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        if (objlanguage) {
            triple.objlanguage = objlanguage;
        }
        
        // objdatatype
        const char* objdatatype = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        if (objdatatype) {
            triple.objdatatype = objdatatype;
        }
        
        triple.isLiteral = true;
        
        literalTriples_.push_back(triple);
        allTriples_.push_back(triple);
        // 트리플 저장에 따른 대략적인 메모리 증가 추적
        GraphMemoryTracker::add(sizeof(RDFTriple));
        
        // 그래프 구조: (subject:uri) --[predicate]--> (literal value)
        // 리터럴 값도 노드로 추가
        addNode(triple.subject);  // subject URI를 노드로 추가
        addNode(triple.object);   // 리터럴 값도 노드로 추가
        addEdge(triple.subject, triple.predicate, triple.object, true, 
                triple.objlanguage, triple.objdatatype);  // 리터럴 엣지 추가
    }
    
    sqlite3_finalize(stmt);
    return true;
}

void RDFGraph::addNode(const std::string& uri) {
    if (uri.empty()) return;
    auto res = nodes_.insert(uri);
    if (res.second) {
        // 새로운 노드 URI가 추가될 때 문자열/노드 구조에 대한 대략적인 메모리 추정
        GraphMemoryTracker::add(sizeof(std::string) + uri.size());
    }
}

void RDFGraph::addEdge(const std::string& source, const std::string& predicate, 
                       const std::string& target, bool isLiteral,
                       const std::string& objlanguage, const std::string& objdatatype) {
    if (source.empty() || predicate.empty() || target.empty()) return;
    
    // 엣지 생성
    RDFEdge edge;
    edge.source = source;
    edge.predicate = predicate;
    edge.target = target;
    edge.isLiteral = isLiteral;
    edge.objlanguage = objlanguage;
    edge.objdatatype = objdatatype;
    
    // 엣지 리스트에 추가
    size_t edgeIndex = edges_.size();
    edges_.push_back(edge);
    // 엣지 구조 및 인덱스 저장에 대한 대략적인 메모리 추정
    GraphMemoryTracker::add(sizeof(RDFEdge) + sizeof(size_t) * 2);
    
    // 인접 리스트 업데이트
    outgoingEdges_[source].push_back(edgeIndex);
    
    // 리터럴인 경우에도 역방향 인접 리스트 업데이트 (리터럴도 노드이므로)
    incomingEdges_[target].push_back(edgeIndex);
}

std::vector<std::string> RDFGraph::getResourcesByType(const std::string& type) const {
    // 엣지 기반으로 재구현: rdf:type 엣지를 통해 타입별 노드 조회
    std::vector<std::string> result;
    std::vector<RDFEdge> typeEdges = getEdgesByPredicate("rdf:type");
    
    for (const auto& edge : typeEdges) {
        if (edge.target == type && !edge.isLiteral) {
            result.push_back(edge.source);
        }
    }
    
    return result;
}

std::vector<std::string> RDFGraph::getPropertyValues(const std::string& uri, const std::string& predicate) const {
    // 엣지 기반으로 재구현: 나가는 엣지 중 predicate가 일치하는 것의 target 반환
    std::vector<std::string> result;
    std::vector<RDFEdge> edges = getOutgoingEdges(uri);
    
    for (const auto& edge : edges) {
        if (edge.predicate == predicate && !edge.isLiteral) {
            result.push_back(edge.target);
        }
    }
    
    return result;
}

std::vector<std::tuple<std::string, std::string, std::string>> 
RDFGraph::getLiteralPropertyValues(const std::string& uri, const std::string& predicate) const {
    // 엣지 기반으로 재구현: 나가는 엣지 중 predicate가 일치하는 리터럴 엣지 반환
    std::vector<std::tuple<std::string, std::string, std::string>> result;
    std::vector<RDFEdge> edges = getOutgoingEdges(uri);
    
    for (const auto& edge : edges) {
        if (edge.predicate == predicate && edge.isLiteral) {
            result.push_back(std::make_tuple(edge.target, edge.objdatatype, edge.objlanguage));
        }
    }
    
    return result;
}

std::vector<RDFEdge> RDFGraph::getOutgoingEdges(const std::string& nodeUri) const {
    std::vector<RDFEdge> result;
    
    auto it = outgoingEdges_.find(nodeUri);
    if (it == outgoingEdges_.end()) {
        return result;
    }
    
    for (size_t edgeIndex : it->second) {
        if (edgeIndex < edges_.size()) {
            result.push_back(edges_[edgeIndex]);
        }
    }
    
    return result;
}

std::vector<RDFEdge> RDFGraph::getIncomingEdges(const std::string& nodeUri) const {
    std::vector<RDFEdge> result;
    
    auto it = incomingEdges_.find(nodeUri);
    if (it == incomingEdges_.end()) {
        return result;
    }
    
    for (size_t edgeIndex : it->second) {
        if (edgeIndex < edges_.size()) {
            result.push_back(edges_[edgeIndex]);
        }
    }
    
    return result;
}

std::vector<RDFEdge> RDFGraph::getEdgesByPredicate(const std::string& predicate) const {
    std::vector<RDFEdge> result;
    
    for (const auto& edge : edges_) {
        if (edge.predicate == predicate) {
            result.push_back(edge);
        }
    }
    
    return result;
}

std::vector<RDFEdge> RDFGraph::getEdges(const std::string& source, const std::string& target) const {
    std::vector<RDFEdge> result;
    
    auto it = outgoingEdges_.find(source);
    if (it == outgoingEdges_.end()) {
        return result;
    }
    
    for (size_t edgeIndex : it->second) {
        if (edgeIndex < edges_.size()) {
            const RDFEdge& edge = edges_[edgeIndex];
            if (edge.target == target) {
                result.push_back(edge);
            }
        }
    }
    
    return result;
}

RDFGraph::Statistics RDFGraph::getStatistics() const {
    Statistics stats;
    stats.totalNodes = nodes_.size();
    stats.totalEdges = edges_.size();
    stats.totalTriples = allTriples_.size();
    stats.typeTriples = typeTriples_.size();
    stats.assertedTriples = assertedTriples_.size();
    stats.literalTriples = literalTriples_.size();
    
    // 타입별 리소스 수 계산 (rdf:type 엣지 기반)
    std::vector<RDFEdge> typeEdges = getEdgesByPredicate("rdf:type");
    for (const auto& edge : typeEdges) {
        if (!edge.isLiteral) {
            stats.resourcesByType[edge.target]++;
        }
    }
    
    // Predicate별 엣지 수 계산
    for (const auto& edge : edges_) {
        stats.edgesByPredicate[edge.predicate]++;
    }
    
    return stats;
}

std::size_t RDFGraph::getTrackedMemoryBytes() const {
    return GraphMemoryTracker::current();
}

