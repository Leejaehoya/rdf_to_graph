#ifndef LINE_FINDER_V2_HPP
#define LINE_FINDER_V2_HPP

#include "rdf_graph_generator.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

/**
 * CIM 토폴로지 전용 장비 그래프를 이용한 고장선로 탐색 V2
 *
 * - RDFGraph 에서 한 번만 CIM 장비 그래프를 구성
 * - 탐색은 Switch / ACLine / Feeder 장비 객체 단위로 진행
 * - Switch.open / Feeder 여부는 캐시된 값으로 사용
 */
class LineFinderV2 {
public:
    /**
     * 생성자
     * @param graph RDF 그래프 객체 (참조로 전달, 수명은 LineFinderV2 보다 길어야 함)
     * @param baseUri RDF/XML xmlns, xml:base 에 넣을 base URI
     */
    explicit LineFinderV2(const RDFGraph& graph, std::string baseUri);
    ~LineFinderV2();

    // raw pointer 를 들고 있어서 복사되면 double delete 남
    LineFinderV2(const LineFinderV2&) = delete;
    LineFinderV2& operator=(const LineFinderV2&) = delete;

    /**
     * 고장선로 탐색 및 RDF/XML 생성 (장비 그래프 기반)
     * @param switchUri 시작 스위치 URI
     * @param outputPath 출력 파일 경로 (선택사항, 비어있으면 stdout 출력)
     * @return 성공 여부
     */
    bool findFaultLineAndGenerateRDFXML(const std::string& switchUri,
                                        const std::string& outputPath = "");

private:
    /** 장비 종류 */
    enum class EquipmentKind : uint8_t {
        Switch,
        ACLine,
        Feeder
    };

    /** Switch 전용 데이터 */
    struct SwitchData {
        int  openValue;     // -1 (없음), 0, 1
        bool isFeederStart; // Feeder 기점 스위치 여부 (필요 시 확장)

        SwitchData() : openValue(-1), isFeederStart(false) {}
    };

    /** ACLine 전용 데이터 (현재는 추가 정보 없음, 확장용) */
    struct ACLineData {
    };

    /** Feeder 전용 데이터 (확장용) */
    struct FeederData {
    };

    /**
     * 장비 노드 (Switch / ACLine / Feeder 를 하나로 래핑)
     * - kind 로 실제 타입 구분
     * - data 는 실제 타입 포인터를 void* 로 저장
     * - neighbors 는 장비 간 인접 장비 목록
     */
    struct EquipmentNode {
        EquipmentKind kind;
        TermId        uri;
        void*         data;              // SwitchData* / ACLineData* / FeederData*
        std::vector<EquipmentNode*> neighbors;

        EquipmentNode(EquipmentKind k, TermId u)
            : kind(k), uri(u), data(nullptr) {}
    };

    /** ConnectivityNode ID 타입 */
    using CNId = uint32_t;

    const RDFGraph& graph_;
    std::string baseUri_;

    // 타입 / 속성 캐시
    std::unordered_map<TermId, std::vector<TermId>> resourceTypes_;   // uri -> type 목록
    std::unordered_map<TermId, int> switchOpenCache_;                 // switch -> open (0/1)
    std::unordered_set<TermId> feederUris_;                           // KEPCOFeeder 집합

    // CN 매핑
    std::unordered_map<TermId, CNId> cnUriToId_;                                 // CN uri -> CNId
    std::vector<TermId>              cnIdToUri_;                                 // CNId -> CN uri
    std::unordered_map<CNId, std::vector<EquipmentNode*>> cnToEquipments_;      // CNId -> 장비 노드들

    // 장비 노드 및 실제 데이터 보관 (수명 관리)
    std::unordered_map<TermId, EquipmentNode*> uriToEquipNode_;            // uri -> 장비 노드
    std::vector<EquipmentNode*> allEquipNodes_;                                  // 소멸자에서 delete
    std::vector<SwitchData*>    allSwitchData_;
    std::vector<ACLineData*>    allACLineData_;
    std::vector<FeederData*>    allFeederData_;

    // ===== 그래프 빌드 관련 =====

    /** rdf:type / Switch.open / KEPCOFeeder 타입 캐시 */
    void buildTypeAndPropertyCaches();

    /** Terminal / CN / CE 관계를 이용해 CIM 장비 그래프 구성 */
    void buildCimTopology();

    /** 타입이 Switch 종류인지 확인 (기존 LineFinder 와 동일한 로직) */
    bool isSwitchType(const std::string& type) const;

    /** 타입이 KEPCOFeeder 인지 확인 */
    bool isKepcoFeederType(const std::string& type) const;

    /** 타입이 ACLineSegment 계열인지 확인 */
    bool isACLineType(const std::string& type) const;

    /** CN URI 를 CNId 로 매핑 (없으면 생성) */
    CNId getOrCreateCNId(TermId cnUri);

    /** 장비 노드 생성 (이미 있으면 그대로 반환) */
    EquipmentNode* createEquipmentNode(TermId uri, EquipmentKind kind);

    /** CN 별로 연결된 장비 노드들을 인접 리스트로 연결 */
    void linkEquipmentsViaConnectivityNodes();

    // ===== DFS / 결과 생성 관련 =====

    /** 리소스 타입 조회 (캐시 기반) */
    const std::vector<TermId>& getResourceTypes(TermId uri) const;

    /** Switch.open 값 조회 (캐시 기반) */
    int getSwitchOpenValue(TermId switchUri) const;

    /** DFS 를 이용해 고장선로에 포함되는 장비 노드 집합을 계산 */
    void dfsFaultSearch(EquipmentNode* startNode,
                        std::unordered_set<EquipmentNode*>& faultEquipments) const;

    /**
     * Level1 선로 탐색 (v2, 장비 그래프 기반)
     * @param faultEquipments 고장선로에 포함된 장비 노드 집합
     * @param level1Equipments Level1 선로에 포함된 장비 노드 집합 (출력)
     */
    void findLevel1Line(const std::unordered_set<EquipmentNode*>& faultEquipments,
                        std::unordered_set<EquipmentNode*>& level1Equipments) const;

    /**
     * Level2 선로 탐색 (v2, 장비 그래프 기반)
     * @param faultEquipments 고장선로 장비 노드 집합
     * @param level1Equipments Level1 선로 장비 노드 집합
     * @param level2Equipments Level2 선로 장비 노드 집합 (출력)
     */
    void findLevel2Line(const std::unordered_set<EquipmentNode*>& faultEquipments,
                        const std::unordered_set<EquipmentNode*>& level1Equipments,
                        std::unordered_set<EquipmentNode*>& level2Equipments) const;

    /**
     * 주어진 URI 집합을 기반으로 RDF/XML 서브그래프 파일 생성 (v2용)
     */
    void exportLineToRDFXML(const std::unordered_set<TermId>& coreUris,
                            const std::string& outputPath) const;

    // ===== RDF/XML 생성에 필요한 유틸리티 (LineFinder 와 동일한 포맷) =====

    static std::string getRelativeUri(const std::string& fullUri, const std::string& baseUri);
    static std::string getShortPredicate(const std::string& predicate);
    static std::string getShortType(const std::string& type);
    static std::string escapeXml(const std::string& text);
};

#endif // LINE_FINDER_V2_HPP


