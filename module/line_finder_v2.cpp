#include "line_finder_v2.hpp"
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <utility>
#include "log_util.hpp"

static double elapsedMs(std::chrono::steady_clock::time_point start,
                        std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

static std::string formatMs(double ms) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3) << ms;
    return oss.str();
}

// ===== LineFinderV2 구현 =====

LineFinderV2::LineFinderV2(const RDFGraph& graph, std::string baseUri)
    : graph_(graph), baseUri_(std::move(baseUri)) {
    using clk = std::chrono::steady_clock;
    auto topoStart = clk::now();
    buildTypeAndPropertyCaches();
    buildCimTopology();
    auto topoEnd = clk::now();

    appendLog("[v2] topology_build_ms=" + formatMs(elapsedMs(topoStart, topoEnd)));
}

LineFinderV2::~LineFinderV2() {
    // 데이터 삭제 (생성 역순은 아니어도 무방하지만 명시적으로 구분)
    for (auto* d : allSwitchData_) {
        delete d;
    }
    for (auto* d : allACLineData_) {
        delete d;
    }
    for (auto* d : allFeederData_) {
        delete d;
    }
    for (auto* n : allEquipNodes_) {
        delete n;
    }
}

// ===== 타입 / 속성 캐시 =====

void LineFinderV2::buildTypeAndPropertyCaches() {
    // rdf:type 엣지를 통해 uri -> 타입 URI 목록 구성
    for (const auto& edge : graph_.getEdgesByPredicate("rdf:type")) {
        if (edge.isLiteral) continue;
        resourceTypes_[edge.source].push_back(edge.target);
    }

    // KEPCOFeeder 타입 URI 수집
    for (const auto& kv : resourceTypes_) {
        TermId uri = kv.first;
        for (TermId t : kv.second) {
            if (isKepcoFeederType(graph_.str(t))) {
                feederUris_.insert(uri);
                break;
            }
        }
    }

    // Switch.open 리터럴 값 캐시
    const std::string switchOpenPredicate =
        "http://iec.ch/TC57/1999/rdf-schema-extensions-19990926#Switch.open";

    for (const auto& edge : graph_.getEdgesByPredicate(switchOpenPredicate)) {
        if (!edge.isLiteral) continue;
        try {
            int v = std::stoi(graph_.str(edge.target));
            if (v == 0 || v == 1) {
                switchOpenCache_[edge.source] = v;
            }
        } catch (...) {
            // 무시
        }
    }
}

bool LineFinderV2::isSwitchType(const std::string& type) const {
    static const std::unordered_set<std::string> switchTypes = {
        // 1999 확장 스키마
        "http://iec.ch/TC57/1999/rdf-schema-extensions-19990926#ManualSwitch",
        "http://iec.ch/TC57/1999/rdf-schema-extensions-19990926#AutoSwitch",
        "http://iec.ch/TC57/1999/rdf-schema-extensions-19990926#CompositeSwitch",
        "http://iec.ch/TC57/1999/rdf-schema-extensions-19990926#AutoSectionSwitch",
        "http://iec.ch/TC57/1999/rdf-schema-extensions-19990926#CircuitBreaker",
        // CIM16 스키마
        "http://iec.ch/TC57/2013/CIM-schema-cim16#Switch",
        "http://iec.ch/TC57/2013/CIM-schema-cim16#KRecloser",
        "http://iec.ch/TC57/2013/CIM-schema-cim16#CutOutSwitch",
        "http://iec.ch/TC57/2013/CIM-schema-cim16#CircuitBreaker",
        "http://iec.ch/TC57/2013/CIM-schema-cim16#EpoxyInsulatedFaultInterrupters"
    };

    if (switchTypes.find(type) != switchTypes.end()) {
        return true;
    }
    return false;
}

bool LineFinderV2::isKepcoFeederType(const std::string& type) const {
    // "KepcoFeeder" 또는 "KEPCOFeeder" 문자열이 포함되어 있으면 Feeder 로 간주
    if (type.find("KEPCOFeeder") != std::string::npos ||
        type.find("KepcoFeeder") != std::string::npos) {
        return true;
    }
    return false;
}

bool LineFinderV2::isACLineType(const std::string& type) const {
    // ACLineSegment 계열 타입인지 간단히 문자열로 판별
    if (type.find("ACLineSegment") != std::string::npos) {
        return true;
    }
    return false;
}

LineFinderV2::CNId LineFinderV2::getOrCreateCNId(TermId cnUri) {
    auto it = cnUriToId_.find(cnUri);
    if (it != cnUriToId_.end()) {
        return it->second;
    }
    CNId id = static_cast<CNId>(cnIdToUri_.size());
    cnUriToId_[cnUri] = id;
    cnIdToUri_.push_back(cnUri);
    return id;
}

LineFinderV2::EquipmentNode* LineFinderV2::createEquipmentNode(TermId uri,
                                                               EquipmentKind kind) {
    auto it = uriToEquipNode_.find(uri);
    if (it != uriToEquipNode_.end()) {
        return it->second;
    }

    EquipmentNode* node = new EquipmentNode(kind, uri);
    void* dataPtr = nullptr;

    switch (kind) {
        case EquipmentKind::Switch: {
            SwitchData* d = new SwitchData();
            auto itOpen = switchOpenCache_.find(uri);
            if (itOpen != switchOpenCache_.end()) {
                d->openValue = itOpen->second;
            }
            dataPtr = static_cast<void*>(d);
            allSwitchData_.push_back(d);
            break;
        }
        case EquipmentKind::ACLine: {
            ACLineData* d = new ACLineData();
            dataPtr = static_cast<void*>(d);
            allACLineData_.push_back(d);
            break;
        }
        case EquipmentKind::Feeder: {
            FeederData* d = new FeederData();
            dataPtr = static_cast<void*>(d);
            allFeederData_.push_back(d);
            break;
        }
    }

    node->data = dataPtr;
    uriToEquipNode_[uri] = node;
    allEquipNodes_.push_back(node);
    return node;
}

void LineFinderV2::linkEquipmentsViaConnectivityNodes() {
    // 각 CN 에 연결된 장비들을 서로 인접 장비로 연결.
    // 같은 쌍이 CN 을 여러 개 공유하면 이웃이 중복되지 않게 한다.
    for (auto& kv : cnToEquipments_) {
        std::vector<EquipmentNode*>& eqs = kv.second;
        const size_t n = eqs.size();
        for (size_t i = 0; i < n; ++i) {
            EquipmentNode* a = eqs[i];
            if (!a) continue;
            for (size_t j = 0; j < n; ++j) {
                if (i == j) continue;
                EquipmentNode* b = eqs[j];
                if (!b || a == b) continue;
                bool already = false;
                for (EquipmentNode* nb : a->neighbors) {
                    if (nb == b) {
                        already = true;
                        break;
                    }
                }
                if (!already) a->neighbors.push_back(b);
            }
        }
    }
}

// ===== CIM 장비 그래프 구성 =====

void LineFinderV2::buildCimTopology() {
    // Terminal - CE / CN 매핑
    std::unordered_map<TermId, std::vector<TermId>> termToCeq;
    std::unordered_map<TermId, TermId> termToCn;
    std::unordered_map<TermId, std::vector<TermId>> ceqToTerms;

    const std::string termCeqPredicate =
        "http://iec.ch/TC57/1999/rdf-schema-extensions-19990926#Terminal.ConductingEquipment";
    const std::string termCnPredicate =
        "http://iec.ch/TC57/1999/rdf-schema-extensions-19990926#Terminal.ConnectivityNode";

    // Terminal.ConductingEquipment
    {
        for (const auto& edge : graph_.getEdgesByPredicate(termCeqPredicate)) {
            if (edge.isLiteral) continue;
            TermId termUri = edge.source;
            TermId ceqUri  = edge.target;
            termToCeq[termUri].push_back(ceqUri);
            ceqToTerms[ceqUri].push_back(termUri);
        }
    }

    // Terminal.ConnectivityNode
    {
        for (const auto& edge : graph_.getEdgesByPredicate(termCnPredicate)) {
            if (edge.isLiteral) continue;
            TermId termUri = edge.source;
            TermId cnUri   = edge.target;
            termToCn[termUri] = cnUri;
        }
    }

    // 1단계: CEQ URI 별로 장비 노드 생성 (Switch / ACLine / 기타는 일단 ACLine 스타일로 취급)
    for (const auto& kv : ceqToTerms) {
        TermId ceqUri = kv.first;

        EquipmentKind kind = EquipmentKind::ACLine; // 기본값
        auto itType = resourceTypes_.find(ceqUri);
        if (itType != resourceTypes_.end()) {
            bool isSw = false;
            bool isLine = false;
            for (TermId tid : itType->second) {
                const std::string& t = graph_.str(tid);
                if (isSwitchType(t)) {
                    isSw = true;
                }
                if (isACLineType(t)) {
                    isLine = true;
                }
            }
            if (isSw) {
                kind = EquipmentKind::Switch;
            } else if (isLine) {
                kind = EquipmentKind::ACLine;
            } else {
                // 그 외 ConductingEquipment 도 현재는 ACLine 과 동일하게 취급
                kind = EquipmentKind::ACLine;
            }
        }

        createEquipmentNode(ceqUri, kind);
    }

    // 2단계: Terminal을 통해 CN ↔ 장비 매핑 구성
    for (const auto& kv : termToCeq) {
        TermId termUri = kv.first;
        const std::vector<TermId>& ceqUris = kv.second;

        auto itCn = termToCn.find(termUri);
        if (itCn == termToCn.end()) {
            continue; // CN 이 없는 Terminal 은 스킵
        }
        TermId cnUri = itCn->second;
        CNId cnId = getOrCreateCNId(cnUri);

        for (TermId ceqUri : ceqUris) {
            auto itNode = uriToEquipNode_.find(ceqUri);
            if (itNode == uriToEquipNode_.end()) {
                continue;
            }
            EquipmentNode* node = itNode->second;
            cnToEquipments_[cnId].push_back(node);
        }
    }

    // 3단계: CN 을 매개로 장비 간 인접 관계 연결 (Switch / ACLine / 기타 CEQ)
    linkEquipmentsViaConnectivityNodes();

    // 4단계: Feeder 와 CircuitBreaker 간 인접 관계 연결
    const std::string feederBreakerPredicate =
        "http://iec.ch/TC57/1999/rdf-schema-extensions-19990926#KEPCOFeeder.CircuitBreaker";

    for (const auto& edge : graph_.getEdgesByPredicate(feederBreakerPredicate)) {
        if (edge.isLiteral) continue;
        TermId feederUri = edge.source;
        TermId cbUri     = edge.target;

        // Feeder 노드
        EquipmentNode* feederNode = createEquipmentNode(feederUri, EquipmentKind::Feeder);

        // CircuitBreaker 는 Switch 타입 중 하나이므로 Switch 로 간주
        EquipmentNode* cbNode = nullptr;
        auto itNode = uriToEquipNode_.find(cbUri);
        if (itNode != uriToEquipNode_.end()) {
            cbNode = itNode->second;
        } else {
            // 아직 생성되지 않았다면 타입을 보고 결정
            EquipmentKind kind = EquipmentKind::Switch;
            cbNode = createEquipmentNode(cbUri, kind);
        }

        feederNode->neighbors.push_back(cbNode);
        cbNode->neighbors.push_back(feederNode);
    }
}

// ===== 타입 / 속성 조회 =====

const std::vector<TermId>& LineFinderV2::getResourceTypes(TermId uri) const {
    static const std::vector<TermId> empty;
    auto it = resourceTypes_.find(uri);
    if (it == resourceTypes_.end()) {
        return empty;
    }
    return it->second;
}

int LineFinderV2::getSwitchOpenValue(TermId switchUri) const {
    auto it = switchOpenCache_.find(switchUri);
    if (it == switchOpenCache_.end()) {
        return -1;
    }
    return it->second;
}

// ===== DFS 구현 =====

void LineFinderV2::dfsFaultSearch(EquipmentNode* startNode,
                                  std::unordered_set<EquipmentNode*>& faultEquipments) const {
    if (!startNode) return;

    std::unordered_set<EquipmentNode*> visited;
    std::vector<EquipmentNode*> stack;
    stack.push_back(startNode);

    while (!stack.empty()) {
        EquipmentNode* current = stack.back();
        stack.pop_back();

        if (!current) continue;
        if (visited.find(current) != visited.end()) {
            continue;
        }
        visited.insert(current);

        // Feeder 인 경우: 종단으로 보고 고장선로에 포함하지 않고 종료
        if (current->kind == EquipmentKind::Feeder) {
            continue;
        }

        bool shouldReturn = false;

        if (current->kind == EquipmentKind::Switch) {
            // Switch.open 상태에 따라 회귀 여부 결정
            int openValue = getSwitchOpenValue(current->uri);
            if (openValue == 1) {
                // 열려 있는 스위치: 여기서 회귀, 이 스위치는 고장선로에 포함하지 않음
                shouldReturn = true;
            }
            // openValue == 0 또는 -1 인 경우는 계속 탐색
        }

        if (shouldReturn) {
            continue;
        }

        // 고장선로에 포함
        faultEquipments.insert(current);

        // 이웃 장비로 확장
        for (EquipmentNode* next : current->neighbors) {
            if (next && visited.find(next) == visited.end()) {
                stack.push_back(next);
            }
        }
    }
}

// ===== Level1 / Level2 선로 탐색 (v2, 장비 그래프 기반) =====

void LineFinderV2::findLevel1Line(const std::unordered_set<EquipmentNode*>& faultEquipments,
                                  std::unordered_set<EquipmentNode*>& level1Equipments) const {
    // 고장선로 주변 상시연계점(Switch.open == 1) 찾기
    std::unordered_set<EquipmentNode*> tieSwitches;
    
    for (EquipmentNode* eq : faultEquipments) {
        if (!eq) continue;
        for (EquipmentNode* nb : eq->neighbors) {
            if (!nb) continue;
            if (nb->kind != EquipmentKind::Switch) continue;
            int open = getSwitchOpenValue(nb->uri);
            if (open == 1) {
                tieSwitches.insert(nb);
            }
        }
    }
    
    // === 상시연계점 URI 출력 (Level1 탐색) ===
    std::cout << "[v2 Level1] Tie-switch (Switch.open=1) URIs:" << std::endl;
    for (EquipmentNode* sw : tieSwitches) {
        if (!sw) continue;
        std::cout << "  " << graph_.str(sw->uri) << std::endl;
    }

    // 각 tie switch 의 반대측으로 DFS
    for (EquipmentNode* tie : tieSwitches) {
        if (!tie) continue;
        
        std::vector<EquipmentNode*> stack;
        std::unordered_set<EquipmentNode*> visited;
        
        // tie 의 이웃 중 고장선로에 포함되지 않은 쪽에서 시작
        for (EquipmentNode* nb : tie->neighbors) {
            if (!nb) continue;
            if (faultEquipments.find(nb) != faultEquipments.end()) continue;
            if (tieSwitches.find(nb) != tieSwitches.end()) continue;
            stack.push_back(nb);
        }
        
        while (!stack.empty()) {
            EquipmentNode* cur = stack.back();
            stack.pop_back();
            if (!cur) continue;
            if (!visited.insert(cur).second) continue;
            if (faultEquipments.find(cur) != faultEquipments.end()) continue;
            
            bool isFeeder = (cur->kind == EquipmentKind::Feeder);
            bool isSw = (cur->kind == EquipmentKind::Switch);
            if (isFeeder) {
                level1Equipments.insert(cur);
                continue;
            }
            if (isSw) {
                int open = getSwitchOpenValue(cur->uri);
                if (open == 1) {
                    level1Equipments.insert(cur);
                    continue;
                }
            }
            
            level1Equipments.insert(cur);
            
            for (EquipmentNode* nb : cur->neighbors) {
                if (!nb) continue;
                if (visited.find(nb) != visited.end()) continue;
                if (faultEquipments.find(nb) != faultEquipments.end()) continue;
                if (tieSwitches.find(nb) != tieSwitches.end()) continue;
                stack.push_back(nb);
            }
        }
    }
}

void LineFinderV2::findLevel2Line(const std::unordered_set<EquipmentNode*>& faultEquipments,
                                  const std::unordered_set<EquipmentNode*>& level1Equipments,
                                  std::unordered_set<EquipmentNode*>& level2Equipments) const {
    // 고장선로 + Level1 선로는 통과 금지
    std::unordered_set<EquipmentNode*> blocked = faultEquipments;
    blocked.insert(level1Equipments.begin(), level1Equipments.end());

    // "Level1 선로의 상시연계점" = Level1 안에 있는 Switch.open == 1 스위치 장비들
    std::unordered_set<EquipmentNode*> tieSwitches;

    for (EquipmentNode* eq : level1Equipments) {
        if (!eq) continue;
        if (eq->kind != EquipmentKind::Switch) continue;
        int open = getSwitchOpenValue(eq->uri);
        if (open == 1) {
            tieSwitches.insert(eq);
        }
    }

    // 각 tie switch 의 반대측으로 DFS
    for (EquipmentNode* tie : tieSwitches) {
        if (!tie) continue;

        std::vector<EquipmentNode*> stack;
        std::unordered_set<EquipmentNode*> visited;

        // tie 의 이웃 중 blocked 가 아닌 쪽에서 시작
        for (EquipmentNode* nb : tie->neighbors) {
            if (!nb) continue;
            if (blocked.find(nb) != blocked.end()) continue;
            if (tieSwitches.find(nb) != tieSwitches.end()) continue;
            stack.push_back(nb);
        }

        while (!stack.empty()) {
            EquipmentNode* cur = stack.back();
            stack.pop_back();
            if (!cur) continue;
            if (!visited.insert(cur).second) continue;
            if (blocked.find(cur) != blocked.end()) continue;

            bool isFeeder = (cur->kind == EquipmentKind::Feeder);
            bool isSw = (cur->kind == EquipmentKind::Switch);
            if (isFeeder) {
                level2Equipments.insert(cur);
                continue;
            }
            if (isSw) {
                int open = getSwitchOpenValue(cur->uri);
                if (open == 1) {
                    level2Equipments.insert(cur);
                    continue;
                }
            }

            level2Equipments.insert(cur);

            for (EquipmentNode* nb : cur->neighbors) {
                if (!nb) continue;
                if (visited.find(nb) != visited.end()) continue;
                if (blocked.find(nb) != blocked.end()) continue;
                if (tieSwitches.find(nb) != tieSwitches.end()) continue;
                stack.push_back(nb);
            }
        }
    }
}

// ===== RDF/XML 생성 유틸리티 =====

std::string LineFinderV2::getRelativeUri(const std::string& fullUri, const std::string& baseUri) {
    if (fullUri.find(baseUri) == 0) {
        return fullUri.substr(baseUri.length());
    }
    return fullUri;
}

std::string LineFinderV2::getShortPredicate(const std::string& predicate) {
    size_t hashPos = predicate.find('#');
    if (hashPos != std::string::npos) {
        std::string namespaceUri = predicate.substr(0, hashPos + 1);
        std::string localName = predicate.substr(hashPos + 1);

        if (namespaceUri == "http://iec.ch/TC57/1999/rdf-schema-extensions-19990926#") {
            return "cim:" + localName;
        } else if (namespaceUri == "http://iec.ch/TC57/2013/CIM-schema-cim16#") {
            return "cim:" + localName;
        } else if (namespaceUri == "http://www.w3.org/1999/02/22-rdf-syntax-ns#") {
            return "rdf:" + localName;
        } else if (namespaceUri == "http://www.w3.org/2000/01/rdf-schema#") {
            return "rdfs:" + localName;
        }
    }
    return predicate;
}

std::string LineFinderV2::getShortType(const std::string& type) {
    size_t hashPos = type.find('#');
    if (hashPos != std::string::npos) {
        std::string namespaceUri = type.substr(0, hashPos + 1);
        std::string localName = type.substr(hashPos + 1);

        if (namespaceUri == "http://iec.ch/TC57/1999/rdf-schema-extensions-19990926#") {
            return "cim:" + localName;
        } else if (namespaceUri == "http://iec.ch/TC57/2013/CIM-schema-cim16#") {
            return "cim:" + localName;
        }
    }
    return type;
}

std::string LineFinderV2::escapeXml(const std::string& text) {
    std::string result;
    result.reserve(text.length() * 1.2);

    for (char c : text) {
        switch (c) {
            case '&':  result += "&amp;"; break;
            case '<':  result += "&lt;"; break;
            case '>':  result += "&gt;"; break;
            case '"':  result += "&quot;"; break;
            case '\'': result += "&apos;"; break;
            default:   result += c; break;
        }
    }

    return result;
}

void LineFinderV2::exportLineToRDFXML(const std::unordered_set<TermId>& coreUris,
                                      const std::string& outputPath) const {
    if (outputPath.empty() || coreUris.empty()) {
        return;
    }

    std::ostringstream xmlStream;

    xmlStream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xmlStream << "<rdf:RDF xmlns:cim=\"http://iec.ch/TC57/1999/rdf-schema-extensions-19990926#\"\n";
    xmlStream << "         xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\"\n";
    xmlStream << "         xmlns:rdfs=\"http://www.w3.org/2000/01/rdf-schema#\"\n";
    xmlStream << "         xmlns=\"" << escapeXml(baseUri_) << "\"\n";
    xmlStream << "         xmlns:desk=\"" << escapeXml(baseUri_) << "\"\n";
    xmlStream << "         xml:base=\"" << escapeXml(baseUri_) << "\">\n";

    const TermId rdfType = graph_.findTerm("rdf:type");
    std::unordered_set<TermId> collectedUris = coreUris;
    std::unordered_map<TermId, std::vector<TermId>> uriTypes;
    std::unordered_map<TermId, std::vector<RDFEdge>> uriEdges;

    for (TermId uri : coreUris) {
        uriTypes[uri] = getResourceTypes(uri);

        for (const auto& edge : graph_.getOutgoingEdges(uri)) {
            if (edge.predicate != rdfType) {
                uriEdges[uri].push_back(edge);
                if (!edge.isLiteral && collectedUris.find(edge.target) == collectedUris.end()) {
                    collectedUris.insert(edge.target);
                }
            }
        }

        for (const auto& edge : graph_.getIncomingEdges(uri)) {
            if (edge.predicate != rdfType && !edge.isLiteral) {
                if (collectedUris.find(edge.source) == collectedUris.end()) {
                    collectedUris.insert(edge.source);
                }
                uriEdges[edge.source].push_back(edge);
            }
        }
    }

    for (TermId uri : collectedUris) {
        if (coreUris.find(uri) == coreUris.end()) {
            const std::vector<TermId>& types = getResourceTypes(uri);
            if (!types.empty()) {
                uriTypes[uri] = types;
            }

            for (const auto& edge : graph_.getOutgoingEdges(uri)) {
                if (edge.predicate != rdfType) {
                    uriEdges[uri].push_back(edge);
                }
            }
        }
    }

    // unordered_set 순서는 실행마다 바뀌므로 URI 문자열로 정렬해서 쓴다
    std::vector<TermId> orderedUris(collectedUris.begin(), collectedUris.end());
    std::sort(orderedUris.begin(), orderedUris.end(), [this](TermId a, TermId b) {
        return graph_.str(a) < graph_.str(b);
    });

    for (TermId uri : orderedUris) {
        if (uriTypes.find(uri) == uriTypes.end() || uriTypes[uri].empty()) {
            continue;
        }

        std::string shortType = getShortType(graph_.str(uriTypes[uri][0]));
        std::string relativeUri = getRelativeUri(graph_.str(uri), baseUri_);
        
        // Switch 타입인지 확인 (모든 타입 확인)
        bool isSwitch = false;
        for (TermId type : uriTypes[uri]) {
            if (isSwitchType(graph_.str(type))) {
                isSwitch = true;
                break;
            }
        }

        xmlStream << "        <" << escapeXml(shortType)
                  << " rdf:ID=\"" << escapeXml(relativeUri) << "\">\n";

        if (uriEdges.find(uri) != uriEdges.end()) {
            // 중복 제거를 위한 집합: (predicate, target) 조합
            std::unordered_set<std::string> seenEdges;
            
            // IdentifiedObject.name과 Switch.open을 저장할 변수
            std::string identifiedObjectName;
            std::string switchOpen;
            
            for (const auto& edge : uriEdges[uri]) {
                std::string shortPredicate = getShortPredicate(graph_.str(edge.predicate));
                const std::string& target = graph_.str(edge.target);
                
                // IdentifiedObject.name 저장
                if (shortPredicate == "cim:IdentifiedObject.name" && edge.isLiteral) {
                    identifiedObjectName = target;
                    continue; // 나중에 출력
                }
                
                // Switch.open 저장 (Switch인 경우)
                if (isSwitch && shortPredicate == "cim:Switch.open" && edge.isLiteral) {
                    switchOpen = target;
                    continue; // 나중에 출력
                }
                
                // rdf:resource 속성만 출력 (리터럴이 아닌 속성)
                if (!edge.isLiteral) {
                    std::string relativeTarget = getRelativeUri(target, baseUri_);
                    
                    // 중복 체크를 위한 키 생성
                    std::string edgeKey = shortPredicate + "|" + relativeTarget;
                    if (seenEdges.find(edgeKey) != seenEdges.end()) {
                        continue;
                    }
                    seenEdges.insert(edgeKey);
                    
                    if (relativeTarget == target) {
                        xmlStream << "                <" << escapeXml(shortPredicate)
                                  << " rdf:resource=\"" << escapeXml(target) << "\"/>\n";
                    } else {
                        xmlStream << "                <" << escapeXml(shortPredicate)
                                  << " rdf:resource=\"#" << escapeXml(relativeTarget) << "\"/>\n";
                    }
                }
                // 리터럴 속성은 출력하지 않음 (IdentifiedObject.name, Switch.open 제외)
            }
            
            // IdentifiedObject.name 출력 (있는 경우)
            if (!identifiedObjectName.empty()) {
                xmlStream << "                <cim:IdentifiedObject.name>" << escapeXml(identifiedObjectName)
                          << "</cim:IdentifiedObject.name>\n";
            }
            
            // Switch.open 출력 (Switch인 경우, 있는 경우)
            if (isSwitch && !switchOpen.empty()) {
                xmlStream << "                <cim:Switch.open>" << escapeXml(switchOpen)
                          << "</cim:Switch.open>\n";
            }
        }

        xmlStream << "        </" << escapeXml(shortType) << ">\n";
    }

    xmlStream << "</rdf:RDF>\n";

    std::ofstream outFile(outputPath);
    if (!outFile.is_open()) {
        std::cerr << "파일 열기 실패: " << outputPath << std::endl;
        return;
    }
    outFile << xmlStream.str();
    outFile.close();

    std::cout << "V2 서브그래프 RDF/XML 생성 완료: " << outputPath
              << " (노드 수: " << coreUris.size() << ")" << std::endl;
}

// ===== 메인 API =====

bool LineFinderV2::findFaultLineAndGenerateRDFXML(const std::string& switchUri,
                                                  const std::string& outputPath) {
    using clk = std::chrono::steady_clock;
    auto allStart = clk::now();

    // 시작 스위치 장비 노드 찾기
    auto itStart = uriToEquipNode_.find(graph_.findTerm(switchUri));
    if (itStart == uriToEquipNode_.end()) {
        std::cerr << "LineFinderV2: 시작 스위치 URI에 해당하는 장비 노드를 찾을 수 없습니다: "
                  << switchUri << std::endl;
        return false;
    }
    EquipmentNode* startNode = itStart->second;

    auto searchStart = clk::now();
    std::unordered_set<EquipmentNode*> faultEquipments;
    dfsFaultSearch(startNode, faultEquipments);
    std::unordered_set<EquipmentNode*> level1Equipments;
    std::unordered_set<EquipmentNode*> level2Equipments;
    findLevel1Line(faultEquipments, level1Equipments);
    findLevel2Line(faultEquipments, level1Equipments, level2Equipments);
    auto searchEnd = clk::now();
    double searchMs = elapsedMs(searchStart, searchEnd);

    // URI 집합 변환
    std::unordered_set<TermId> faultLineUris, level1Uris, level2Uris;
    for (auto* n : faultEquipments) if (n) faultLineUris.insert(n->uri);
    for (auto* n : level1Equipments) if (n) level1Uris.insert(n->uri);
    for (auto* n : level2Equipments) if (n) level2Uris.insert(n->uri);

    auto faultXmlStart = clk::now();
    // fault_line RDF/XML 생성
    if (!outputPath.empty()) {
        exportLineToRDFXML(faultLineUris, outputPath);
    } else {
        // 출력 경로가 비어있으면 stdout 으로 출력
        // (exportLineToRDFXML 은 파일만 출력하므로 이 경우는 생략)
    }
    auto faultXmlEnd = clk::now();
    double faultXmlMs = elapsedMs(faultXmlStart, faultXmlEnd);

    double level1XmlMs = 0, level2XmlMs = 0;
    if (!outputPath.empty()) {
        std::string base = outputPath;
        std::string ext  = "";
        auto dotPos = outputPath.find_last_of('.');
        if (dotPos != std::string::npos) {
            base = outputPath.substr(0, dotPos);
            ext  = outputPath.substr(dotPos);
        }
        if (!level1Uris.empty()) {
            auto t1 = clk::now();
            exportLineToRDFXML(level1Uris, base + "_level1" + ext);
            auto t2 = clk::now();
            level1XmlMs = elapsedMs(t1, t2);
        }
        if (!level2Uris.empty()) {
            auto t1 = clk::now();
            exportLineToRDFXML(level2Uris, base + "_level2" + ext);
            auto t2 = clk::now();
            level2XmlMs = elapsedMs(t1, t2);
        }
    }

    auto allEnd = clk::now();
    double allMs = elapsedMs(allStart, allEnd);

    // 선로별 장치 수 / 노드 수
    auto faultDevices = faultEquipments.size();
    auto level1Devices = level1Equipments.size();
    auto level2Devices = level2Equipments.size();
    auto faultNodes   = faultLineUris.size();
    auto level1Nodes  = level1Uris.size();
    auto level2Nodes  = level2Uris.size();

    appendLog(
        "[v2] fault_devices=" + std::to_string(faultDevices) +
        " level1_devices=" + std::to_string(level1Devices) +
        " level2_devices=" + std::to_string(level2Devices) +
        " fault_nodes=" + std::to_string(faultNodes) +
        " level1_nodes=" + std::to_string(level1Nodes) +
        " level2_nodes=" + std::to_string(level2Nodes) +
        " search_ms=" + formatMs(searchMs) +
        " fault_xml_ms=" + formatMs(faultXmlMs) +
        " level1_xml_ms=" + formatMs(level1XmlMs) +
        " level2_xml_ms=" + formatMs(level2XmlMs) +
        " total_ms=" + formatMs(allMs)
    );

    // 요약 출력
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "\n========================================" << std::endl;
    std::cout << "LineFinderV2 고장선로 및 Level1/Level2 탐색 완료" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "시작 스위치 URI: " << switchUri << std::endl;
    std::cout << "고장선로 장비 수: " << faultDevices << std::endl;
    std::cout << "Level1 장비 수: " << level1Devices << std::endl;
    std::cout << "Level2 장비 수: " << level2Devices << std::endl;
    std::cout << "탐색 시간: " << searchMs << " ms ("
              << (searchMs / 1000.0) << " 초)" << std::endl;
    std::cout << "RDF/XML 생성 시간(고장선로 + Level1/2): "
              << (faultXmlMs + level1XmlMs + level2XmlMs) << " ms" << std::endl;
    std::cout << "총 소요 시간: " << allMs << " ms ("
              << (allMs / 1000.0) << " 초)" << std::endl;
    std::cout << "========================================" << std::endl;

    return true;
}

