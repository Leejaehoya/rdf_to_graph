#include "rdf_graph_generator.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <cstdio>
#include <sys/resource.h>
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/task_info.h>
#elif __linux__
#include <unistd.h>
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
    terms_.clear();
    nodes_.clear();
    isNode_.clear();
    edges_.clear();
    outgoing_.clear();
    incoming_.clear();
    byPredicate_.clear();
    typeCount_ = assertedCount_ = literalCount_ = 0;
}

// 현재 RSS (bytes)
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
    // ru_maxrss 는 peak 값이라 현재 RSS 는 /proc/self/statm 에서 읽음
    long pages = 0, resident = 0;
    FILE* f = std::fopen("/proc/self/statm", "r");
    if (!f) return 0;
    int n = std::fscanf(f, "%ld %ld", &pages, &resident);
    std::fclose(f);
    if (n != 2) return 0;
    return static_cast<size_t>(resident) * static_cast<size_t>(sysconf(_SC_PAGESIZE));
#else
    return 0;
#endif
}

// ru_maxrss 는 프로세스 peak. Linux 는 KB, macOS 는 bytes.
static size_t getPeakRssBytes() {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#ifdef __APPLE__
    return static_cast<size_t>(usage.ru_maxrss);
#else
    return static_cast<size_t>(usage.ru_maxrss) * 1024;
#endif
}

static const char* columnText(sqlite3_stmt* stmt, int col) {
    return reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
}

bool RDFGraph::loadFromDatabase(const std::string& dbPath) {
    auto startTime = std::chrono::steady_clock::now();
    size_t memoryBefore = getCurrentMemoryUsage();

    clear();

    int rc = sqlite3_open_v2(dbPath.c_str(), &db_, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "데이터베이스 열기 실패: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }

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

    // 로드 끝났으면 DB 는 더 안 씀
    sqlite3_close(db_);
    db_ = nullptr;

    auto endTime = std::chrono::steady_clock::now();
    size_t memoryAfter = getCurrentMemoryUsage();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    size_t memoryUsed = memoryAfter > memoryBefore ? memoryAfter - memoryBefore : 0;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "========================================" << std::endl;
    std::cout << "RDF 그래프 생성 완료" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "소요 시간: " << duration.count() << " ms ("
              << (duration.count() / 1000.0) << " 초)" << std::endl;
    std::cout << "메모리 사용량(현재 RSS 증가분): " << (memoryUsed / 1024.0 / 1024.0) << " MB" << std::endl;
    std::cout << "peak RSS: " << (getPeakRssBytes() / 1024.0 / 1024.0) << " MB" << std::endl;
    std::cout << "총 트리플 수: " << (typeCount_ + assertedCount_ + literalCount_) << std::endl;
    std::cout << "  - 타입 트리플: " << typeCount_ << std::endl;
    std::cout << "  - 어설션 트리플: " << assertedCount_ << std::endl;
    std::cout << "  - 리터럴 트리플: " << literalCount_ << std::endl;
    std::cout << "총 노드 수: " << nodes_.size() << std::endl;
    std::cout << "총 엣지 수: " << edges_.size() << std::endl;
    std::cout << "고유 문자열 수: " << terms_.size() << std::endl;
    std::cout << "========================================" << std::endl;

    return true;
}

std::string RDFGraph::findTableName(const std::string& pattern) {
    if (!db_) {
        return "";
    }

    const char* query = "SELECT name FROM sqlite_master WHERE type='table' AND name LIKE ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, query, -1, &stmt, nullptr) != SQLITE_OK) {
        return "";
    }
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);

    std::string tableName;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* name = columnText(stmt, 0);
        if (name) {
            tableName = name;
        }
    }
    sqlite3_finalize(stmt);
    return tableName;
}

bool RDFGraph::loadTypeStatements() {
    std::string tableName = findTableName("kb_%_type_statements");
    if (tableName.empty()) {
        std::cerr << "타입 스테이트먼트 테이블을 찾을 수 없습니다." << std::endl;
        return false;
    }

    std::string query = "SELECT member, klass FROM " + tableName;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "쿼리 준비 실패: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }

    const TermId rdfType = terms_.intern("rdf:type");
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char* member = columnText(stmt, 0);
        const char* klass = columnText(stmt, 1);
        if (!member || !klass) continue;

        TermId s = terms_.intern(member);
        TermId o = terms_.intern(klass);
        ++typeCount_;
        addNode(s);
        addNode(o);
        addEdge(s, rdfType, o, false);
    }

    if (rc != SQLITE_DONE) {
        std::cerr << "쿼리 실행 실패: " << sqlite3_errmsg(db_) << std::endl;
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_finalize(stmt);
    return true;
}

bool RDFGraph::loadAssertedStatements() {
    std::string tableName = findTableName("kb_%_asserted_statements");
    if (tableName.empty()) {
        std::cerr << "어설션 스테이트먼트 테이블을 찾을 수 없습니다." << std::endl;
        return false;
    }

    std::string query = "SELECT subject, predicate, object FROM " + tableName;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "쿼리 준비 실패: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }

    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char* subject = columnText(stmt, 0);
        const char* predicate = columnText(stmt, 1);
        const char* object = columnText(stmt, 2);
        if (!subject || !predicate || !object) continue;

        TermId s = terms_.intern(subject);
        TermId p = terms_.intern(predicate);
        TermId o = terms_.intern(object);
        ++assertedCount_;
        addNode(s);
        addNode(o);
        addEdge(s, p, o, false);
    }

    if (rc != SQLITE_DONE) {
        std::cerr << "쿼리 실행 실패: " << sqlite3_errmsg(db_) << std::endl;
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_finalize(stmt);
    return true;
}

bool RDFGraph::loadLiteralStatements() {
    std::string tableName = findTableName("kb_%_literal_statements");
    if (tableName.empty()) {
        std::cerr << "리터럴 스테이트먼트 테이블을 찾을 수 없습니다." << std::endl;
        return false;
    }

    std::string query = "SELECT subject, predicate, object, objlanguage, objdatatype FROM " + tableName;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "쿼리 준비 실패: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }

    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char* subject = columnText(stmt, 0);
        const char* predicate = columnText(stmt, 1);
        const char* object = columnText(stmt, 2);
        if (!subject || !predicate || !object) continue;
        const char* lang = columnText(stmt, 3);
        const char* dtype = columnText(stmt, 4);

        TermId s = terms_.intern(subject);
        TermId p = terms_.intern(predicate);
        TermId o = terms_.intern(object);
        ++literalCount_;
        addNode(s);  // 리터럴 값은 노드로 안 만듦
        addEdge(s, p, o, true, lang ? terms_.intern(lang) : 0, dtype ? terms_.intern(dtype) : 0);
    }

    if (rc != SQLITE_DONE) {
        std::cerr << "쿼리 실행 실패: " << sqlite3_errmsg(db_) << std::endl;
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_finalize(stmt);
    return true;
}

void RDFGraph::addNode(TermId id) {
    if (id == 0) return;  // 빈 문자열
    if (id >= isNode_.size()) isNode_.resize(terms_.size(), 0);
    if (!isNode_[id]) {
        isNode_[id] = 1;
        nodes_.push_back(id);
    }
}

void RDFGraph::addEdge(TermId source, TermId predicate, TermId target, bool isLiteral,
                       TermId objlanguage, TermId objdatatype) {
    if (source == 0 || predicate == 0 || target == 0) return;

    uint32_t idx = static_cast<uint32_t>(edges_.size());
    edges_.push_back(RDFEdge{source, predicate, target, objlanguage, objdatatype, isLiteral});

    if (outgoing_.size() < terms_.size()) outgoing_.resize(terms_.size());
    outgoing_[source].push_back(idx);
    byPredicate_[predicate].push_back(idx);
    if (!isLiteral) {
        if (incoming_.size() < terms_.size()) incoming_.resize(terms_.size());
        incoming_[target].push_back(idx);
    }
}

EdgeRange RDFGraph::getOutgoingEdges(TermId node) const {
    if (node >= outgoing_.size()) return EdgeRange(edges_, nullptr);
    return EdgeRange(edges_, &outgoing_[node]);
}

EdgeRange RDFGraph::getIncomingEdges(TermId node) const {
    if (node >= incoming_.size()) return EdgeRange(edges_, nullptr);
    return EdgeRange(edges_, &incoming_[node]);
}

EdgeRange RDFGraph::getEdgesByPredicate(TermId predicate) const {
    auto it = byPredicate_.find(predicate);
    return EdgeRange(edges_, it == byPredicate_.end() ? nullptr : &it->second);
}

std::vector<std::string> RDFGraph::getResourcesByType(const std::string& type) const {
    std::vector<std::string> result;
    TermId t = findTerm(type);
    for (const auto& edge : getEdgesByPredicate(std::string("rdf:type"))) {
        if (edge.target == t && !edge.isLiteral) {
            result.push_back(str(edge.source));
        }
    }
    return result;
}

std::vector<std::string> RDFGraph::getPropertyValues(const std::string& uri, const std::string& predicate) const {
    std::vector<std::string> result;
    TermId p = findTerm(predicate);
    for (const auto& edge : getOutgoingEdges(uri)) {
        if (edge.predicate == p && !edge.isLiteral) {
            result.push_back(str(edge.target));
        }
    }
    return result;
}

std::vector<std::tuple<std::string, std::string, std::string>>
RDFGraph::getLiteralPropertyValues(const std::string& uri, const std::string& predicate) const {
    std::vector<std::tuple<std::string, std::string, std::string>> result;
    TermId p = findTerm(predicate);
    for (const auto& edge : getOutgoingEdges(uri)) {
        if (edge.predicate == p && edge.isLiteral) {
            result.emplace_back(str(edge.target), str(edge.objdatatype), str(edge.objlanguage));
        }
    }
    return result;
}

RDFGraph::Statistics RDFGraph::getStatistics() const {
    Statistics stats;
    stats.totalNodes = nodes_.size();
    stats.totalEdges = edges_.size();
    stats.totalTriples = typeCount_ + assertedCount_ + literalCount_;
    stats.typeTriples = typeCount_;
    stats.assertedTriples = assertedCount_;
    stats.literalTriples = literalCount_;

    for (const auto& edge : getEdgesByPredicate(std::string("rdf:type"))) {
        if (!edge.isLiteral) {
            stats.resourcesByType[str(edge.target)]++;
        }
    }
    for (const auto& kv : byPredicate_) {
        stats.edgesByPredicate[str(kv.first)] = kv.second.size();
    }
    return stats;
}
