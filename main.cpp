#include "module/rdf_graph_generator.hpp"
#include "module/line_finder_v2.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <iostream>
#include <string>
#include <vector>
#include <sys/resource.h>
#ifdef __APPLE__
#include <mach/mach.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

// 로드와 장비 그래프 구성이 끝난 직후의 현재 RSS (MB).
static double currentRssMB() {
#ifdef __linux__
    long pages = 0, resident = 0;
    FILE* f = std::fopen("/proc/self/statm", "r");
    if (!f) return 0;
    if (std::fscanf(f, "%ld %ld", &pages, &resident) != 2) resident = 0;
    std::fclose(f);
    return resident * (sysconf(_SC_PAGESIZE) / 1024.0 / 1024.0);
#elif defined(__APPLE__)
    struct task_basic_info info;
    mach_msg_type_number_t size = sizeof(info);
    kern_return_t kerr = task_info(mach_task_self(), TASK_BASIC_INFO,
                                   reinterpret_cast<task_info_t>(&info), &size);
    if (kerr != KERN_SUCCESS) return 0;
    return info.resident_size / 1024.0 / 1024.0;
#else
    return 0;
#endif
}

// 프로세스 peak RSS (MB). Linux ru_maxrss 는 KB, macOS 는 bytes.
static double peakRssMB() {
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) return 0;
#ifdef __APPLE__
    return ru.ru_maxrss / 1024.0 / 1024.0;
#else
    return ru.ru_maxrss / 1024.0;
#endif
}

static std::string baseUriOf(const std::string& switchUri) {
    auto pos = switchUri.rfind('#');
    if (pos == std::string::npos) {
        return "http://www.desktop-eqkvgq5.net/2019/cim#";
    }
    return switchUri.substr(0, pos + 1);
}

// graph_viewer --bench <db> <switch_uri> [N]
// switch_uri 는 전체 URI. 예: http://www.desktop-eqkvgq5.net/2019/cim#_11921400000062
static int runBench(RDFGraph& graph, const std::string& dbPath,
                    const std::string& switchUri, int n) {
    using clk = std::chrono::steady_clock;
    auto t0 = clk::now();
    if (!graph.loadFromDatabase(dbPath)) return 1;
    auto t1 = clk::now();
    LineFinderV2 finder(graph, baseUriOf(switchUri));
    auto t2 = clk::now();

    std::vector<double> us;
    us.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        auto a = clk::now();
        finder.findFaultLineAndGenerateRDFXML(switchUri, "bench_out.xml");
        us.push_back(std::chrono::duration<double, std::micro>(clk::now() - a).count());
    }
    std::sort(us.begin(), us.end());
    double sum = 0;
    for (double v : us) sum += v;
    std::fprintf(stderr,
        "BENCH load_ms=%.1f topo_ms=%.1f rss_mb=%.1f peak_mb=%.1f "
        "query_avg_ms=%.3f query_p95_ms=%.3f n=%d\n",
        std::chrono::duration<double, std::milli>(t1 - t0).count(),
        std::chrono::duration<double, std::milli>(t2 - t1).count(),
        currentRssMB(), peakRssMB(),
        sum / n / 1000.0,
        us[std::min(us.size() - 1, us.size() * 95 / 100)] / 1000.0,
        n);
    return 0;
}

void performSearch(const RDFGraph& graph, LineFinderV2& finder_v2,
                   const std::string& switchBaseUri, const std::string& switchid) {
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
    if (!graph.hasNode(switchUri)) {
        std::cout << "경고: 스위치 URI가 그래프에 존재하지 않습니다." << std::endl;
        std::cout << "비슷한 URI를 찾는 중..." << std::endl;
        
        // 비슷한 URI 찾기
        int count = 0;
        for (TermId id : graph.getAllNodes()) {
            const std::string& node = graph.str(id);
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
    std::cout << "타입 정보:" << std::endl;
    for (const auto& edge : graph.getOutgoingEdges(switchUri)) {
        if (graph.str(edge.predicate) == "rdf:type" && !edge.isLiteral) {
            std::cout << "  - " << graph.str(edge.target) << std::endl;
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
    // graph_viewer --bench <db> <switch_uri> [N]
    if (argc >= 2 && std::string(argv[1]) == "--bench") {
        if (argc < 4) {
            std::cerr << "usage: graph_viewer --bench <db> <switch_uri> [N]\n";
            return 1;
        }
        int n = 50;
        if (argc > 4) {
            try {
                n = std::stoi(argv[4]);
            } catch (const std::exception&) {
                std::cerr << "N은 정수여야 합니다.\n";
                return 1;
            }
        }
        if (n < 1) {
            std::cerr << "N은 1 이상이어야 합니다.\n";
            return 1;
        }
        RDFGraph g;
        return runBench(g, argv[2], argv[3], n);
    }

    // usage: graph_viewer [db_path] [base_uri]
    std::string dbPath = argc > 1 ? argv[1] : "data/rdfstore.db";
    std::string baseUri = argc > 2 ? argv[2] : "http://www.desktop-eqkvgq5.net/2019/cim#";
    
    std::cout << "RDF 그래프 로드 중..." << std::endl;
    
    // RDF 그래프 로드 (한 번만)
    RDFGraph graph;
    if (!graph.loadFromDatabase(dbPath)) {
        std::cerr << "데이터베이스 로드 실패" << std::endl;
        return 1;
    }
    
    // 장비 그래프는 한 번만 만든다 (탐색 상태는 멤버에 안 남음)
    LineFinderV2 finder_v2(graph, baseUri);

    std::cout << "\nRDF 그래프 로드 완료. switch_id를 입력하세요." << std::endl;
    std::cout << "종료하려면 'quit' 또는 'exit'를 입력하세요." << std::endl;
    std::cout << "예시: 11921400000062 또는 _11921400000062\n" << std::endl;
    
    // 반복적으로 switchid 입력받기
    std::string input;
    while (true) {
        std::cout << "switch_id 입력: ";
        if (!std::getline(std::cin, input)) {
            break;  // EOF (파이프 입력 끝 / Ctrl+D)
        }
        
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
        performSearch(graph, finder_v2, baseUri, input);
    }
    
    return 0;
}

