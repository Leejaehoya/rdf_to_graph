#include "module/rdf_graph_generator.hpp"
#include "module/line_finder_v2.hpp"
#include <iostream>
#include <string>

void performSearch(RDFGraph& graph, const std::string& switchid) {
    std::string switchBaseUri = "http://www.desktop-eqkvgq5.net/2019/cim#";
    std::string processedSwitchid = switchid;
    
    // switchid가 언더스코어로 시작하지 않으면 추가
    if (processedSwitchid.empty()) {
        std::cerr << "오류: switch_id가 비어있습니다." << std::endl;
        return;
    }
    if (processedSwitchid[0] != '_') {
        processedSwitchid = "_" + processedSwitchid;
    }
    
    // 전체 URI 생성
    std::string switchUri = switchBaseUri + processedSwitchid;
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "고장선로 탐색 시작..." << std::endl;
    std::cout << "스위치 URI: " << switchUri << std::endl;
    
    // 디버깅: 스위치 URI가 그래프에 존재하는지 확인
    const auto& allNodes = graph.getAllNodes();
    if (allNodes.find(switchUri) == allNodes.end()) {
        std::cout << "경고: 스위치 URI가 그래프에 존재하지 않습니다." << std::endl;
        std::cout << "비슷한 URI를 찾는 중..." << std::endl;
        
        // 비슷한 URI 찾기
        int count = 0;
        for (const auto& node : allNodes) {
            if (node.find(switchUri) != std::string::npos || switchUri.find(node) != std::string::npos) {
                std::cout << "  발견: " << node << std::endl;
                count++;
                if (count >= 10) break;
            }
        }
        if (count == 0) {
            std::cout << "  비슷한 URI를 찾을 수 없습니다." << std::endl;
        }
        std::cout << "========================================\n" << std::endl;
        return;
    }
    
    std::cout << "스위치 URI가 그래프에 존재합니다." << std::endl;
    
    // 타입 확인
    std::vector<RDFEdge> typeEdges = graph.getOutgoingEdges(switchUri);
    std::cout << "타입 정보:" << std::endl;
    for (const auto& edge : typeEdges) {
        if (edge.predicate == "rdf:type" && !edge.isLiteral) {
            std::cout << "  - " << edge.target << std::endl;
        }
    }
    
    // Switch.open 값 확인
    std::string switchOpenPredicate = "http://iec.ch/TC57/1999/rdf-schema-extensions-19990926#Switch.open";
    auto openValues = graph.getLiteralPropertyValues(switchUri, switchOpenPredicate);
    if (!openValues.empty()) {
        std::cout << "Switch.open 값: " << std::get<0>(openValues[0]) << std::endl;
    } else {
        std::cout << "Switch.open 값이 없습니다." << std::endl;
    }
    
    // LineFinderV2 생성 (매번 새로 생성하여 이전 탐색 정보 지움)
    LineFinderV2 finder_v2(graph);
    
    // 출력 파일 경로
    std::string outputPathV2 = "fault_line_v2" + processedSwitchid + ".xml";
    
    std::cout << "\n[LineFinder v2] 고장선로 탐색 시작..." << std::endl;
    if (!finder_v2.findFaultLineAndGenerateRDFXML(switchUri, outputPathV2)) {
        std::cerr << "LineFinder v2 고장선로 탐색 실패" << std::endl;
    } else {
        std::cout << "LineFinder v2 결과가 " << outputPathV2 << " 파일에 저장되었습니다." << std::endl;
    }
    std::cout << "========================================\n" << std::endl;
}

int main(int argc, char* argv[]) {
    // 데이터베이스 경로
    std::string dbPath = "data/rdfstore.db";
    
    std::cout << "RDF 그래프 로드 중..." << std::endl;
    
    // RDF 그래프 로드 (한 번만)
    RDFGraph graph;
    if (!graph.loadFromDatabase(dbPath)) {
        std::cerr << "데이터베이스 로드 실패" << std::endl;
        return 1;
    }
    
    std::cout << "\nRDF 그래프 로드 완료. switch_id를 입력하세요." << std::endl;
    std::cout << "종료하려면 'quit' 또는 'exit'를 입력하세요." << std::endl;
    std::cout << "예시: 11921400000062 또는 _11921400000062\n" << std::endl;
    
    // 반복적으로 switchid 입력받기
    std::string input;
    while (true) {
        std::cout << "switch_id 입력: ";
        std::getline(std::cin, input);
        
        // 입력이 비어있으면 다시 입력받기
        if (input.empty()) {
            continue;
        }
        
        // 종료 명령 확인
        if (input == "quit" || input == "exit" || input == "q") {
            std::cout << "프로그램을 종료합니다." << std::endl;
            break;
        }
        
        // 탐색 실행
        performSearch(graph, input);
    }
    
    return 0;
}

