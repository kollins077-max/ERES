#ifndef OPTIMIZED_INDEX_EXPERIMENTAL_H
#define OPTIMIZED_INDEX_EXPERIMENTAL_H

#include "commonfunctions.h"
#include "temporal_graph.h"
#include <cstdint>

class OptimizedIndex {

    private:

        typedef std::pair<long long,int> EncodedEdge;

        // find(ts, u, id): find the label of u at time t(ts, u, id) with start time ts.
        int find(int u);

        int find_an_index(int t, int ts, int te);
    
        // unioN(ts, u, v, t): perform the union operation on u and v at time t with start time ts.

        void kosaraju1(int now);
        void kosaraju3(int now);
        void kosaraju5(int now);
        void kosaraju2(int now, int ts);
        void kosaraju4(int now, int ori,int ts);
        int top,col,len;
        int *inOrder = nullptr;
        int *outOrder = nullptr;
        int *lowestOrder = nullptr;
        int *Sta = nullptr;
        int *Vis = nullptr;
        int *Vis2 = nullptr;
        int *f = nullptr;
        struct RES{
            std::pair<long long,int> edge;
            int ts;
            RES(std::pair<long long,int> e, int t){
                edge=e;
                ts=t;
            }
        };
        static bool cmp(RES a, RES b);
        static bool cmpReverse(RES a, RES b);
        bool reverseIndex = false;
        std::stringstream solveReverse(int n, int ts, int te);
        std::vector<int> markedVertices;
        std::vector<int> markedVertices2;
        std::set<std::pair<long long,int>> key;
        std::vector<std::pair<long long,int>> *outLabel = nullptr;
        std::vector<std::pair<long long,int>> *outLabel2 = nullptr;
        std::map<std::pair<long long,int>,std::pair<int,int> >  S;
        std::map<EncodedEdge, std::vector<std::pair<int,int>>> appearanceIntervals;
        std::vector<int> *actual_time = nullptr;
        std::vector<int> originalVertexIds;
        std::vector<int> *actual_time_temporal = nullptr;
        std::vector<std::pair<long long,int>> *edge = nullptr;
        std::vector<std::pair<long long,int>> tmpedge;
        std::vector<RES> *G = nullptr;
        std::vector<RES> *Chunk = nullptr;
        //std::vector<std::vector<std::pair<long long,int>>> *G_temporal;

        std::stack<int> Stack;
        std::vector<int> CC;
        std::vector<std::pair<long long,int>> *newedge = nullptr;
        std::set<std::pair<long long,int>> beta;
        std::set<std::pair<long long,int>> tmper;
        std::vector<std::pair<long long,int>> alfa;

        int lastUpdatedEdgeCount = 0;
        unsigned long long lastUpdateTimeMicros = 0;
        unsigned long long lastMaterializationTimeMicros = 0;

        enum SingleEdgeMode {
            ORIGINAL_SINGLE_EDGE = 0,
            ERES_ET_SINGLE_EDGE = 1,
            ERES_ET_NO_PRUNE_SINGLE_EDGE = 2,
            ERES_SINGLE_EDGE = 3,
            ERES_BATCH = 4,
            ORIGINAL_BATCH = 5,
            RES_ET_SINGLE_EDGE = 6
        };

        void initializeAppearanceIntervals();
        std::set<EncodedEdge> recoverPhiAt(int startTime) const;
        void addPhiMembershipAt(int startTime, const std::set<EncodedEdge>& phi);
        void removePhiMembershipRange(int left, int right);
        void rebuildForwardStorage();
        void rebuildReverseStorage();
        void rebuildReverseStorageCollapsed();
        void modifySingleEdgeOriginalFlow(
            const std::vector<std::vector<std::pair<int,int>>>& activeEdges,
            int uNew, int vNew, int tNew);
        void modifySingleEdgeOriginalFlowRestricted(
            const std::vector<std::vector<std::pair<int,int>>>& activeEdges,
            int uNew, int vNew, int tNew, int left, int right);
        static std::map<int, std::set<EncodedEdge>> computeReverseConstructorPhiRange(
            int vertexCount,
            const std::vector<std::vector<std::pair<int,int>>>& activeEdges,
            int leftEnd,
            int rightEnd,
            int contextRightEnd = -1);
        static std::map<int, std::set<EncodedEdge>> computeReverseConstructorPhiRangeDiagonalPruned(
            int vertexCount,
            const std::vector<std::vector<std::pair<int,int>>>& activeEdges,
            int leftEnd,
            int rightEnd,
            int contextRightEnd,
            unsigned long long *stats,
            bool reportProgress = false,
            unsigned long long progressStartTime = 0,
            OptimizedIndex *directIntervalTarget = nullptr,
            const std::vector<int> *vertexRemap = nullptr);
        static OptimizedIndex * buildSingleEdgeExperiment(
            TemporalGraph * Graph, double a, int b, SingleEdgeMode mode);

    public:

        // n, m, tmax: graph information.
        int n, m, tmax, t1;

        std::stringstream solve(int n, int ts, int te);
       void update(TemporalGraph * Graph);
       void modify(TemporalGraph * Graph,int tpre,int tim);
        OptimizedIndex() = default;
        OptimizedIndex(TemporalGraph * Graph, double t_fraction);
        static OptimizedIndex * buildReverse(TemporalGraph * Graph, double t_fraction);
        static OptimizedIndex * buildERESConstructor(TemporalGraph * Graph);
        static OptimizedIndex * buildOriginalSingleEdge(
            TemporalGraph * Graph, double a, int b);
        static OptimizedIndex * buildRESWithETSingleEdge(
            TemporalGraph * Graph, double a, int b);
        static OptimizedIndex * buildOriginalBatch(
            TemporalGraph * Graph, double a, int b);
        static OptimizedIndex * buildERESWithETSingleEdge(
            TemporalGraph * Graph, double a, int b);
        static OptimizedIndex * buildERESWithETNoPruneSingleEdge(
            TemporalGraph * Graph, double a, int b);
        static OptimizedIndex * buildERESSingleEdge(
            TemporalGraph * Graph, double a, int b);
        static OptimizedIndex * buildERESBatch(
            TemporalGraph * Graph, double a, int b);
        ~OptimizedIndex();

        std::uint64_t size();
        int timeHorizon() const;
        int updatedEdgeCount() const;
        unsigned long long updateTimeMicros() const;
        unsigned long long materializationTimeMicros() const;
        double averageUpdateTimeMicros() const;

};

void optimized(OptimizedIndex * Index, int vertex_num, char * query_file, char * output_file);

#endif
