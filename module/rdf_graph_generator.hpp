#ifndef RDF_GRAPH_GENERATOR_HPP
#define RDF_GRAPH_GENERATOR_HPP

#include <string>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <tuple>
#include <sqlite3.h>
#include "string_pool.hpp"

/**
 * RDF 그래프 엣지 구조 (문자열 대신 StringPool id 보관, 24 bytes)
 * (source URI) --[predicate]--> (target URI 또는 리터럴)
 */
struct RDFEdge {
    TermId source = 0;
    TermId predicate = 0;
    TermId target = 0;
    TermId objlanguage = 0;   // 0 = 없음
    TermId objdatatype = 0;   // 0 = 없음
    bool isLiteral = false;
};

/**
 * 엣지 인덱스 목록을 RDFEdge 참조로 순회하게 해주는 뷰 (복사 없음)
 */
class EdgeRange {
public:
    class iterator {
    public:
        iterator(const std::vector<RDFEdge>* edges, const uint32_t* p) : edges_(edges), p_(p) {}
        const RDFEdge& operator*() const { return (*edges_)[*p_]; }
        const RDFEdge* operator->() const { return &(*edges_)[*p_]; }
        iterator& operator++() { ++p_; return *this; }
        bool operator!=(const iterator& o) const { return p_ != o.p_; }
    private:
        const std::vector<RDFEdge>* edges_;
        const uint32_t* p_;
    };

    EdgeRange(const std::vector<RDFEdge>& edges, const std::vector<uint32_t>* ids)
        : edges_(&edges), ids_(ids) {}
    iterator begin() const { return iterator(edges_, ids_ ? ids_->data() : nullptr); }
    iterator end() const { return iterator(edges_, ids_ ? ids_->data() + ids_->size() : nullptr); }
    size_t size() const { return ids_ ? ids_->size() : 0; }
    bool empty() const { return size() == 0; }

private:
    const std::vector<RDFEdge>* edges_;
    const std::vector<uint32_t>* ids_;
};

/**
 * RDF 그래프 클래스
 * SQLite 데이터베이스에서 RDF 데이터를 로드하여 메모리에 그래프 구조로 저장
 */
class RDFGraph {
public:
    RDFGraph();
    ~RDFGraph();
    RDFGraph(const RDFGraph&) = delete;
    RDFGraph& operator=(const RDFGraph&) = delete;

    bool loadFromDatabase(const std::string& dbPath);

    // 문자열 <-> id
    const std::string& str(TermId id) const { return terms_.str(id); }
    TermId findTerm(const std::string& s) const { return terms_.find(s); }

    // 노드(URI) 조회. 리터럴은 노드 아님
    bool hasNode(TermId id) const { return id < isNode_.size() && isNode_[id]; }
    bool hasNode(const std::string& uri) const { return hasNode(findTerm(uri)); }
    const std::vector<TermId>& getAllNodes() const { return nodes_; }

    EdgeRange getOutgoingEdges(TermId node) const;
    EdgeRange getIncomingEdges(TermId node) const;
    EdgeRange getEdgesByPredicate(TermId predicate) const;
    EdgeRange getOutgoingEdges(const std::string& nodeUri) const { return getOutgoingEdges(findTerm(nodeUri)); }
    EdgeRange getIncomingEdges(const std::string& nodeUri) const { return getIncomingEdges(findTerm(nodeUri)); }
    EdgeRange getEdgesByPredicate(const std::string& p) const { return getEdgesByPredicate(findTerm(p)); }

    const std::vector<RDFEdge>& getAllEdges() const { return edges_; }

    std::vector<std::string> getResourcesByType(const std::string& type) const;
    std::vector<std::string> getPropertyValues(const std::string& uri, const std::string& predicate) const;
    std::vector<std::tuple<std::string, std::string, std::string>>
    getLiteralPropertyValues(const std::string& uri, const std::string& predicate) const;

    struct Statistics {
        size_t totalNodes = 0;
        size_t totalEdges = 0;
        size_t totalTriples = 0;
        size_t typeTriples = 0;
        size_t assertedTriples = 0;
        size_t literalTriples = 0;
        std::unordered_map<std::string, size_t> resourcesByType;
        std::unordered_map<std::string, size_t> edgesByPredicate;
    };
    Statistics getStatistics() const;

    void clear();

private:
    sqlite3* db_;

    StringPool terms_;
    std::vector<TermId> nodes_;      // URI 노드 id 목록
    std::vector<uint8_t> isNode_;    // TermId -> 노드 여부
    std::vector<RDFEdge> edges_;

    // TermId -> 엣지 인덱스
    std::vector<std::vector<uint32_t>> outgoing_;
    std::vector<std::vector<uint32_t>> incoming_;
    std::unordered_map<TermId, std::vector<uint32_t>> byPredicate_;

    size_t typeCount_ = 0;
    size_t assertedCount_ = 0;
    size_t literalCount_ = 0;

    bool loadTypeStatements();
    bool loadAssertedStatements();
    bool loadLiteralStatements();
    std::string findTableName(const std::string& pattern);

    void addNode(TermId id);
    void addEdge(TermId source, TermId predicate, TermId target, bool isLiteral,
                 TermId objlanguage = 0, TermId objdatatype = 0);
};

#endif // RDF_GRAPH_GENERATOR_HPP
