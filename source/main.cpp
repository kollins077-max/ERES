#include "commonfunctions.h"
#include "temporal_graph.h"
#include "online_search.h"
#include "baseline.h"
#include "optimized.h"
#include <cstdlib>
#include <limits>
#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

bool debug = false;

TemporalGraph * build(char * argv[], double subgraph_fraction) {

    std::cout << "Building graph..." << std::endl;
    unsigned long long build_graph_start_time = currentTime();
    TemporalGraph * Graph = new TemporalGraph(argv[1], (char *)"Directed", subgraph_fraction);
    unsigned long long build_graph_end_time = currentTime();
    std::cout << "Build graph success in " << timeFormatting(build_graph_end_time - build_graph_start_time).str() << std::endl;
    std::cout << "n = " << Graph->numOfVertices() << ", m = " << Graph->numOfEdges() << ", tmax = " << Graph->tmax << ", size = " << Graph->size() << " bytes" << std::endl;
    return Graph;
    
}

struct ExperimentGraphMetadata {
    long long totalEdges = 0;
    int fullMaxVertex = -1;
    int fullTmax = 0;
    std::vector<long long> edgesPerTime;
};

bool parseTemporalTripleLine(const std::string& line, int& u, int& v, int& t) {
    if (line.empty() || line[0] == '%') {
        return false;
    }
    std::istringstream input(line);
    return bool(input >> u >> v >> t);
}

ExperimentGraphMetadata scanExperimentGraphMetadata(const char *graphFile) {
    ExperimentGraphMetadata meta;
    std::ifstream fin(graphFile);
    if (!fin) {
        throw std::runtime_error(std::string("Cannot open graph file: ") + graphFile);
    }

    std::string line;
    int u = 0;
    int v = 0;
    int t = 0;
    while (std::getline(fin, line)) {
        if (!parseTemporalTripleLine(line, u, v, t)) {
            continue;
        }
        if (t < 0) {
            continue;
        }
        meta.fullMaxVertex = std::max(meta.fullMaxVertex, std::max(u, v));
        meta.fullTmax = std::max(meta.fullTmax, t);
        if (t >= int(meta.edgesPerTime.size())) {
            meta.edgesPerTime.resize(std::size_t(t) + 1, 0);
        }
        ++meta.edgesPerTime[t];
        ++meta.totalEdges;
    }
    return meta;
}

TemporalGraph * buildExperimentGraph(char * argv[], double a, long requested) {
    std::cout << "Building graph for prefix/update experiment..." << std::endl;
    unsigned long long buildStart = currentTime();

    ExperimentGraphMetadata meta = scanExperimentGraphMetadata(argv[1]);
    if (meta.totalEdges <= 0) {
        throw std::runtime_error("No valid temporal edges were read from the graph file.");
    }

    double ratio = std::max(0.0, std::min(1.0, a));
    long long prefixCount = static_cast<long long>(
        std::floor(ratio * double(meta.totalEdges) + 1e-12));
    if (prefixCount < 0) {
        prefixCount = 0;
    }
    if (prefixCount > meta.totalEdges) {
        prefixCount = meta.totalEdges;
    }
    long long updateCount = std::min<long long>(
        std::max<long long>(0, requested), meta.totalEdges - prefixCount);
    long long materializedTarget = prefixCount + updateCount;

    std::vector<long long> needPerTime(meta.edgesPerTime.size(), 0);
    long long remaining = materializedTarget;
    for (int time = 0; time < int(meta.edgesPerTime.size()) && remaining > 0; ++time) {
        long long take = std::min(remaining, meta.edgesPerTime[time]);
        needPerTime[time] = take;
        remaining -= take;
    }

    TemporalGraph * Graph = new TemporalGraph();
    Graph->is_directed = true;
    Graph->is_general = true;
    Graph->tmax = meta.fullTmax;
    Graph->total_m = meta.totalEdges;
    Graph->temporal_edge.resize(std::size_t(meta.fullTmax) + 1);

    int fullVertexCount = meta.fullMaxVertex + 1;
    const int largeVertexUniverseThreshold = 10000000;
    bool compressVertexIds = fullVertexCount >= largeVertexUniverseThreshold &&
        std::getenv("FINDSCC_DISABLE_VERTEX_COMPRESSION") == nullptr;

    std::vector<long long> loadedPerTime(needPerTime.size(), 0);
    std::unordered_map<int,int> denseId;
    if (compressVertexIds) {
        long long estimatedVertexCapacity = std::min<long long>(
            std::max<long long>(materializedTarget * 2, 1),
            std::max<long long>(fullVertexCount, 1));
        std::size_t expectedVertices = std::size_t(std::min<long long>(
            estimatedVertexCapacity, 50000000LL));
        denseId.reserve(expectedVertices);
        Graph->original_vertex_ids.reserve(expectedVertices);
        std::cout << "[Experiment graph loader] Large vertex id universe detected: full n = "
                  << fullVertexCount
                  << ". Compressing materialized vertex ids for this a,b experiment."
                  << std::endl;
    }

    std::ifstream fin(argv[1]);
    if (!fin) {
        delete Graph;
        throw std::runtime_error(std::string("Cannot reopen graph file: ") + argv[1]);
    }

    auto getDenseId = [&](int original) -> int {
        std::unordered_map<int,int>::iterator found = denseId.find(original);
        if (found != denseId.end()) {
            return found->second;
        }
        int id = int(Graph->original_vertex_ids.size());
        denseId[original] = id;
        Graph->original_vertex_ids.push_back(original);
        return id;
    };

    std::string line;
    int u = 0;
    int v = 0;
    int t = 0;
    int loadedMaxVertex = -1;
    long long loadedEdges = 0;
    while (loadedEdges < materializedTarget && std::getline(fin, line)) {
        if (!parseTemporalTripleLine(line, u, v, t)) {
            continue;
        }
        if (t < 0 || t >= int(needPerTime.size())) {
            continue;
        }
        if (loadedPerTime[t] >= needPerTime[t]) {
            continue;
        }
        int storedU = u;
        int storedV = v;
        if (compressVertexIds) {
            storedU = getDenseId(u);
            storedV = getDenseId(v);
        }
        Graph->temporal_edge[t].push_back(std::make_pair(storedU, storedV));
        ++loadedPerTime[t];
        ++loadedEdges;
        loadedMaxVertex = std::max(loadedMaxVertex, std::max(storedU, storedV));
    }

    if (loadedEdges != materializedTarget) {
        std::cout << "[Experiment graph loader] Warning: requested "
                  << materializedTarget << " materialized edges, but loaded "
                  << loadedEdges << "." << std::endl;
    }

    Graph->m = loadedEdges > std::numeric_limits<int>::max()
        ? std::numeric_limits<int>::max()
        : int(loadedEdges);

    int materializedVertexCount = loadedMaxVertex + 1;
    if (materializedVertexCount <= 0) {
        materializedVertexCount = compressVertexIds ? 0 : fullVertexCount;
    }
    Graph->n = compressVertexIds ? materializedVertexCount : fullVertexCount;
    if (compressVertexIds) {
        std::cout << "[Experiment graph loader] Vertex compression finished: materialized n = "
                  << Graph->n << ", full max-id n = " << fullVertexCount
                  << ". Query output will be mapped back to original vertex ids."
                  << std::endl;
    }

    unsigned long long buildEnd = currentTime();
    std::cout << "Build graph success in "
              << timeFormatting(buildEnd - buildStart).str() << std::endl;
    std::cout << "full_m = " << meta.totalEdges
              << ", materialized_m = " << loadedEdges
              << ", prefix_edges = " << prefixCount
              << ", update_edges = " << updateCount
              << ", n = " << Graph->numOfVertices()
              << ", full_n = " << fullVertexCount
              << ", tmax = " << Graph->tmax
              << ", size = " << Graph->size() << " bytes"
              << std::endl;
    return Graph;
}
bool isSingleEdgeMode(const char * mode) {
    return std::strcmp(mode, "RES-Single") == 0 ||
           std::strcmp(mode, "RES-ET") == 0 ||
           std::strcmp(mode, "RES-Batch") == 0 ||
           std::strcmp(mode, "ERES-ET") == 0 ||
           std::strcmp(mode, "ERES-ET-NoPrune") == 0 ||
           std::strcmp(mode, "ERES") == 0 ||
           std::strcmp(mode, "ERES-Batch") == 0 ||
           std::strcmp(mode, "DRES3") == 0;
}

int runSingleEdgeMode(int argc, char * argv[]) {
    if (argc != 7 || !isSingleEdgeMode(argv[4])) {
        std::cout
            << "Usage: main.exe graph.txt query.txt output.txt "
            << "<RES-Single|RES-ET|RES-Batch|ERES|ERES-ET|ERES-ET-NoPrune|ERES-Batch> <a> <b>"
            << std::endl;
        return 1;
    }

    char * ratioEnd = nullptr;
    char * countEnd = nullptr;
    double a = std::strtod(argv[5], &ratioEnd);
    long requested = std::strtol(argv[6], &countEnd, 10);
    if (ratioEnd == argv[5] || *ratioEnd != '\0' ||
        countEnd == argv[6] || *countEnd != '\0' ||
        requested < 0 || requested > std::numeric_limits<int>::max()) {
        std::cout << "Invalid a or b. Require 0<=a<=1 and b>=0."
                  << std::endl;
        return 1;
    }
    if (a < 0.0 || a > 1.0) {
        std::cout << "Invalid a. Require 0<=a<=1." << std::endl;
        return 1;
    }

    TemporalGraph * Graph = nullptr;
    try {
        Graph = buildExperimentGraph(argv, a, requested);
    }
    catch (const std::bad_alloc& e) {
        std::cout << "Memory allocation failed while preparing the prefix/update graph: " << e.what() << std::endl;
        return 2;
    }
    catch (const std::exception& e) {
        std::cout << "Failed to prepare the prefix/update graph: " << e.what() << std::endl;
        return 2;
    }
    int vertex_num = Graph->numOfVertices();

    std::cout << "Initial edge fraction a = " << a
              << ", requested update edges b = " << requested
              << std::endl;
    unsigned long long totalStart = currentTime();
    OptimizedIndex * Index = nullptr;
    try {
        if (std::strcmp(argv[4], "RES-Single") == 0) {
        std::cout << "Running forward original-paper single-edge maintenance..."
                  << std::endl;
        Index = OptimizedIndex::buildOriginalSingleEdge(
            Graph, a, int(requested));
    }
    else if (std::strcmp(argv[4], "RES-ET") == 0) {
        std::cout
            << "Running forward original-paper single-edge maintenance with SCC-formation influence-interval pruning..."
            << std::endl;
        Index = OptimizedIndex::buildRESWithETSingleEdge(
            Graph, a, int(requested));
    }
    else if (std::strcmp(argv[4], "RES-Batch") == 0) {
        std::cout << "Running forward original-paper timestamp-batch maintenance..."
                  << std::endl;
        Index = OptimizedIndex::buildOriginalBatch(
            Graph, a, int(requested));
    }
    else if (std::strcmp(argv[4], "ERES-ET-NoPrune") == 0) {
        std::cout
            << "Running ERES-ET without intra-SCC non-RES edge pruning..."
            << std::endl;
        Index = OptimizedIndex::buildERESWithETNoPruneSingleEdge(
            Graph, a, int(requested));
    }
    else if (std::strcmp(argv[4], "ERES") == 0) {
        std::cout
            << "Running ERES single-edge maintenance..."
            << std::endl;
        Index = OptimizedIndex::buildERESSingleEdge(
            Graph, a, int(requested));
    }
    else if (std::strcmp(argv[4], "ERES-Batch") == 0) {
        std::cout
            << "Running reverse end-time batch maintenance with diagonal SCCID pruning..."
            << std::endl;
        Index = OptimizedIndex::buildERESBatch(
            Graph, a, int(requested));
    }
    else {
        std::cout
            << "Running ERES-ET single-edge maintenance with SCC-formation influence-interval pruning..."
            << std::endl;
        Index = OptimizedIndex::buildERESWithETSingleEdge(
            Graph, a, int(requested));
    }
        }
    catch (const std::bad_alloc& e) {
        std::cout << "Memory allocation failed during index construction/update: " << e.what() << std::endl;
        delete Graph;
        return 2;
    }
    catch (const std::exception& e) {
        std::cout << "Index construction/update failed with exception: " << e.what() << std::endl;
        delete Graph;
        return 2;
    }
    unsigned long long totalEnd = currentTime();

    if (Index == nullptr) {
        std::cout << "Index construction/update failed." << std::endl;
        delete Graph;
        return 1;
    }

    std::cout << "Processed update edges: "
              << Index->updatedEdgeCount() << std::endl;
    std::cout << "Update time: "
              << timeFormatting(Index->updateTimeMicros()).str()
              << std::endl;
    std::cout << std::fixed << std::setprecision(2)
              << "Average update time per edge: "
              << Index->averageUpdateTimeMicros() << " us"
              << std::endl;
    std::cout << "Final G/Chunk materialization time: "
              << timeFormatting(Index->materializationTimeMicros()).str()
              << std::endl;
    std::cout << "Total prefix-build plus update time: "
              << timeFormatting(totalEnd - totalStart).str()
              << std::endl;
    std::cout << "Index cost " << Index->size() << " bytes"
              << std::endl;

    std::cout << "Solving queries..." << std::endl;
    unsigned long long queryStart = currentTime();
    optimized(Index, vertex_num, argv[2], argv[3]);
    unsigned long long queryEnd = currentTime();
    std::cout << "Query completed in "
              << timeFormatting(queryEnd - queryStart).str()
              << std::endl;

    delete Index;
    delete Graph;
    return 0;
}

int main(int argc, char * argv[]) {

    std::ios::sync_with_stdio(false);

    if (argc == 7 && isSingleEdgeMode(argv[4])) {
        return runSingleEdgeMode(argc, argv);
    }

    unsigned long long start_time = currentTime();

    double update_fraction = 0.0;
    double subgraph_fraction = 1.0;

    if (std::strcmp(argv[argc - 1], "Debug") == 0) {
        debug = true;
        argc--;
    }

    if (argc == 6 && std::strcmp(argv[argc - 1], "update") == 0) {
        std::cout << "Index update mode enabled. Please input the fraction of timestamps to update (0 < x < 1): ";
        std::cin >> update_fraction;
        argc--;
    }

    if (argc == 6 && std::strcmp(argv[argc - 1], "subgraph") == 0) {
        std::cout << "Subgraph mode enabled. Please input the fraction of timestamps in the subgraph (0 < x < 1): ";
        std::cin >> subgraph_fraction;
        argc--;
    }

    if (argc != 5) {
        std::cout << "Parameters are non-standard. Please check the readme file." << std::endl;
    }

    TemporalGraph * Graph = build(argv, subgraph_fraction);
    int vertex_num = Graph->numOfVertices();

    if (std::strcmp(argv[argc - 1], "Online") == 0) {
        for (int i = 2; i < argc - 2; i++) {
            std::cout << "Running online search..." << std::endl;
            unsigned long long online_search_start_time = currentTime();
            online(Graph, argv[i], argv[argc - 2]);
            unsigned long long online_search_end_time = currentTime();
            std::cout << "Online search completed in " << timeFormatting(online_search_end_time - online_search_start_time).str() << std::endl;
        }
        delete Graph;
    }

    if (std::strcmp(argv[argc - 1], "Baseline") == 0) {
        std::cout << "Running baseline..." << std::endl;
        std::cout << "Constructing the index structure..." << std::endl;
        unsigned long long index_construction_start_time = currentTime();
        BaselineIndex *Index = new BaselineIndex(Graph, 1 - update_fraction);
        unsigned long long index_construction_end_time = currentTime();
        std::cout << "Index construction completed in " << timeFormatting(index_construction_end_time - index_construction_start_time).str() << std::endl;
        if (update_fraction > 0) {
            std::cout << "Updating the index structure..." << std::endl;
            int t1 = int(Graph->tmax * (1 - update_fraction));
            int num_of_edges = 0;
            for (int t = t1 + 1; t <= Graph->tmax; t++) {
                num_of_edges += Graph->temporal_edge[t].size();
            }
            std::cout << "Number of edges to be updated: " << num_of_edges << std::endl;
            unsigned long long index_update_start_time = currentTime();
            Index->update(Graph);
            unsigned long long index_update_end_time = currentTime();
            std::cout << "Index update completed in " << timeFormatting(index_update_end_time - index_update_start_time).str() << std::endl;
        }
        std::cout << "Index cost " << Index->size() << " bytes" << std::endl;
        delete Graph;
        for (int i = 2; i < argc - 2; i++) {
            std::cout << "Solving queries..." << std::endl;
            unsigned long long query_start_time = currentTime();
                baseline(Index, vertex_num, argv[i], argv[argc - 2]);
            unsigned long long query_end_time = currentTime();
            std::cout << "Query completed in " << timeFormatting(query_end_time - query_start_time).str() << std::endl;
        }
        std::cout << "Baseline completed!" << std::endl;
    }

    if (std::strcmp(argv[argc - 1], "RES-con") == 0) {
        std::cout << "Running optimized..." << std::endl;
        std::cout << "Constructing the index structure..." << std::endl;
        unsigned long long index_construction_start_time = currentTime();
        OptimizedIndex *Index = new OptimizedIndex(Graph, 1 - update_fraction);
        unsigned long long index_construction_end_time = currentTime();
        std::cout << "Index construction completed in " << timeFormatting(index_construction_end_time - index_construction_start_time).str() << std::endl;
        if (update_fraction > 0) {
            std::cout << "Updating the index structure..." << std::endl;
            int t1 = int(Graph->tmax * (1 - update_fraction));
            int num_of_edges = 0;
            for (int t = t1 + 1; t <= Graph->tmax; t++) {
                num_of_edges += Graph->temporal_edge[t].size();
            }
            std::cout << "Number of edges to be updated: " << num_of_edges << std::endl;
            unsigned long long index_update_start_time = currentTime();
            Index->update(Graph);
            unsigned long long index_update_end_time = currentTime();
            unsigned long long time_cost = index_update_end_time - index_update_start_time;
            std::cout << "Index update completed in " << timeFormatting(index_update_end_time - index_update_start_time).str() << std::endl;
            std::cout << "Average time cost for updating one edge: " << double(1.0 * time_cost / num_of_edges) << std::endl;
            std::cout << "Index update completed in " << timeFormatting(index_update_end_time - index_update_start_time).str() << std::endl;
        }
        std::cout << "Index cost " << Index->size() << " bytes" << std::endl;
        delete Graph;
        for (int i = 2; i < argc - 2; i++) {
            std::cout << "Solving queries..." << std::endl;
            unsigned long long query_start_time = currentTime();
            optimized(Index, vertex_num, argv[i], argv[argc - 2]);
            unsigned long long query_end_time = currentTime();
            std::cout << "Query completed in " << timeFormatting(query_end_time - query_start_time).str() << std::endl;
        }
        std::cout << "Optimized completed!" << std::endl;
    }

    if (std::strcmp(argv[argc - 1], "ERES-con") == 0) {
        if (update_fraction > 0) {
            std::cout << "ERES-con mode does not use update_fraction; please run without update." << std::endl;
            delete Graph;
            return 0;
        }

        std::cout << "Running optimized ERES full constructor..." << std::endl;
        std::cout << "Constructing the optimized end-time (t_e) index structure..." << std::endl;
        unsigned long long index_construction_start_time = currentTime();
        OptimizedIndex *Index = OptimizedIndex::buildERESConstructor(Graph);
        unsigned long long index_construction_end_time = currentTime();
        if (Index == nullptr) {
            std::cout << "ERES index construction failed." << std::endl;
            delete Graph;
            return 1;
        }
        std::cout << "ERES index construction completed in "
                  << timeFormatting(index_construction_end_time - index_construction_start_time).str()
                  << std::endl;
        std::cout << "Index cost " << Index->size() << " bytes" << std::endl;

        delete Graph;
        for (int i = 2; i < argc - 2; i++) {
            std::cout << "Solving queries with the ERES index..." << std::endl;
            unsigned long long query_start_time = currentTime();
            optimized(Index, vertex_num, argv[i], argv[argc - 2]);
            unsigned long long query_end_time = currentTime();
            std::cout << "Query completed in "
                      << timeFormatting(query_end_time - query_start_time).str()
                      << std::endl;
        }
        delete Index;
        std::cout << "ERES-con completed!" << std::endl;
    }

    if (std::strcmp(argv[argc - 1], "RES-Reverse") == 0) {
        if (update_fraction > 0) {
            std::cout << "RES-Reverse currently supports full construction only; update mode is not implemented." << std::endl;
            delete Graph;
            return 0;
        }

        std::cout << "Running end-time-indexed optimized..." << std::endl;
        std::cout << "Constructing the end-time (t_e) index structure..." << std::endl;
        unsigned long long index_construction_start_time = currentTime();
        OptimizedIndex *Index = OptimizedIndex::buildReverse(Graph, 1.0);
        unsigned long long index_construction_end_time = currentTime();
        if (Index == nullptr) {
            std::cout << "End-time index construction failed." << std::endl;
            delete Graph;
            return 1;
        }
        std::cout << "End-time index construction completed in "
                  << timeFormatting(index_construction_end_time - index_construction_start_time).str()
                  << std::endl;
        std::cout << "Index cost " << Index->size() << " bytes" << std::endl;

        delete Graph;
        for (int i = 2; i < argc - 2; i++) {
            std::cout << "Solving queries with the end-time index..." << std::endl;
            unsigned long long query_start_time = currentTime();
            optimized(Index, vertex_num, argv[i], argv[argc - 2]);
            unsigned long long query_end_time = currentTime();
            std::cout << "Query completed in "
                      << timeFormatting(query_end_time - query_start_time).str()
                      << std::endl;
        }
        delete Index;
        std::cout << "End-time-indexed optimized completed!" << std::endl;
    }
    
    unsigned long long end_time = currentTime();
    std::cout << "Program finished in " << timeFormatting(difftime(end_time, start_time)).str() << std::endl;

    return 0;

}
