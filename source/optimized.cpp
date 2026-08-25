#include "optimized.h"
#include<math.h>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <iomanip>
#include <limits>

namespace {

typedef std::pair<long long,int> ExperimentalEdge;

struct ExperimentalEdgeHash {
    std::size_t operator()(const ExperimentalEdge& edge) const {
        std::size_t h1 = std::hash<long long>()(edge.first);
        std::size_t h2 = std::hash<int>()(edge.second);
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};

struct StreamEdge {
    int u;
    int v;
    int t;
};

struct QuotientEdge {
    int from;
    int to;
    ExperimentalEdge original;
};

struct SccResult {
    std::vector<int> component;
    std::vector<std::vector<int>> groups;
};

struct ERESPruningStats {
    unsigned long long initialInternalNonResPruned = 0;
    unsigned long long initialInternalResSelected = 0;
    unsigned long long arrivalAlreadyInternal = 0;
    unsigned long long reachabilityRedundantSkipped = 0;
    unsigned long long duplicateSuperEdgeSkipped = 0;
    unsigned long long shrinkInternalNonResPruned = 0;
    unsigned long long shrinkInternalResSelected = 0;
    std::set<ExperimentalEdge> uniqueInitialInternalNonRes;
    std::set<ExperimentalEdge> uniqueShrinkInternalNonRes;
};

struct ComponentHistory {
    int n;
    std::vector<std::vector<std::pair<int,int>>> changes;
    std::vector<int> touchedVertices;

    ComponentHistory(int vertexCount = 0)
        : n(vertexCount), changes(vertexCount) {
    }

    void reset(int vertexCount) {
        n = vertexCount;
        changes.assign(vertexCount, std::vector<std::pair<int,int>>());
        touchedVertices.clear();
    }

    void clearChanges() {
        for (std::vector<int>::const_iterator it = touchedVertices.begin();
             it != touchedVertices.end(); ++it) {
            changes[*it].clear();
        }
        touchedVertices.clear();
    }

    void addChange(int vertex, int start, int component) {
        if (vertex < 0 || vertex >= n) {
            return;
        }
        std::vector<std::pair<int,int>>& list = changes[vertex];
        if (list.empty()) {
            touchedVertices.push_back(vertex);
        }
        if (!list.empty() && list.back().first == start) {
            list.back().second = component;
            return;
        }
        if (!list.empty() && list.back().second == component) {
            return;
        }
        list.push_back(std::make_pair(start, component));
    }

    void finalize() {
        for (std::vector<int>::const_iterator it = touchedVertices.begin();
             it != touchedVertices.end(); ++it) {
            std::reverse(changes[*it].begin(), changes[*it].end());
        }
    }

    void swapContents(ComponentHistory& other) {
        changes.swap(other.changes);
        touchedVertices.swap(other.touchedVertices);
    }

    int componentAt(int vertex, int start) const {
        if (vertex < 0 || vertex >= n) {
            return vertex;
        }
        const std::vector<std::pair<int,int>>& list = changes[vertex];
        int left = 0;
        int right = int(list.size());
        while (left < right) {
            int mid = left + (right - left) / 2;
            if (list[mid].first < start) {
                left = mid + 1;
            }
            else {
                right = mid;
            }
        }
        if (left == int(list.size())) {
            return vertex;
        }
        return list[left].second;
    }

    int previousChangeStart(int vertex, int start) const {
        if (vertex < 0 || vertex >= n) {
            return -1;
        }
        const std::vector<std::pair<int,int>>& list = changes[vertex];
        int left = 0;
        int right = int(list.size());
        while (left < right) {
            int mid = left + (right - left) / 2;
            if (list[mid].first < start) {
                left = mid + 1;
            }
            else {
                right = mid;
            }
        }

        if (left == int(list.size())) {
            return list.empty() ? -1 : list.back().first;
        }
        if (list[left].first == start) {
            return left > 0 ? list[left - 1].first : -1;
        }
        return left > 0 ? list[left - 1].first : -1;
    }

    int nextJointChangeStart(int u, int v, int start) const {
        int nextU = previousChangeStart(u, start);
        int nextV = previousChangeStart(v, start);
        return std::max(nextU, nextV);
    }
};

ExperimentalEdge encodeEdge(int u, int v, int t) {
    long long encoded =
        (((long long)u) << 37) + (((long long)v) << 12);
    return std::make_pair(encoded, t);
}

int edgeSource(const ExperimentalEdge& edge) {
    return int(edge.first >> 37);
}

int edgeDestination(const ExperimentalEdge& edge) {
    return int((edge.first >> 12) & 33554431ll);
}

long long encodeSuperPair(int from, int to) {
    return (static_cast<long long>(from) << 32) ^
           static_cast<unsigned int>(to);
}

SccResult computeScc(
    int nodeCount,
    const std::vector<std::pair<int,int>>& edges) {

    std::vector<std::vector<int>> out(nodeCount);
    std::vector<std::vector<int>> in(nodeCount);
    for (std::vector<std::pair<int,int>>::const_iterator it = edges.begin();
         it != edges.end(); ++it) {
        if (it->first < 0 || it->first >= nodeCount ||
            it->second < 0 || it->second >= nodeCount) {
            continue;
        }
        out[it->first].push_back(it->second);
        in[it->second].push_back(it->first);
    }

    std::vector<char> visited(nodeCount, 0);
    std::vector<int> order;
    order.reserve(nodeCount);

    for (int start = 0; start < nodeCount; ++start) {
        if (visited[start]) {
            continue;
        }

        std::vector<std::pair<int,std::size_t>> stack;
        visited[start] = 1;
        stack.push_back(std::make_pair(start, std::size_t(0)));
        while (!stack.empty()) {
            int u = stack.back().first;
            std::size_t& next = stack.back().second;
            if (next < out[u].size()) {
                int v = out[u][next++];
                if (!visited[v]) {
                    visited[v] = 1;
                    stack.push_back(std::make_pair(v, std::size_t(0)));
                }
            }
            else {
                order.push_back(u);
                stack.pop_back();
            }
        }
    }

    SccResult result;
    result.component.assign(nodeCount, -1);
    for (int pos = nodeCount - 1; pos >= 0; --pos) {
        int start = order[pos];
        if (result.component[start] != -1) {
            continue;
        }

        int componentId = int(result.groups.size());
        result.groups.push_back(std::vector<int>());
        std::vector<int> stack(1, start);
        result.component[start] = componentId;
        while (!stack.empty()) {
            int u = stack.back();
            stack.pop_back();
            result.groups.back().push_back(u);
            for (std::vector<int>::const_iterator it = in[u].begin();
                 it != in[u].end(); ++it) {
                if (result.component[*it] == -1) {
                    result.component[*it] = componentId;
                    stack.push_back(*it);
                }
            }
        }
    }
    return result;
}

std::set<ExperimentalEdge> selectCondensationTreeEdges(
    int nodeCount,
    const std::vector<QuotientEdge>& edges,
    const SccResult& scc) {

    std::vector<std::vector<int>> out(nodeCount);
    std::vector<std::vector<int>> in(nodeCount);
    for (int i = 0; i < int(edges.size()); ++i) {
        out[edges[i].from].push_back(i);
        in[edges[i].to].push_back(i);
    }

    std::set<ExperimentalEdge> selected;
    std::vector<char> visited(nodeCount, 0);
    std::queue<int> queue;

    for (int componentId = 0;
         componentId < int(scc.groups.size()); ++componentId) {
        const std::vector<int>& group = scc.groups[componentId];
        if (group.size() <= 1) {
            continue;
        }

        int root = group.front();
        for (std::vector<int>::const_iterator it = group.begin();
             it != group.end(); ++it) {
            visited[*it] = 0;
        }
        visited[root] = 1;
        queue.push(root);
        while (!queue.empty()) {
            int u = queue.front();
            queue.pop();
            for (std::vector<int>::const_iterator it = out[u].begin();
                 it != out[u].end(); ++it) {
                const QuotientEdge& edge = edges[*it];
                if (scc.component[edge.to] != componentId ||
                    visited[edge.to]) {
                    continue;
                }
                visited[edge.to] = 1;
                selected.insert(edge.original);
                queue.push(edge.to);
            }
        }

        for (std::vector<int>::const_iterator it = group.begin();
             it != group.end(); ++it) {
            visited[*it] = 0;
        }
        visited[root] = 1;
        queue.push(root);
        while (!queue.empty()) {
            int u = queue.front();
            queue.pop();
            for (std::vector<int>::const_iterator it = in[u].begin();
                 it != in[u].end(); ++it) {
                const QuotientEdge& edge = edges[*it];
                if (scc.component[edge.from] != componentId ||
                    visited[edge.from]) {
                    continue;
                }
                visited[edge.from] = 1;
                selected.insert(edge.original);
                queue.push(edge.from);
            }
        }
    }

    return selected;
}

int latestStartWithPath(
    int n,
    const std::vector<std::vector<std::pair<int,int>>>& activeEdges,
    int source,
    int target,
    int maximumTime) {

    if (source == target) {
        return maximumTime;
    }
    if (source < 0 || source >= n || target < 0 || target >= n) {
        return -1;
    }

    std::vector<std::vector<int>> adjacency(n);
    std::vector<char> reachable(n, 0);
    std::queue<int> queue;
    reachable[source] = 1;

    int upper = std::min(maximumTime, int(activeEdges.size()) - 1);
    for (int time = upper; time >= 0; --time) {
        const std::vector<std::pair<int,int>>& bucket = activeEdges[time];
        for (std::vector<std::pair<int,int>>::const_iterator it =
                 bucket.begin(); it != bucket.end(); ++it) {
            adjacency[it->first].push_back(it->second);
        }
        for (std::vector<std::pair<int,int>>::const_iterator it =
                 bucket.begin(); it != bucket.end(); ++it) {
            if (reachable[it->first] && !reachable[it->second]) {
                reachable[it->second] = 1;
                queue.push(it->second);
            }
        }
        while (!queue.empty()) {
            int u = queue.front();
            queue.pop();
            for (std::vector<int>::const_iterator it = adjacency[u].begin();
                 it != adjacency[u].end(); ++it) {
                if (!reachable[*it]) {
                    reachable[*it] = 1;
                    queue.push(*it);
                }
            }
        }
        if (reachable[target]) {
            return time;
        }
    }
    return -1;
}

int earliestEndWithPath(
    int n,
    const std::vector<std::vector<std::pair<int,int>>>& activeEdges,
    int source,
    int target,
    int upperEnd,
    int lowerTime = 0) {

    if (source < 0 || source >= n || target < 0 || target >= n ||
        upperEnd < 0 || activeEdges.empty()) {
        return -1;
    }
    int upper = std::min(upperEnd, int(activeEdges.size()) - 1);
    int lower = std::max(0, lowerTime);
    if (lower > upper) {
        return -1;
    }
    if (source == target) {
        return lower;
    }
    std::vector<char> reachable(n, 0);
    std::vector<std::vector<int>> pending(n);
    std::queue<int> queue;
    reachable[source] = 1;

    for (int time = lower; time <= upper; ++time) {
        const std::vector<std::pair<int,int>>& bucket = activeEdges[time];
        for (std::vector<std::pair<int,int>>::const_iterator it =
                 bucket.begin(); it != bucket.end(); ++it) {
            int from = it->first;
            int to = it->second;
            if (from < 0 || from >= n || to < 0 || to >= n) {
                continue;
            }

            if (reachable[from]) {
                if (!reachable[to]) {
                    reachable[to] = 1;
                    queue.push(to);
                }
            }
            else {
                pending[from].push_back(to);
            }

            while (!queue.empty()) {
                int x = queue.front();
                queue.pop();
                for (std::vector<int>::const_iterator y =
                         pending[x].begin();
                     y != pending[x].end(); ++y) {
                    if (!reachable[*y]) {
                        reachable[*y] = 1;
                        queue.push(*y);
                    }
                }
                pending[x].clear();
            }
        }

        if (reachable[target]) {
            return time;
        }
    }

    return -1;
}

std::pair<int,int> computeEffectiveInterval(
    int n,
    const std::vector<std::vector<std::pair<int,int>>>& activeEdges,
    const StreamEdge& newEdge) {

    if (newEdge.u == newEdge.v) {
        return std::make_pair(-1, -1);
    }

    int reverseThreshold = latestStartWithPath(
        n, activeEdges, newEdge.v, newEdge.u, newEdge.t);
    if (reverseThreshold < 0) {
        return std::make_pair(-1, -1);
    }

    int forwardThreshold = latestStartWithPath(
        n, activeEdges, newEdge.u, newEdge.v, newEdge.t);
    if (forwardThreshold >= reverseThreshold) {
        return std::make_pair(-1, -1);
    }

    return std::make_pair(forwardThreshold + 1, reverseThreshold);
}

std::pair<int,int> computePaperInfluenceInterval(
    int n,
    const std::vector<std::vector<std::pair<int,int>>>& activeEdges,
    const StreamEdge& newEdge) {

    int tl = -1;
    int tr = -1;
    int tEdge = newEdge.t;
    if (tEdge < 0 ||
        newEdge.u < 0 || newEdge.u >= n ||
        newEdge.v < 0 || newEdge.v >= n) {
        return std::make_pair(-1, -1);
    }

    int upper = std::min(tEdge - 1, int(activeEdges.size()) - 1);
    if (upper < 0) {
        return std::make_pair(-1, -1);
    }

    int tlOpt = -1;
    for (int time = upper; time >= 0; --time) {
        const std::vector<std::pair<int,int>>& bucket = activeEdges[time];
        bool foundSameEndpoint = false;
        for (std::vector<std::pair<int,int>>::const_iterator it =
                 bucket.begin(); it != bucket.end(); ++it) {
            if (it->first == newEdge.u && it->second == newEdge.v) {
                tlOpt = time + 1;
                foundSameEndpoint = true;
                break;
            }
        }
        if (foundSameEndpoint) {
            break;
        }
    }

    std::vector<char> forwardReach(n, 0);
    std::vector<char> reverseReach(n, 0);
    std::vector<std::vector<int>> pendingForward(n);
    std::vector<std::vector<int>> pendingReverse(n);
    std::queue<int> forwardQueue;
    std::queue<int> reverseQueue;

    forwardReach[newEdge.v] = 1;
    reverseReach[newEdge.u] = 1;
    bool connected = (newEdge.v == newEdge.u);
    tl = tlOpt;

    for (int time = upper; time >= 0; --time) {
        const std::vector<std::pair<int,int>>& bucket = activeEdges[time];
        for (std::vector<std::pair<int,int>>::const_iterator it =
                 bucket.begin(); it != bucket.end(); ++it) {
            int from = it->first;
            int to = it->second;
            if (from < 0 || from >= n || to < 0 || to >= n) {
                continue;
            }

            if (forwardReach[from]) {
                if (!forwardReach[to]) {
                    forwardReach[to] = 1;
                    forwardQueue.push(to);
                    if (reverseReach[to]) {
                        connected = true;
                    }
                }
            }
            else {
                pendingForward[from].push_back(to);
            }

            if (reverseReach[to]) {
                if (!reverseReach[from]) {
                    reverseReach[from] = 1;
                    reverseQueue.push(from);
                    if (forwardReach[from]) {
                        connected = true;
                    }
                }
            }
            else {
                pendingReverse[to].push_back(from);
            }

            while (!forwardQueue.empty()) {
                int x = forwardQueue.front();
                forwardQueue.pop();
                for (std::vector<int>::const_iterator y =
                         pendingForward[x].begin();
                     y != pendingForward[x].end(); ++y) {
                    if (!forwardReach[*y]) {
                        forwardReach[*y] = 1;
                        forwardQueue.push(*y);
                        if (reverseReach[*y]) {
                            connected = true;
                        }
                    }
                }
                pendingForward[x].clear();
            }

            while (!reverseQueue.empty()) {
                int x = reverseQueue.front();
                reverseQueue.pop();
                for (std::vector<int>::const_iterator y =
                         pendingReverse[x].begin();
                     y != pendingReverse[x].end(); ++y) {
                    if (!reverseReach[*y]) {
                        reverseReach[*y] = 1;
                        reverseQueue.push(*y);
                        if (forwardReach[*y]) {
                            connected = true;
                        }
                    }
                }
                pendingReverse[x].clear();
            }

            if (connected) {
                if (tr == -1) {
                    tr = time;
                    if (tlOpt != -1) {
                        if (tlOpt <= tr) {
                            return std::make_pair(tlOpt, tr);
                        }
                        return std::make_pair(-1, -1);
                    }
                }
                tl = time;
            }
        }
    }

    if (tl == -1 || tr == -1 || tl > tr) {
        return std::make_pair(-1, -1);
    }
    return std::make_pair(tl, tr);
}

std::pair<int,int> computeForwardResInfluenceInterval(
    int n,
    const std::vector<std::vector<std::pair<int,int>>>& activeEdges,
    const StreamEdge& newEdge) {

    // The paper scan starts at t_new-1 because it assumes a strictly newer
    // timestamp.  The CLI experiment also permits edges in one timestamp
    // bucket to arrive one by one, so include that bucket in this case.
    if (newEdge.t >= 0 && newEdge.t < int(activeEdges.size()) &&
        !activeEdges[newEdge.t].empty()) {
        return computeEffectiveInterval(n, activeEdges, newEdge);
    }
    return computePaperInfluenceInterval(n, activeEdges, newEdge);
}

std::pair<int,int> computePaperReverseInfluenceInterval(
    int n,
    const std::vector<std::vector<std::pair<int,int>>>& activeEdges,
    const StreamEdge& newEdge,
    int maximumEndTime) {

    if (newEdge.u == newEdge.v) {
        return std::make_pair(-1, -1);
    }
    if (newEdge.t < 0 ||
        newEdge.u < 0 || newEdge.u >= n ||
        newEdge.v < 0 || newEdge.v >= n) {
        return std::make_pair(-1, -1);
    }

    int upper = std::min(maximumEndTime, int(activeEdges.size()) - 1);
    if (upper < newEdge.t) {
        return std::make_pair(-1, -1);
    }

    // Time-dual of the exact two-threshold forward effective interval:
    //
    //   forward axis: [latest(u -> v) + 1, latest(v -> u)]
    //
    // For the reverse/end-time axis with fixed start 0, SCC-changing windows
    // begin when the old graph first contains v -> u, and stop immediately
    // before the old graph first contains u -> v.  A later old edge with the
    // same endpoints is only one special case of the u -> v redundancy test.
    int closeAt = earliestEndWithPath(
        n, activeEdges, newEdge.v, newEdge.u, upper);
    if (closeAt < 0) {
        return std::make_pair(-1, -1);
    }

    int left = std::max(newEdge.t, closeAt);
    // Redundancy on the end-time axis is safe only if the replacement
    // u -> v path survives every query start that can contain the new edge.
    // Therefore the replacement path may use only old edges with timestamps
    // >= t_new.  An earlier old path such as (u,v,t'<t_new) would disappear
    // in queries with start time t'+1..t_new and must not truncate the
    // reverse interval.
    int redundantAt = earliestEndWithPath(
        n, activeEdges, newEdge.u, newEdge.v, upper, newEdge.t);
    int right = (redundantAt < 0) ? upper : redundantAt - 1;

    if (left < newEdge.t || left > right) {
        return std::make_pair(-1, -1);
    }
    return std::make_pair(left, right);
}

class ReverseCondensedState {
    private:
        int n;
        std::vector<int> baseComponent;
        std::vector<int> parent;
        std::vector<int> rank;
        std::vector<QuotientEdge> candidates;

        int findRoot(int u) {
            if (parent[u] != u) {
                parent[u] = findRoot(parent[u]);
            }
            return parent[u];
        }

        void unite(int u, int v) {
            u = findRoot(u);
            v = findRoot(v);
            if (u == v) {
                return;
            }
            if (rank[u] < rank[v]) {
                std::swap(u, v);
            }
            parent[v] = u;
            if (rank[u] == rank[v]) {
                ++rank[u];
            }
        }

    public:
        ReverseCondensedState(
            int vertexCount,
            const std::set<ExperimentalEdge>& oldPhi)
            : n(vertexCount) {

            std::vector<std::pair<int,int>> edges;
            edges.reserve(oldPhi.size());
            for (std::set<ExperimentalEdge>::const_iterator it =
                     oldPhi.begin(); it != oldPhi.end(); ++it) {
                edges.push_back(
                    std::make_pair(edgeSource(*it), edgeDestination(*it)));
            }
            SccResult oldScc = computeScc(vertexCount, edges);
            baseComponent = oldScc.component;
            parent.resize(oldScc.groups.size());
            rank.assign(oldScc.groups.size(), 0);
            for (int i = 0; i < int(parent.size()); ++i) {
                parent[i] = i;
            }
        }

        void addRawEdge(int u, int v, int time) {
            QuotientEdge edge;
            edge.from = baseComponent[u];
            edge.to = baseComponent[v];
            edge.original = encodeEdge(u, v, time);
            candidates.push_back(edge);
        }

        void contractNewSccs(std::set<ExperimentalEdge>& phi) {
            std::map<int,int> compact;
            for (int i = 0; i < int(parent.size()); ++i) {
                int root = findRoot(i);
                if (!compact.count(root)) {
                    compact[root] = int(compact.size());
                }
            }

            std::vector<QuotientEdge> quotient;
            for (std::vector<QuotientEdge>::const_iterator it =
                     candidates.begin(); it != candidates.end(); ++it) {
                int fromRoot = findRoot(it->from);
                int toRoot = findRoot(it->to);
                if (fromRoot == toRoot) {
                    continue;
                }
                QuotientEdge edge = *it;
                edge.from = compact[fromRoot];
                edge.to = compact[toRoot];
                quotient.push_back(edge);
            }
            if (quotient.empty()) {
                return;
            }

            std::vector<std::pair<int,int>> pairs;
            pairs.reserve(quotient.size());
            for (std::vector<QuotientEdge>::const_iterator it =
                     quotient.begin(); it != quotient.end(); ++it) {
                pairs.push_back(std::make_pair(it->from, it->to));
            }
            SccResult scc = computeScc(int(compact.size()), pairs);
            std::set<ExperimentalEdge> additions =
                selectCondensationTreeEdges(int(compact.size()), quotient, scc);
            phi.insert(additions.begin(), additions.end());

            std::vector<int> compactToRoot(compact.size(), -1);
            for (std::map<int,int>::const_iterator it = compact.begin();
                 it != compact.end(); ++it) {
                compactToRoot[it->second] = it->first;
            }
            for (std::vector<std::vector<int>>::const_iterator group =
                     scc.groups.begin(); group != scc.groups.end(); ++group) {
                if (group->size() <= 1) {
                    continue;
                }
                int representative = compactToRoot[group->front()];
                for (std::vector<int>::const_iterator it = group->begin() + 1;
                     it != group->end(); ++it) {
                    unite(representative, compactToRoot[*it]);
                }
            }
    }
};

std::set<ExperimentalEdge> computeReversePhiAtEnd(
    int n,
    const std::vector<std::vector<std::pair<int,int>>>& activeEdges,
    int endTime) {

    std::set<ExperimentalEdge> phi;
    ReverseCondensedState state(n, phi);
    int upper = std::min(endTime, int(activeEdges.size()) - 1);
    for (int start = upper; start >= 0; --start) {
        const std::vector<std::pair<int,int>>& bucket =
            activeEdges[start];
        for (std::vector<std::pair<int,int>>::const_iterator it =
                 bucket.begin(); it != bucket.end(); ++it) {
            state.addRawEdge(it->first, it->second, start);
        }
        state.contractNewSccs(phi);
    }
    return phi;
}

std::set<ExperimentalEdge> computeReversePhiAtEndRestricted(
    int n,
    const std::vector<std::vector<std::pair<int,int>>>& activeEdgesBefore,
    const StreamEdge& newEdge,
    int endTime,
    int intervalLeft,
    int intervalRight,
    const std::set<ExperimentalEdge>& previousPhi) {

    int upper = std::min(endTime, int(activeEdgesBefore.size()) - 1);
    int right = std::min(intervalRight, upper);
    int left = std::max(0, intervalLeft);
    if (left > right) {
        return previousPhi;
    }

    std::set<ExperimentalEdge> prefixPhi;
    ReverseCondensedState prefixState(n, prefixPhi);

    // Important: the prefix cannot be restored from RES edges alone.  Some
    // raw edges may be dormant at starts > tr (not selected into RES yet) but
    // become necessary when edges in [tl,tr] arrive.  Therefore the prefix
    // condensation state is reconstructed from the old raw stream above tr.
    for (int start = upper; start > right; --start) {
        const std::vector<std::pair<int,int>>& bucket =
            activeEdgesBefore[start];
        for (std::vector<std::pair<int,int>>::const_iterator it =
                 bucket.begin(); it != bucket.end(); ++it) {
            prefixState.addRawEdge(it->first, it->second, start);
        }
        prefixState.contractNewSccs(prefixPhi);
    }

    ReverseCondensedState oldState = prefixState;
    std::set<ExperimentalEdge> oldPhi = prefixPhi;
    for (int start = right; start >= left; --start) {
        const std::vector<std::pair<int,int>>& bucket =
            activeEdgesBefore[start];
        for (std::vector<std::pair<int,int>>::const_iterator it =
                 bucket.begin(); it != bucket.end(); ++it) {
            oldState.addRawEdge(it->first, it->second, start);
        }
        oldState.contractNewSccs(oldPhi);
    }

    std::set<ExperimentalEdge> oldAffected;
    for (std::set<ExperimentalEdge>::const_iterator it =
             oldPhi.begin(); it != oldPhi.end(); ++it) {
        if (!prefixPhi.count(*it)) {
            oldAffected.insert(*it);
        }
    }

    ReverseCondensedState newState = prefixState;
    std::set<ExperimentalEdge> newPhi = prefixPhi;
    newState.addRawEdge(newEdge.u, newEdge.v, newEdge.t);
    newState.contractNewSccs(newPhi);

    // Only the affected start interval [tl, tr] is traversed after the new
    // edge is introduced.  Starts below tl are intentionally left untouched.
    for (int start = right; start >= left; --start) {
        const std::vector<std::pair<int,int>>& bucket =
            activeEdgesBefore[start];
        for (std::vector<std::pair<int,int>>::const_iterator it =
                 bucket.begin(); it != bucket.end(); ++it) {
            newState.addRawEdge(it->first, it->second, start);
        }
        newState.contractNewSccs(newPhi);
    }

    std::set<ExperimentalEdge> newAffected;
    for (std::set<ExperimentalEdge>::const_iterator it =
             newPhi.begin(); it != newPhi.end(); ++it) {
        if (!prefixPhi.count(*it)) {
            newAffected.insert(*it);
        }
    }

    std::set<ExperimentalEdge> updatedPhi = previousPhi;
    for (std::set<ExperimentalEdge>::const_iterator it =
             oldAffected.begin(); it != oldAffected.end(); ++it) {
        updatedPhi.erase(*it);
    }
    for (std::set<ExperimentalEdge>::const_iterator it =
             newAffected.begin(); it != newAffected.end(); ++it) {
        updatedPhi.insert(*it);
    }
    return updatedPhi;
}

bool superReachable(
    int nodeCount,
    const std::vector<std::vector<int>>& adjacency,
    int source,
    int target) {

    if (source == target) {
        return true;
    }
    if (source < 0 || source >= nodeCount ||
        target < 0 || target >= nodeCount) {
        return false;
    }

    std::vector<char> visited(nodeCount, 0);
    std::queue<int> queue;
    visited[source] = 1;
    queue.push(source);
    while (!queue.empty()) {
        int u = queue.front();
        queue.pop();
        for (std::vector<int>::const_iterator it = adjacency[u].begin();
             it != adjacency[u].end(); ++it) {
            if (*it == target) {
                return true;
            }
            if (!visited[*it]) {
                visited[*it] = 1;
                queue.push(*it);
            }
        }
    }
    return false;
}

void rebuildSuperAdjacency(
    int nodeCount,
    const std::vector<QuotientEdge>& edges,
    std::vector<std::vector<int>>& adjacency,
    std::unordered_set<long long>& edgeExists) {

    adjacency.assign(nodeCount, std::vector<int>());
    edgeExists.clear();
    edgeExists.reserve(edges.size() * 2 + 1);
    for (std::vector<QuotientEdge>::const_iterator it = edges.begin();
         it != edges.end(); ++it) {
        if (it->from < 0 || it->from >= nodeCount ||
            it->to < 0 || it->to >= nodeCount ||
            it->from == it->to) {
            continue;
        }
        long long key = encodeSuperPair(it->from, it->to);
        if (!edgeExists.insert(key).second) {
            continue;
        }
        adjacency[it->from].push_back(it->to);
    }
}

std::set<ExperimentalEdge> computeERESIncrementalPhiForStartRange(
    int n,
    const std::vector<std::vector<std::pair<int,int>>>& activeEdges,
    int endTime,
    int leftStart,
    int rightStart,
    const std::set<ExperimentalEdge>& inheritedPhi,
    bool enableIntraSccPruning,
    ERESPruningStats *pruningStats) {

    std::set<ExperimentalEdge> result = inheritedPhi;
    if (n <= 0 || activeEdges.empty() || endTime < 0) {
        return result;
    }

    int end = std::min(endTime, int(activeEdges.size()) - 1);
    int left = std::max(0, leftStart);
    int right = std::min(rightStart, end);
    if (left > right) {
        return result;
    }

    std::vector<std::pair<int,int>> initialPairs;
    std::vector<QuotientEdge> initialEdges;
    std::vector<int> originalToCompact(n, -1);
    std::vector<int> compactToOriginal;
    compactToOriginal.reserve(std::min(n, end - right + 1));
    auto ensureCompactVertex = [&](int vertex) -> int {
        int compact = originalToCompact[vertex];
        if (compact >= 0) {
            return compact;
        }
        compact = int(compactToOriginal.size());
        originalToCompact[vertex] = compact;
        compactToOriginal.push_back(vertex);
        return compact;
    };

    for (int time = right; time <= end; ++time) {
        const std::vector<std::pair<int,int>>& bucket =
            activeEdges[time];
        for (std::vector<std::pair<int,int>>::const_iterator it =
                 bucket.begin(); it != bucket.end(); ++it) {
            if (it->first < 0 || it->first >= n ||
                it->second < 0 || it->second >= n) {
                continue;
            }
            int compactFrom = ensureCompactVertex(it->first);
            int compactTo = ensureCompactVertex(it->second);
            initialPairs.push_back(std::make_pair(compactFrom, compactTo));
            QuotientEdge edge;
            edge.from = compactFrom;
            edge.to = compactTo;
            edge.original = encodeEdge(it->first, it->second, time);
            initialEdges.push_back(edge);
        }
    }

    int compactNodeCount = int(compactToOriginal.size());
    SccResult initialScc = computeScc(compactNodeCount, initialPairs);
    std::set<ExperimentalEdge> selected =
        selectCondensationTreeEdges(compactNodeCount, initialEdges, initialScc);
    result.insert(selected.begin(), selected.end());

    std::vector<int> nodeToSuper(n, -1);
    for (int compact = 0; compact < compactNodeCount; ++compact) {
        int original = compactToOriginal[compact];
        if (original >= 0 && original < n &&
            compact < int(initialScc.component.size())) {
            nodeToSuper[original] = initialScc.component[compact];
        }
    }
    int superCount = int(initialScc.groups.size());
    std::vector<QuotientEdge> superEdges;
    for (std::vector<QuotientEdge>::const_iterator it =
             initialEdges.begin(); it != initialEdges.end(); ++it) {
        int originalFrom = edgeSource(it->original);
        int originalTo = edgeDestination(it->original);
        int from = (originalFrom >= 0 && originalFrom < n)
            ? nodeToSuper[originalFrom] : -1;
        int to = (originalTo >= 0 && originalTo < n)
            ? nodeToSuper[originalTo] : -1;
        if (from < 0 || to < 0) {
            continue;
        }
        if (from == to) {
            if (pruningStats != nullptr) {
                if (selected.count(it->original)) {
                    ++pruningStats->initialInternalResSelected;
                }
                else {
                    ++pruningStats->initialInternalNonResPruned;
                    pruningStats->uniqueInitialInternalNonRes.insert(
                        it->original);
                }
            }
            if (!enableIntraSccPruning) {
                QuotientEdge edge = *it;
                edge.from = from;
                edge.to = to;
                superEdges.push_back(edge);
            }
            continue;
        }
        QuotientEdge edge = *it;
        edge.from = from;
        edge.to = to;
        superEdges.push_back(edge);
    }

    std::vector<std::vector<int>> superAdjacency;
    std::unordered_set<long long> superEdgeExists;
    rebuildSuperAdjacency(
        superCount, superEdges, superAdjacency, superEdgeExists);

    auto ensureSuperForOriginal = [&](int vertex) -> int {
        if (vertex < 0 || vertex >= n) {
            return -1;
        }
        int super = nodeToSuper[vertex];
        if (super >= 0) {
            return super;
        }
        super = superCount++;
        nodeToSuper[vertex] = super;
        if (int(superAdjacency.size()) < superCount) {
            superAdjacency.resize(superCount);
        }
        return super;
    };

    for (int start = right - 1; start >= left; --start) {
        const std::vector<std::pair<int,int>>& bucket =
            activeEdges[start];
        bool inserted = false;
        for (std::vector<std::pair<int,int>>::const_iterator it =
                 bucket.begin(); it != bucket.end(); ++it) {
            if (it->first < 0 || it->first >= n ||
                it->second < 0 || it->second >= n) {
                continue;
            }
            int from = ensureSuperForOriginal(it->first);
            int to = ensureSuperForOriginal(it->second);
            if (from < 0 || from >= superCount ||
                to < 0 || to >= superCount) {
                continue;
            }
            if (from == to) {
                if (pruningStats != nullptr) {
                    ++pruningStats->arrivalAlreadyInternal;
                }
                continue;
            }
            long long superKey = encodeSuperPair(from, to);
            if (superEdgeExists.find(superKey) != superEdgeExists.end()) {
                if (pruningStats != nullptr) {
                    ++pruningStats->duplicateSuperEdgeSkipped;
                }
                continue;
            }
            if (superReachable(superCount, superAdjacency, from, to)) {
                if (pruningStats != nullptr) {
                    ++pruningStats->reachabilityRedundantSkipped;
                }
                continue;
            }
            QuotientEdge edge;
            edge.from = from;
            edge.to = to;
            edge.original = encodeEdge(it->first, it->second, start);
            superEdges.push_back(edge);
            superEdgeExists.insert(superKey);
            superAdjacency[from].push_back(to);
            inserted = true;
        }

        if (!inserted) {
            continue;
        }

        std::vector<std::pair<int,int>> superPairs;
        superPairs.reserve(superEdges.size());
        for (std::vector<QuotientEdge>::const_iterator it =
                 superEdges.begin(); it != superEdges.end(); ++it) {
            superPairs.push_back(std::make_pair(it->from, it->to));
        }

        SccResult superScc = computeScc(superCount, superPairs);
        std::set<ExperimentalEdge> additions =
            selectCondensationTreeEdges(superCount, superEdges, superScc);
        result.insert(additions.begin(), additions.end());

        if (int(superScc.groups.size()) == superCount) {
            continue;
        }

        std::vector<QuotientEdge> contractedEdges;
        contractedEdges.reserve(superEdges.size());
        for (std::vector<QuotientEdge>::const_iterator it =
                 superEdges.begin(); it != superEdges.end(); ++it) {
            int from = superScc.component[it->from];
            int to = superScc.component[it->to];
            if (from == to) {
                if (pruningStats != nullptr) {
                    if (additions.count(it->original)) {
                        ++pruningStats->shrinkInternalResSelected;
                    }
                    else {
                        ++pruningStats->shrinkInternalNonResPruned;
                        pruningStats->uniqueShrinkInternalNonRes.insert(
                            it->original);
                    }
                }
                if (!enableIntraSccPruning) {
                    QuotientEdge edge = *it;
                    edge.from = from;
                    edge.to = to;
                    contractedEdges.push_back(edge);
                }
                continue;
            }
            QuotientEdge edge = *it;
            edge.from = from;
            edge.to = to;
            contractedEdges.push_back(edge);
        }
        for (std::vector<int>::iterator it = nodeToSuper.begin();
             it != nodeToSuper.end(); ++it) {
            if (*it >= 0 && *it < superCount) {
                *it = superScc.component[*it];
            }
        }
        superEdges.swap(contractedEdges);
        superCount = int(superScc.groups.size());
        rebuildSuperAdjacency(
            superCount, superEdges, superAdjacency, superEdgeExists);
    }

    return result;
}

}

int OptimizedIndex::find(int u) {
    int root = u;
    while (f[root] != root) {
        root = f[root];
    }
    while (f[u] != u) {
        int parent = f[u];
        f[u] = root;
        u = parent;
    }
    return root;
}


void OptimizedIndex::kosaraju1(int now) {
    if (Vis[now] == 1) {
        return;
    }

    std::vector<std::pair<int, std::size_t>> stack;
    stack.push_back(std::make_pair(now, std::size_t(0)));
    Vis[now] = 1;
    markedVertices.push_back(now);

    while (!stack.empty()) {
        int u = stack.back().first;
        std::size_t& next = stack.back().second;

        if (next < outLabel[u].size()) {
            EncodedEdge g = outLabel[u][next++];
            int v = find(int((g.first >> 12) & 33554431ll));
            if (Vis[v] == 1) {
                continue;
            }
            Vis[v] = 1;
            markedVertices.push_back(v);
            stack.push_back(std::make_pair(v, std::size_t(0)));
        }
        else {
            Sta[++top] = u;
            stack.pop_back();
        }
    }
}


void OptimizedIndex::kosaraju5(int now) {
    if (Vis[now] == 1) {
        return;
    }

    std::vector<std::pair<int, std::size_t>> stack;
    stack.push_back(std::make_pair(now, std::size_t(0)));
    Vis[now] = 1;
    markedVertices.push_back(now);

    while (!stack.empty()) {
        int u = stack.back().first;
        std::size_t& next = stack.back().second;

        if (next < outLabel[u].size()) {
            EncodedEdge g = outLabel[u][next++];
            int v = int((g.first >> 12) & 33554431ll);
            if (Vis[v] == 1) {
                continue;
            }
            Vis[v] = 1;
            markedVertices.push_back(v);
            stack.push_back(std::make_pair(v, std::size_t(0)));
        }
        else {
            Sta[++top] = u;
            stack.pop_back();
        }
    }
}

void OptimizedIndex::kosaraju3(int now) {
    if (Vis[now] == 1) {
        return;
    }

    std::vector<int> stack;
    stack.push_back(now);
    Vis[now] = 1;
    markedVertices.push_back(now);

    while (!stack.empty()) {
        int u = stack.back();
        stack.pop_back();
        CC.push_back(u);

        for (std::vector<EncodedEdge>::reverse_iterator it = outLabel2[u].rbegin();
             it != outLabel2[u].rend(); ++it) {
            int v = int(it->first >> 37);
            if (Vis[v] == 1) {
                continue;
            }
            Vis[v] = 1;
            markedVertices.push_back(v);
            stack.push_back(v);
        }
    }
}

void OptimizedIndex::kosaraju2(int now,int ts){
    if (Vis[now] != 0) {
        return;
    }

    std::vector<std::pair<int, std::size_t>> stack;
    stack.push_back(std::make_pair(now, std::size_t(0)));
    Vis[now] = col;
    markedVertices.push_back(now);

    while (!stack.empty()) {
        int u = stack.back().first;
        std::size_t& next = stack.back().second;

        if (next < outLabel2[u].size()) {
            EncodedEdge g = outLabel2[u][next++];
            int v = find(int(g.first >> 37));
            if (Vis[v] == 0) {
                key.insert(g);
                Vis[v] = col;
                markedVertices.push_back(v);
                stack.push_back(std::make_pair(v, std::size_t(0)));
            }
        }
        else {
            stack.pop_back();
        }
    }
}

void OptimizedIndex::kosaraju4(int now, int ori, int ts){
    if (Vis2[now]) {
        return;
    }

    std::vector<std::pair<int, std::size_t>> stack;
    stack.push_back(std::make_pair(now, std::size_t(0)));
    Vis2[now] = 1;
    markedVertices2.push_back(now);
    f[now] = ori;
    CC.push_back(now);

    while (!stack.empty()) {
        int u = stack.back().first;
        std::size_t& next = stack.back().second;

        if (next < outLabel[u].size()) {
            EncodedEdge g = outLabel[u][next++];
            int v = find(int((g.first >> 12) & 33554431ll));
            if (Vis2[v]) {
                continue;
            }
            if (Vis[v] == Vis[u]) {
                key.insert(g);
                Vis2[v] = 1;
                markedVertices2.push_back(v);
                f[v] = ori;
                CC.push_back(v);
                stack.push_back(std::make_pair(v, std::size_t(0)));
            }
        }
        else {
            stack.pop_back();
        }
    }
}

int OptimizedIndex::find_an_index(int t, int ts, int te) {

    int l = 0;
    int r = actual_time[t].size() - 1;

    if (r == -1 || actual_time[t][r] < ts || actual_time[t][0] > te) {
        return -1;
    }
    
    while (l < r) {
        int mid = l + r >> 1;
        if (actual_time[t][mid] >= ts && actual_time[t][mid] <= te) {
            return mid;
        }
        else {
            if (actual_time[t][mid] < ts) {
                l = mid + 1;
            }
            else {
                r = mid;
            }
        }
    }

    if (actual_time[t][l] >= ts && actual_time[t][l] <= te) {
        return l;
    }
    else {
        return -1;
    }

}

std::stringstream OptimizedIndex::solve(int n, int ts, int te) {
    if (reverseIndex) {
        return solveReverse(n, ts, te);
    }
    
    std::stringstream Ans;
    std::vector<int> *CurrentCC = new std::vector<int>[n]();
    markedVertices.clear();
    markedVertices2.clear();
    
    Ans << "The spanned strongly connected components in [" << ts << ", " << te << "] are:\n";

    top = 0;
    
    for (int u = 0; u < n; u++) {
        Vis[u] = 0;
        outLabel[u].clear();
        outLabel2[u].clear();
    }

    if (ts < 0) {
        ts = 0;
    }
    if (te > tmax) {
        te = tmax;
    }
    if (ts > te) {
        delete [] CurrentCC;
        return Ans;
    }

    auto scanPoint = [&](int left, int right) {
        left = std::max(left, 0);
        right = std::min(right, tmax);
        for (int i = left; i <= right; ++i) {
            for (int now = 0; now < int(G[i].size()); ++now) {
                if (G[i][now].ts > ts) {
                    break;
                }
                std::pair<long long,int> g = G[i][now].edge;
                if (g.second < ts || g.second > te) {
                    continue;
                }
                int u = int(g.first >> 37);
                int v = int((g.first >> 12) & 33554431ll);
                outLabel[u].push_back(g);
                outLabel2[v].push_back(g);
            }
        }
    };

    int leftBlock = ts / len;
    int rightBlock = te / len;
    if (leftBlock == rightBlock) {
        scanPoint(ts, te);
    }
    else {
        scanPoint(ts, (leftBlock + 1) * len - 1);
        for (int block = leftBlock + 1; block < rightBlock; ++block) {
            for (int now = 0; now < int(Chunk[block].size()); ++now) {
                if (Chunk[block][now].ts > ts) {
                    break;
                }
                std::pair<long long,int> g = Chunk[block][now].edge;
                if (g.second < ts || g.second > te) {
                    continue;
                }
                int u = int(g.first >> 37);
                int v = int((g.first >> 12) & 33554431ll);
                outLabel[u].push_back(g);
                outLabel2[v].push_back(g);
            }
        }
        scanPoint(rightBlock * len, te);
    }
    // for(int i=ts;i<=te;i++){
    //     for(int now=0;now<G[i].size();now++){
    //         if(G[i][now].ts<=ts){
    //             std::pair<long long,int> g=G[i][now].edge;
    //             long long u = (g.first >> 37), v = (g.first >> 12) & (33554431ll);
    //             if(g.second>te) continue;
    //             outLabel[u].push_back(g);
    //             outLabel2[v].push_back(g);
    //         }
    //         else break;
    //     }
    // }
    for(int u=0;u<n;u++){
        if(!Vis[u]){
            kosaraju5(u);
        }
    }
    for(int u=0;u<n;u++)Vis[u]=0;
    while(top){
        int t=0;
        int u=Sta[top];top--;
        if(Vis[u])continue;
        CC.clear();
        kosaraju3(u);
        std::sort(CC.begin(),CC.end());
        CurrentCC[CC[0]]=CC;
    }
    for (int u = 0; u < n; ++u) {
        if (CurrentCC[u].size() == 0) {
            continue;
        }
        std::vector<int> printedComponent;
        printedComponent.reserve(CurrentCC[u].size());
        for (std::vector<int>::iterator it = CurrentCC[u].begin();
             it != CurrentCC[u].end(); ++it) {
            int id = *it;
            if (!originalVertexIds.empty() && id >= 0 &&
                id < int(originalVertexIds.size())) {
                id = originalVertexIds[id];
            }
            printedComponent.push_back(id);
        }
        std::sort(printedComponent.begin(), printedComponent.end());
        Ans << "{ ";
        for (std::vector<int>::iterator it = printedComponent.begin();
             it != printedComponent.end(); ++it) {
            Ans << *it << " ";
        }
        Ans << "}\n";
    }
    delete [] CurrentCC;
    return Ans;

}

bool OptimizedIndex::cmp(RES a, RES b){
    return a.ts<b.ts;
}

bool OptimizedIndex::cmpReverse(RES a, RES b){
    return a.ts>b.ts;
}

std::stringstream OptimizedIndex::solveReverse(int n, int ts, int te) {
    std::stringstream Ans;
    std::vector<int> *CurrentCC = new std::vector<int>[n]();
    markedVertices.clear();
    markedVertices2.clear();

    Ans << "The spanned strongly connected components in [" << ts << ", " << te << "] are:\n";

    top = 0;
    for (int u = 0; u < n; u++) {
        Vis[u] = 0;
        outLabel[u].clear();
        outLabel2[u].clear();
    }

    // In the reverse index, G[L] stores edges whose end-time appearing
    // interval is [L, R], and RES.ts stores R.  A query [ts, te] needs
    // L <= te <= R and the original edge timestamp >= ts.
    auto scanPoint = [&](int left, int right) {
        left = std::max(left, 0);
        right = std::min(right, tmax);
        for (int i = left; i <= right; ++i) {
            for (int now = 0; now < (int)G[i].size(); ++now) {
                if (G[i][now].ts < te) {
                    break;
                }
                std::pair<long long,int> g = G[i][now].edge;
                if (g.second < ts || g.second > te) {
                    continue;
                }
                long long u = g.first >> 37;
                long long v = (g.first >> 12) & (33554431ll);
                outLabel[u].push_back(g);
                outLabel2[v].push_back(g);
            }
        }
    };

    int leftBlock = ts / len;
    int rightBlock = te / len;
    if (leftBlock == rightBlock) {
        scanPoint(ts, te);
    }
    else {
        scanPoint(ts, (leftBlock + 1) * len - 1);

        for (int block = leftBlock + 1; block < rightBlock; ++block) {
            for (int now = 0; now < (int)Chunk[block].size(); ++now) {
                if (Chunk[block][now].ts < te) {
                    break;
                }
                std::pair<long long,int> g = Chunk[block][now].edge;
                if (g.second < ts || g.second > te) {
                    continue;
                }
                long long u = g.first >> 37;
                long long v = (g.first >> 12) & (33554431ll);
                outLabel[u].push_back(g);
                outLabel2[v].push_back(g);
            }
        }

        scanPoint(rightBlock * len, te);
    }

    for (int u = 0; u < n; u++) {
        if (!Vis[u]) {
            kosaraju5(u);
        }
    }
    for (int u = 0; u < n; u++) {
        Vis[u] = 0;
    }
    while (top) {
        int u = Sta[top--];
        if (Vis[u]) {
            continue;
        }
        CC.clear();
        kosaraju3(u);
        std::sort(CC.begin(), CC.end());
        CurrentCC[CC[0]] = CC;
    }

    for (int u = 0; u < n; ++u) {
        if (CurrentCC[u].empty()) {
            continue;
        }
        std::vector<int> printedComponent;
        printedComponent.reserve(CurrentCC[u].size());
        for (std::vector<int>::iterator it = CurrentCC[u].begin();
             it != CurrentCC[u].end(); ++it) {
            int id = *it;
            if (!originalVertexIds.empty() && id >= 0 &&
                id < int(originalVertexIds.size())) {
                id = originalVertexIds[id];
            }
            printedComponent.push_back(id);
        }
        std::sort(printedComponent.begin(), printedComponent.end());
        Ans << "{ ";
        for (std::vector<int>::iterator it = printedComponent.begin();
             it != printedComponent.end(); ++it) {
            Ans << *it << " ";
        }
        Ans << "}\n";
    }

    delete [] CurrentCC;
    return Ans;
}

OptimizedIndex::OptimizedIndex(TemporalGraph * Graph, double t_fraction) {
    
    unsigned long long start_time = currentTime();

    n = Graph->numOfVertices();
    m = Graph->numOfEdges();
    tmax = Graph->tmax;
    originalVertexIds = Graph->original_vertex_ids;
    len=std::max(1, int(std::sqrt(double(std::max(1, tmax)))));
    Sta = new int[n + 1];
    Vis = new int[n];
    Vis2 = new int[n];
    f = new int[n];
    edge = new std::vector<std::pair<long long,int>> [tmax+1]();
    newedge = new std::vector<std::pair<long long,int>> [tmax+1]();
    G = new std::vector<RES> [tmax+1]();
    Chunk = new std::vector<RES> [tmax+1]();
    actual_time= new std::vector<int> [tmax+1]();
    outLabel = new std::vector<std::pair<long long,int>>[n]();
    outLabel2 = new std::vector<std::pair<long long,int>>[n]();
    top=0;
    S.clear();
    t1=tmax*t_fraction;
    for(int t=0;t<=t1;t++){
        std::vector<std::pair<int, int>>::iterator iter;
            for (iter = Graph->temporal_edge[t].begin(); iter != Graph->temporal_edge[t].end(); iter++) {
                int u=iter->first,v=iter->second;
                std::pair<long long,int> g=std::pair<long long,int>((((long long)iter->first)<<37)+(((long long)iter->second)<<12),t);
                edge[t].push_back(g);
            }
    }
    std::set<std::pair<long long,int>> table;
    std::size_t nextConstructedResReport = 1000;
    for (int ts = 0; ts <= t1; ++ts) {
        //std::cerr<<ts<<'\n';
        for(int u=0;u<n;u++){
            outLabel[u].clear();
            outLabel2[u].clear();
            f[u]=u;
            Vis[u]=0;
            Vis2[u]=0;
        }
        for(int i=0;i<=t1;i++)newedge[i].clear();
        for(auto g:key){
            newedge[g.second].push_back(g);
        }
        table=key;
        key.clear();
        for(int t=ts;t<=t1;t++){
            //  std::cerr<<ts<<' '<<t<<'\n';
            std::vector<int> point;
            point.clear();
            std::vector<std::pair<long long,int>>::iterator it;
            tmpedge.clear();
            if(!newedge[t].empty())
            for(it = newedge[t].begin();it!=newedge[t].end();it++){
                std::pair<long long,int> g=*it;
                int u=find(g.first>>37),v=find((g.first>>12)&(33554431ll)),tim=g.second;
                if(u==v){tmpedge.push_back(g);continue;}
                if(tim<ts)continue;
                point.push_back(u);
                point.push_back(v);
                outLabel[u].push_back(g);
                outLabel2[v].push_back(g);
            }
            if(!edge[t].empty())
            for (it = edge[t].begin(); it != edge[t].end(); it++) {
                std::pair<long long,int> g=*it;
                if(table.find(g)!=table.end())continue;
                int u=find(g.first>>37),v=find((g.first>>12)&(33554431ll)),tim=g.second;
                if(u==v){tmpedge.push_back(g);continue;}
                if(tim<ts)continue;
                point.push_back(u);
                point.push_back(v);
                outLabel[u].push_back(g);
                outLabel2[v].push_back(g);
            }
            //only vertex connected to edges needs to be considered in running SCC
            sort(point.begin(),point.end());
            std::vector<int>:: iterator pos=std::unique(point.begin(),point.end());
            point.erase(pos,point.end());
            // int reaf=0;
            // update the edges
            edge[t]=tmpedge;

            //run the scc
            //std::cerr<<"Run the scc.\n";
            if(!markedVertices2.empty())
            for(auto u:markedVertices2){
                Vis2[u]=0;
            }
            if(!markedVertices.empty())
            for (auto u:markedVertices){
                Vis[u] = 0;
            }
            markedVertices2.clear();
            markedVertices.clear();
            top=0;
            if(!point.empty())
            for(auto g:point){
                if(!Vis[g]){
                    kosaraju1(g);
                }
            }
            if(!markedVertices.empty())
            for(auto u:markedVertices){
                Vis[u]=0;
            }
            markedVertices.clear();
            col=0;
            //if(ts==132)std::cerr<<"Go through it.\n"<<top<<'\n';
            while(top){
                //if(ts==132)std::cerr<<top<<'\n';
                int u=Sta[top];top--;
                //if(ts==132)std::cerr<<u<<' '<<top<<'\n';
                int g=find(u);
                if(Vis2[g])continue;
                col++;
                CC.clear();
                kosaraju2(g,ts);
                kosaraju4(g,g,ts);
                std::vector<std::pair<long long,int>> tmp;
                tmp.clear();
                for(auto u:CC){
                    std::vector<std::pair<long long,int>>::iterator iter;
                    for(iter=outLabel2[u].begin();iter!=outLabel2[u].end();iter++){
                        long long v=(*iter).first>>37;
                        if(find(v)!=g){
                            tmp.push_back(*iter);
                        }
                        else{
                            edge[t].push_back(*iter);
                        }
                    }
                }
                for(auto u:CC){
                    outLabel2[u].clear();
                    std::vector<std::pair<long long,int>>().swap(outLabel2[u]);
                }
                outLabel2[g]=tmp;
                tmp.clear();
                for(auto u:CC){
                    std::vector<std::pair<long long,int>>::iterator iter;
                    for(iter=outLabel[u].begin();iter!=outLabel[u].end();iter++){
                        long long v=((*iter).first>>12)&(33554431ll);
                        if(find(v)!=g){
                            tmp.push_back(*iter);
                        }
                    }
                }
                for(auto u:CC){
                    outLabel[u].clear();
                    std::vector<std::pair<long long,int>>().swap(outLabel[u]);
                }
                outLabel[g]=tmp;
                tmp.clear();
            }
            
        }
        
        //update the RES-index
        tmper=key;
        if(!key.empty())
        for(auto e: key){
            if(S.count(e)){
                S[e].second=ts;
            }
            else{
                S[e]=std::pair<int,int>(ts,ts);
            }
        }
        if (S.size() >= nextConstructedResReport) {
            while (nextConstructedResReport <= S.size()) {
                nextConstructedResReport += 1000;
            }
            std::cout
                << "[RES constructor] Constructed RES index entries: "
                << S.size() << " / input edges " << m
                << "; completed start anchors: " << (ts + 1)
                << " / " << (t1 + 1)
                << "; elapsed = "
                << timeFormatting(currentTime() - start_time).str()
                << "." << std::endl;
        }
       if(ts%100 == 0)
        putProcess(double(ts+1) / (t1+1), currentTime() - start_time);
    }
    std::cout
        << "[RES constructor] Internal RES batch construction finished. "
        << "Constructed RES index entries: " << S.size()
        << " / input edges " << m
        << "; processed start anchors: " << (t1 + 1)
        << " / " << (t1 + 1)
        << "; elapsed = "
        << timeFormatting(currentTime() - start_time).str()
        << "." << std::endl;
    //std::cerr<<"now?\n";
    for(auto e:S){
        int ts=e.second.first;
        int te=e.second.second;
        G[te].push_back(RES(e.first,ts));
        Chunk[te/len].push_back(RES(e.first,ts));
    }
    for(int t=0;t<=t1;t++){
        sort(G[t].begin(),G[t].end(),cmp);
    }
    for(int t=0;t<=t1/len;t++){
        sort(Chunk[t].begin(),Chunk[t].end(),cmp);
    }
    initializeAppearanceIntervals();
    //std::cerr<<"Here?\n";
        // for(int lt=0;lt<=t1;lt++){
        //     int len=actual_time[lt].size();
        //     for(int i=len-1;i>=0;i--){
        //         if(actual_time[lt][i]<t1)break;
        //         if(actual_time[lt][i]==t1){
                    
        //             alfa.clear();
        //             std::set<std::pair<long long,int>>::iterator iter;
        //             for(iter=S[lt][i].begin();iter!=S[lt][i].end();iter++){
        //                 alfa.push_back(*iter);
        //             }
        //             G[lt].push_back(alfa);
        //         }
        //     }
        // }
    delete [] edge;
    edge = nullptr;
    //delete [] S;
}

OptimizedIndex * OptimizedIndex::buildReverse(TemporalGraph * Graph, double t_fraction) {
    if (Graph == nullptr) {
        return nullptr;
    }

    unsigned long long start_time = currentTime();
    OptimizedIndex *Index = new OptimizedIndex();

    const int originalN = Graph->numOfVertices();
    // Sta is used as a 1-based stack by the original traversal routines.
    // Keep one private spare slot without changing the original constructor.
    const int workingN = originalN + 1;

    Index->n = workingN;
    Index->m = Graph->numOfEdges();
    Index->originalVertexIds = Graph->original_vertex_ids;
    Index->tmax = Graph->tmax;
    Index->t1 = int(Index->tmax * t_fraction);
    Index->len = std::max(1, int(std::sqrt(double(Index->tmax))));

    Index->Sta = new int[workingN];
    Index->Vis = new int[workingN];
    Index->Vis2 = new int[workingN];
    Index->f = new int[workingN];
    Index->edge = new std::vector<std::pair<long long,int>>[Index->tmax + 1]();
    Index->newedge = new std::vector<std::pair<long long,int>>[Index->tmax + 1]();
    Index->G = new std::vector<RES>[Index->tmax + 1]();
    Index->Chunk = new std::vector<RES>[Index->tmax + 1]();
    Index->actual_time = new std::vector<int>[Index->tmax + 1]();
    Index->outLabel = new std::vector<std::pair<long long,int>>[workingN]();
    Index->outLabel2 = new std::vector<std::pair<long long,int>>[workingN]();

    Index->top = 0;
    Index->key.clear();
    Index->S.clear();
    Index->reverseIndex = true;

    // Full reverse construction stores end-time intervals.  The lower bound
    // controls which end anchors are materialized; every materialized anchor
    // still represents the original window [0, te].
    const int lowerTime = Index->tmax - Index->t1;

    std::vector<int> activeVertices;
    std::vector<unsigned char> isActiveVertex(workingN, 0);
    for (int t = 0; t <= Index->tmax; ++t) {
        if (t < (int)Graph->temporal_edge.size()) {
            for (std::vector<std::pair<int,int>>::iterator iter =
                     Graph->temporal_edge[t].begin();
                 iter != Graph->temporal_edge[t].end(); ++iter) {
                if (!isActiveVertex[iter->first]) {
                    isActiveVertex[iter->first] = 1;
                    activeVertices.push_back(iter->first);
                }
                if (!isActiveVertex[iter->second]) {
                    isActiveVertex[iter->second] = 1;
                    activeVertices.push_back(iter->second);
                }
                long long encoded =
                    (((long long)iter->first) << 37) +
                    (((long long)iter->second) << 12);
                Index->edge[t].push_back(std::make_pair(encoded, t));
            }
        }
    }
    std::vector<unsigned char>().swap(isActiveVertex);

    const int totalEndAnchors = Index->tmax - lowerTime + 1;
    std::size_t nextReverseConstructedResReport = 1000;
    const bool reverseProfile =
        (std::getenv("RES_REVERSE_PROFILE") != nullptr);
    unsigned long long profileResetMicros = 0;
    unsigned long long profileNewedgeMicros = 0;
    unsigned long long profileScanMicros = 0;
    unsigned long long profileCandidateMicros = 0;
    unsigned long long profileSortMicros = 0;
    unsigned long long profileSccMicros = 0;
    unsigned long long profileSaveMicros = 0;
    unsigned long long profileMaterializeMicros = 0;
    unsigned long long profileInnerSteps = 0;
    unsigned long long profileNewedgeItems = 0;
    unsigned long long profileRawItems = 0;
    unsigned long long profileCandidateChecks = 0;
    unsigned long long profilePointTotal = 0;
    unsigned long long profileMaxPoint = 0;
    unsigned long long profileSkippedInnerSteps = 0;
    std::cout
        << "[Reverse RES constructor] Strict end-time construction starts. "
        << "End anchors: " << totalEndAnchors
        << ", fixed minimum start time = 0."
        << std::endl;

    std::unordered_set<EncodedEdge, ExperimentalEdgeHash> table;
    table.max_load_factor(0.75f);
    std::vector<int> labelCandidateEpoch(workingN, 0);
    int currentCandidateEpoch = 0;
    std::vector<int> touchedNewedgeTimes;
    int completedEndAnchors = 0;
    int skippedEmptyEndAnchors = 0;
    for (int te = Index->tmax; te >= lowerTime; --te) {
        unsigned long long profileStepStart =
            reverseProfile ? currentTime() : 0;
        std::vector<int> labelCandidates;
        ++currentCandidateEpoch;
        if (currentCandidateEpoch == std::numeric_limits<int>::max()) {
            std::fill(
                labelCandidateEpoch.begin(),
                labelCandidateEpoch.end(),
                0);
            currentCandidateEpoch = 1;
        }

        for (std::vector<int>::const_iterator active =
                 activeVertices.begin();
             active != activeVertices.end(); ++active) {
            int u = *active;
            Index->outLabel[u].clear();
            Index->outLabel2[u].clear();
            Index->f[u] = u;
            Index->Vis[u] = 0;
            Index->Vis2[u] = 0;
        }
        if (reverseProfile) {
            profileResetMicros += currentTime() - profileStepStart;
            profileStepStart = currentTime();
        }

        for (std::vector<int>::const_iterator touched =
                 touchedNewedgeTimes.begin();
             touched != touchedNewedgeTimes.end(); ++touched) {
            Index->newedge[*touched].clear();
        }
        touchedNewedgeTimes.clear();
        table.clear();
        table.reserve(Index->key.size());
        for (std::set<EncodedEdge>::iterator it =
                 Index->key.begin(); it != Index->key.end(); ++it) {
            if (it->second >= 0 && it->second <= Index->tmax) {
                if (Index->newedge[it->second].empty()) {
                    touchedNewedgeTimes.push_back(it->second);
                }
                Index->newedge[it->second].push_back(*it);
            }
            table.insert(*it);
        }
        Index->key.clear();
        if (reverseProfile) {
            profileNewedgeMicros += currentTime() - profileStepStart;
        }

        std::vector<int> point;
        std::vector<int> nextLabelCandidates;
        for (int t = te; t >= 0; --t) {
            if (reverseProfile) {
                ++profileInnerSteps;
            }
            if (Index->newedge[t].empty() && Index->edge[t].empty()) {
                if (reverseProfile) {
                    ++profileSkippedInnerSteps;
                }
                continue;
            }

            bool hasNewCrossingEdge = false;
            point.clear();
            Index->tmpedge.clear();

            profileStepStart = reverseProfile ? currentTime() : 0;
            for (std::vector<EncodedEdge>::iterator it =
                     Index->newedge[t].begin();
                 it != Index->newedge[t].end(); ++it) {
                if (reverseProfile) {
                    ++profileNewedgeItems;
                }
                EncodedEdge g = *it;
                int u = Index->find(int(g.first >> 37));
                int v = Index->find(int((g.first >> 12) & 33554431ll));
                int edgeTime = g.second;

                if (u == v) {
                    Index->tmpedge.push_back(g);
                    continue;
                }
                if (edgeTime > te) {
                    continue;
                }
                point.push_back(u);
                point.push_back(v);
                hasNewCrossingEdge = true;
                Index->outLabel[u].push_back(g);
                Index->outLabel2[v].push_back(g);
                if (labelCandidateEpoch[u] != currentCandidateEpoch) {
                    labelCandidateEpoch[u] = currentCandidateEpoch;
                    labelCandidates.push_back(u);
                }
                if (labelCandidateEpoch[v] != currentCandidateEpoch) {
                    labelCandidateEpoch[v] = currentCandidateEpoch;
                    labelCandidates.push_back(v);
                }
            }

            for (std::vector<EncodedEdge>::iterator it =
                     Index->edge[t].begin();
                 it != Index->edge[t].end(); ++it) {
                if (reverseProfile) {
                    ++profileRawItems;
                }
                EncodedEdge g = *it;
                if (table.find(g) != table.end()) {
                    continue;
                }

                int u = Index->find(int(g.first >> 37));
                int v = Index->find(int((g.first >> 12) & 33554431ll));
                int edgeTime = g.second;

                if (u == v) {
                    Index->tmpedge.push_back(g);
                    continue;
                }
                if (edgeTime > te) {
                    continue;
                }
                point.push_back(u);
                point.push_back(v);
                hasNewCrossingEdge = true;
                Index->outLabel[u].push_back(g);
                Index->outLabel2[v].push_back(g);
                if (labelCandidateEpoch[u] != currentCandidateEpoch) {
                    labelCandidateEpoch[u] = currentCandidateEpoch;
                    labelCandidates.push_back(u);
                }
                if (labelCandidateEpoch[v] != currentCandidateEpoch) {
                    labelCandidateEpoch[v] = currentCandidateEpoch;
                    labelCandidates.push_back(v);
                }
            }
            if (reverseProfile) {
                profileScanMicros += currentTime() - profileStepStart;
                profileStepStart = currentTime();
            }

            if (!hasNewCrossingEdge) {
                Index->edge[t].swap(Index->tmpedge);
                if (reverseProfile) {
                    ++profileSkippedInnerSteps;
                }
                continue;
            }

            nextLabelCandidates.clear();
            nextLabelCandidates.reserve(labelCandidates.size());
            for (std::vector<int>::iterator candidate =
                     labelCandidates.begin();
                 candidate != labelCandidates.end(); ++candidate) {
                if (reverseProfile) {
                    ++profileCandidateChecks;
                }
                int vertex = *candidate;
                if (!Index->outLabel[vertex].empty() ||
                    !Index->outLabel2[vertex].empty()) {
                    point.push_back(vertex);
                    nextLabelCandidates.push_back(vertex);
                }
                else {
                    labelCandidateEpoch[vertex] = 0;
                }
            }
            labelCandidates.swap(nextLabelCandidates);
            if (reverseProfile) {
                profileCandidateMicros += currentTime() - profileStepStart;
                profileStepStart = currentTime();
            }

            std::sort(point.begin(), point.end());
            point.erase(std::unique(point.begin(), point.end()), point.end());
            Index->edge[t].swap(Index->tmpedge);
            if (reverseProfile) {
                profileSortMicros += currentTime() - profileStepStart;
                profilePointTotal += point.size();
                if (point.size() > profileMaxPoint) {
                    profileMaxPoint = point.size();
                }
                profileStepStart = currentTime();
            }

            for (std::vector<int>::iterator it =
                     Index->markedVertices2.begin();
                 it != Index->markedVertices2.end(); ++it) {
                Index->Vis2[*it] = 0;
            }
            for (std::vector<int>::iterator it =
                     Index->markedVertices.begin();
                 it != Index->markedVertices.end(); ++it) {
                Index->Vis[*it] = 0;
            }
            Index->markedVertices2.clear();
            Index->markedVertices.clear();
            Index->top = 0;

            for (std::vector<int>::iterator it = point.begin();
                 it != point.end(); ++it) {
                if (!Index->Vis[*it]) {
                    Index->kosaraju1(*it);
                }
            }

            for (std::vector<int>::iterator it =
                     Index->markedVertices.begin();
                 it != Index->markedVertices.end(); ++it) {
                Index->Vis[*it] = 0;
            }
            Index->markedVertices.clear();
            Index->col = 0;

            while (Index->top) {
                int u = Index->Sta[Index->top--];
                int representative = Index->find(u);
                if (Index->Vis2[representative]) {
                    continue;
                }

                ++Index->col;
                Index->CC.clear();
                Index->kosaraju2(representative, te);
                Index->kosaraju4(representative, representative, te);

                std::vector<EncodedEdge> remaining;
                for (std::vector<int>::iterator ccIt = Index->CC.begin();
                     ccIt != Index->CC.end(); ++ccIt) {
                    int member = *ccIt;
                    for (std::vector<EncodedEdge>::iterator edgeIt =
                             Index->outLabel2[member].begin();
                         edgeIt != Index->outLabel2[member].end(); ++edgeIt) {
                        long long source = edgeIt->first >> 37;
                        if (Index->find(int(source)) != representative) {
                            remaining.push_back(*edgeIt);
                        }
                        else {
                            Index->edge[t].push_back(*edgeIt);
                        }
                    }
                }
                for (std::vector<int>::iterator ccIt = Index->CC.begin();
                     ccIt != Index->CC.end(); ++ccIt) {
                    Index->outLabel2[*ccIt].clear();
                }
                Index->outLabel2[representative].swap(remaining);

                remaining.clear();
                for (std::vector<int>::iterator ccIt = Index->CC.begin();
                     ccIt != Index->CC.end(); ++ccIt) {
                    int member = *ccIt;
                    for (std::vector<EncodedEdge>::iterator edgeIt =
                             Index->outLabel[member].begin();
                         edgeIt != Index->outLabel[member].end(); ++edgeIt) {
                        long long destination =
                            (edgeIt->first >> 12) & 33554431ll;
                        if (Index->find(int(destination)) != representative) {
                            remaining.push_back(*edgeIt);
                        }
                    }
                }
                for (std::vector<int>::iterator ccIt = Index->CC.begin();
                     ccIt != Index->CC.end(); ++ccIt) {
                    Index->outLabel[*ccIt].clear();
                }
                Index->outLabel[representative].swap(remaining);
            }
            if (reverseProfile) {
                profileSccMicros += currentTime() - profileStepStart;
            }
        }

        unsigned long long profileSaveStart =
            reverseProfile ? currentTime() : 0;
        for (std::set<EncodedEdge>::iterator it =
                 Index->key.begin(); it != Index->key.end(); ++it) {
            std::map<EncodedEdge,std::pair<int,int>>::iterator interval =
                Index->S.find(*it);
            if (interval != Index->S.end()) {
                interval->second.first = te;
            }
            else {
                Index->S[*it] = std::make_pair(te, te);
            }
        }
        if (reverseProfile) {
            profileSaveMicros += currentTime() - profileSaveStart;
        }
        ++completedEndAnchors;

        if (Index->S.size() >= nextReverseConstructedResReport) {
            while (nextReverseConstructedResReport <= Index->S.size()) {
                nextReverseConstructedResReport += 1000;
            }
            std::cout
                << "[Reverse RES constructor] Constructed reverse RES index entries: "
                << Index->S.size()
                << " / input edges " << Index->m
                << "; completed end anchors: "
                << (Index->tmax - te + 1)
                << " / " << totalEndAnchors
                << "; elapsed = "
                << timeFormatting(currentTime() - start_time).str()
                << "." << std::endl;
        }

        int processed = Index->tmax - te;
        if (processed % 100 == 0) {
            putProcess(
                double(processed + 1) / double(Index->t1 + 1),
                currentTime() - start_time
            );
        }

        // Decreasing the end time only removes edges.  Once no RES edge is
        // required for G_[0,te], every smaller end anchor is also empty.
        if (Index->key.empty() && te > lowerTime) {
            skippedEmptyEndAnchors = te - lowerTime;
            break;
        }
    }

    Index->n = originalN;

    unsigned long long profileMaterializeStart =
        reverseProfile ? currentTime() : 0;
    for (std::map<EncodedEdge,std::pair<int,int>>::iterator it =
             Index->S.begin(); it != Index->S.end(); ++it) {
        int left = it->second.first;
        int right = it->second.second;
        if (left < 0 || right > Index->tmax || left > right) {
            continue;
        }
        Index->G[left].push_back(RES(it->first, right));
        Index->Chunk[left / Index->len].push_back(RES(it->first, right));
    }

    for (int t = 0; t <= Index->tmax; ++t) {
        std::sort(Index->G[t].begin(), Index->G[t].end(), cmpReverse);
    }
    for (int block = 0; block <= Index->tmax / Index->len; ++block) {
        std::sort(
            Index->Chunk[block].begin(),
            Index->Chunk[block].end(),
            cmpReverse
        );
    }
    if (reverseProfile) {
        profileMaterializeMicros += currentTime() - profileMaterializeStart;
    }

    std::cout
        << "[Reverse RES constructor] Strict end-time construction finished. "
        << "Constructed reverse RES index entries: " << Index->S.size()
        << " / input edges " << Index->m
        << "; processed end anchors: "
        << completedEndAnchors << " / " << totalEndAnchors;
    if (skippedEmptyEndAnchors > 0) {
        std::cout << "; proven-empty end anchors skipped: "
                  << skippedEmptyEndAnchors;
    }
    std::cout
        << "; elapsed = "
        << timeFormatting(currentTime() - start_time).str()
        << "." << std::endl;

    if (reverseProfile) {
        double averagePoint =
            profileInnerSteps == 0
                ? 0.0
                : double(profilePointTotal) / double(profileInnerSteps);
        std::cout
            << "[Reverse RES profile] reset=" << profileResetMicros << "us"
            << ", newedge=" << profileNewedgeMicros << "us"
            << ", scan=" << profileScanMicros << "us"
            << ", candidates=" << profileCandidateMicros << "us"
            << ", sort=" << profileSortMicros << "us"
            << ", scc=" << profileSccMicros << "us"
            << ", saveS=" << profileSaveMicros << "us"
            << ", materialize=" << profileMaterializeMicros << "us"
            << "." << std::endl;
        std::cout
            << "[Reverse RES profile] inner steps=" << profileInnerSteps
            << ", skipped inner steps=" << profileSkippedInnerSteps
            << ", newedge processed=" << profileNewedgeItems
            << ", raw-edge visits=" << profileRawItems
            << ", candidate checks=" << profileCandidateChecks
            << ", average point size=" << averagePoint
            << ", max point size=" << profileMaxPoint
            << "." << std::endl;
    }

    delete [] Index->edge;
    Index->edge = nullptr;
    Index->initializeAppearanceIntervals();
    return Index;
}

OptimizedIndex * OptimizedIndex::buildERESConstructor(TemporalGraph * Graph) {
    if (Graph == nullptr) {
        return nullptr;
    }

    unsigned long long startTime = currentTime();
    OptimizedIndex *Index = new OptimizedIndex();

    const int graphVertexCount = Graph->numOfVertices();
    Index->n = graphVertexCount;
    Index->originalVertexIds = Graph->original_vertex_ids;
    Index->tmax = Graph->tmax;
    Index->t1 = Graph->tmax;
    Index->len = std::max(
        1, int(std::sqrt(double(std::max(1, Index->tmax)))));
    Index->reverseIndex = true;
    Index->S.clear();
    Index->appearanceIntervals.clear();
    Index->lastUpdatedEdgeCount = 0;
    Index->lastUpdateTimeMicros = 0;
    Index->lastMaterializationTimeMicros = 0;

    std::vector<std::vector<std::pair<int,int>>> activeEdges(
        Index->tmax + 1);
    std::vector<int> denseId(graphVertexCount, -1);
    std::vector<int> denseToGraphVertex;
    std::vector<unsigned char> denseDegreeRole;
    std::size_t activeEdgeCount = 0;
    std::size_t sourceDestinationOverlapCount = 0;

    auto getDenseId = [&](int vertex) -> int {
        int& id = denseId[vertex];
        if (id < 0) {
            id = int(denseToGraphVertex.size());
            denseToGraphVertex.push_back(vertex);
            denseDegreeRole.push_back(0);
        }
        return id;
    };

    auto addDegreeRole = [&](int denseVertex, unsigned char role) {
        unsigned char oldRole = denseDegreeRole[denseVertex];
        unsigned char newRole =
            static_cast<unsigned char>(oldRole | role);
        if (oldRole != 3 && newRole == 3) {
            ++sourceDestinationOverlapCount;
        }
        denseDegreeRole[denseVertex] = newRole;
    };

    for (int time = 0; time <= Index->tmax; ++time) {
        if (time >= int(Graph->temporal_edge.size())) {
            break;
        }
        const std::vector<std::pair<int,int>>& bucket =
            Graph->temporal_edge[time];
        activeEdges[time].reserve(bucket.size());
        for (std::vector<std::pair<int,int>>::const_iterator edgeIt =
                 bucket.begin(); edgeIt != bucket.end(); ++edgeIt) {
            int source = edgeIt->first;
            int destination = edgeIt->second;
            if (source < 0 || source >= graphVertexCount ||
                destination < 0 || destination >= graphVertexCount) {
                continue;
            }
            int denseSource = getDenseId(source);
            int denseDestination = getDenseId(destination);
            addDegreeRole(denseSource, 1);
            addDegreeRole(denseDestination, 2);
            activeEdges[time].push_back(std::make_pair(
                denseSource, denseDestination));
            ++activeEdgeCount;
        }
    }
    std::vector<int>().swap(denseId);
    std::vector<unsigned char>().swap(denseDegreeRole);

    Index->m = activeEdgeCount >
        std::size_t(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max()
        : int(activeEdgeCount);

    std::cout
        << "[ERES-con] Full optimized end-time construction starts. "
        << "End anchors: " << (Index->tmax + 1)
        << ", active edges = " << activeEdgeCount
        << " / loaded edges " << Graph->numOfEdges()
        << ", active vertices = " << denseToGraphVertex.size()
        << " / loaded vertices " << graphVertexCount
        << ", vertices with both in/out edges = "
        << sourceDestinationOverlapCount
        << "; diagonal SCCID pruning and internal non-RES pruning are ENABLED."
        << std::endl;

    unsigned long long phiStart = currentTime();
    unsigned long long pruningStats[15] = {};
    if (sourceDestinationOverlapCount == 0) {
        std::cout
            << "[ERES-con] No active vertex has both an incoming "
            << "and an outgoing edge. A directed cycle is impossible; "
            << "all reverse RES families are empty."
            << std::endl;
    }
    else {
        computeReverseConstructorPhiRangeDiagonalPruned(
            int(denseToGraphVertex.size()),
            activeEdges,
            0,
            Index->tmax,
            Index->tmax,
            pruningStats,
            true,
            startTime,
            Index,
            &denseToGraphVertex);
    }
    unsigned long long phiMicros = currentTime() - phiStart;

    unsigned long long membershipStart = currentTime();
    Index->initializeAppearanceIntervals();
    unsigned long long membershipMicros =
        currentTime() - membershipStart;
    std::vector<int>().swap(denseToGraphVertex);
    std::vector<std::vector<std::pair<int,int>>>().swap(activeEdges);

    // Query state remains indexed by the original graph vertex ids.  Only the
    // construction workspace is densely compressed, so isolated vertices in
    // the selected timestamp prefix are still returned as singleton SCCs.
    Index->Sta = new int[Index->n + 1];
    Index->Vis = new int[Index->n]();
    Index->Vis2 = new int[Index->n]();
    Index->f = new int[Index->n]();
    Index->outLabel =
        new std::vector<std::pair<long long,int>>[Index->n]();
    Index->outLabel2 =
        new std::vector<std::pair<long long,int>>[Index->n]();
    Index->G = new std::vector<RES>[Index->tmax + 1]();
    Index->Chunk = new std::vector<RES>[Index->tmax + 1]();
    Index->actual_time =
        new std::vector<int>[Index->tmax + 1]();

    unsigned long long materializationStart = currentTime();
    Index->rebuildReverseStorageCollapsed();
    Index->lastMaterializationTimeMicros =
        currentTime() - materializationStart;

    double pruneRatio = pruningStats[0] == 0 ? 0.0 :
        100.0 * double(pruningStats[1]) / double(pruningStats[0]);
    double internalDropRatio = pruningStats[7] == 0 ? 0.0 :
        100.0 * double(pruningStats[9]) / double(pruningStats[7]);
    double uniquePruneRatio = activeEdgeCount == 0 ? 0.0 :
        100.0 * double(pruningStats[11]) / double(activeEdgeCount);

    std::cout
        << "[ERES-con] Optimized Phi construction finished in "
        << timeFormatting(phiMicros).str()
        << "; membership insertion = "
        << timeFormatting(membershipMicros).str()
        << "; materialization = "
        << timeFormatting(Index->lastMaterializationTimeMicros).str()
        << "." << std::endl;
    std::cout
        << "[ERES-con][Diagonal pruning] end anchors built = "
        << pruningStats[4]
        << "; SCCID checks = " << pruningStats[0]
        << "; deferred/pruned events = " << pruningStats[1]
        << " (" << std::fixed << std::setprecision(2)
        << pruneRatio << "%)"
        << "; fresh prune events = " << pruningStats[5]
        << "; repeated deferred prune events = " << pruningStats[6]
        << "; deferred activations = " << pruningStats[2]
        << "; max deferred bucket size = " << pruningStats[3]
        << "." << std::endl;
    std::cout
        << "[ERES-con][Internal non-RES pruning] redundant internal non-RES events = "
        << pruningStats[7]
        << "; safe same-end deletions = " << pruningStats[9]
        << " (" << std::fixed << std::setprecision(2)
        << internalDropRatio << "% of internal non-RES events)"
        << "; temporarily retained for smaller end anchors = "
        << pruningStats[10]
        << "; internal RES-selected events = " << pruningStats[8]
        << "." << std::endl;
    if (pruningStats[14] != 0) {
        std::cout
            << "[ERES-con][Internal non-RES pruning] unique pruned active edges = "
            << pruningStats[11]
            << " (" << std::fixed << std::setprecision(2)
            << uniquePruneRatio
            << "% of input edges)"
            << "; unique same-end permanent drops = " << pruningStats[12]
            << "; unique temporary retained edges = " << pruningStats[13]
            << "." << std::endl;
    }
    else {
        std::cout
            << "[ERES-con][Internal non-RES pruning] unique-event diagnostics disabled for this large construction; pruning behavior is unchanged."
            << std::endl;
    }
    std::cout
        << "[ERES-con] Full optimized end-time construction finished. "
        << "Constructed collapsed reverse RES index entries: "
        << Index->S.size()
        << " / input edges " << Index->m
        << "; elapsed = "
        << timeFormatting(currentTime() - startTime).str()
        << "." << std::endl;

    return Index;
}

std::map<int, std::set<OptimizedIndex::EncodedEdge>>
OptimizedIndex::computeReverseConstructorPhiRange(
    int vertexCount,
    const std::vector<std::vector<std::pair<int,int>>>& activeEdges,
    int leftEnd,
    int rightEnd,
    int contextRightEnd) {

    std::map<int, std::set<EncodedEdge>> result;
    if (vertexCount <= 0 || activeEdges.empty()) {
        return result;
    }

    int contextRight = contextRightEnd < 0 ? rightEnd : contextRightEnd;
    contextRight = std::min(contextRight, int(activeEdges.size()) - 1);
    int right = std::min(rightEnd, contextRight);
    int left = std::max(0, leftEnd);
    if (left > right) {
        return result;
    }

    // Reverse constructor with original-RES-style reuse across end anchors.
    //
    // End anchors move from right to left.  Work.key from the previous anchor
    // is reintroduced through Work.newedge, exactly like the original RES
    // constructor reuses the previous start anchor.  The important correction
    // compared with the earlier reverse draft is the SCC point set: when
    // scanning a smaller te, old labels can contain vertices not touched by
    // the current timestamp bucket.  We therefore include every condensed
    // vertex that still has an in/out label before running the two DFS passes.
    // Without that, one-way stale paths such as (5,6,1) in [0,1] can be
    // incorrectly selected.
    const int workingN = vertexCount + 1;

    OptimizedIndex Work;
    Work.n = workingN;
    Work.m = 0;
    Work.tmax = contextRight;
    Work.t1 = contextRight;
    Work.len = std::max(1, int(std::sqrt(double(std::max(1, contextRight)))));

    Work.Sta = new int[workingN];
    Work.Vis = new int[workingN];
    Work.Vis2 = new int[workingN];
    Work.f = new int[workingN];
    Work.edge = new std::vector<std::pair<long long,int>>[contextRight + 1]();
    Work.newedge = new std::vector<std::pair<long long,int>>[contextRight + 1]();
    Work.outLabel = new std::vector<std::pair<long long,int>>[workingN]();
    Work.outLabel2 = new std::vector<std::pair<long long,int>>[workingN]();
    Work.top = 0;
    Work.key.clear();

    for (int t = 0; t <= contextRight; ++t) {
        const std::vector<std::pair<int,int>>& bucket = activeEdges[t];
        for (std::vector<std::pair<int,int>>::const_iterator iter =
                 bucket.begin(); iter != bucket.end(); ++iter) {
            long long encoded =
                (((long long)iter->first) << 37) +
                (((long long)iter->second) << 12);
            Work.edge[t].push_back(std::make_pair(encoded, t));
            ++Work.m;
        }
    }

    std::set<EncodedEdge> table;
    for (int te = contextRight; te >= left; --te) {
        std::vector<int> labelCandidates;
        std::vector<char> inLabelCandidate(workingN, 0);
        for (int u = 0; u < workingN; ++u) {
            Work.outLabel[u].clear();
            Work.outLabel2[u].clear();
            Work.f[u] = u;
            Work.Vis[u] = 0;
            Work.Vis2[u] = 0;
        }

        for (int t = 0; t <= contextRight; ++t) {
            Work.newedge[t].clear();
        }
        for (std::set<EncodedEdge>::iterator it =
                 Work.key.begin(); it != Work.key.end(); ++it) {
            if (it->second >= 0 && it->second <= contextRight) {
                Work.newedge[it->second].push_back(*it);
            }
        }

        table = Work.key;
        Work.key.clear();

        for (int t = te; t >= 0; --t) {
            std::vector<int> point;
            Work.tmpedge.clear();

            for (std::vector<EncodedEdge>::iterator it =
                     Work.newedge[t].begin();
                 it != Work.newedge[t].end(); ++it) {
                EncodedEdge g = *it;
                int u = Work.find(int(g.first >> 37));
                int v = Work.find(int((g.first >> 12) & 33554431ll));
                int edgeTime = g.second;

                if (u == v) {
                    Work.tmpedge.push_back(g);
                    continue;
                }
                if (edgeTime > te) {
                    continue;
                }
                point.push_back(u);
                point.push_back(v);
                Work.outLabel[u].push_back(g);
                Work.outLabel2[v].push_back(g);
                if (!inLabelCandidate[u]) {
                    inLabelCandidate[u] = 1;
                    labelCandidates.push_back(u);
                }
                if (!inLabelCandidate[v]) {
                    inLabelCandidate[v] = 1;
                    labelCandidates.push_back(v);
                }
            }

            for (std::vector<EncodedEdge>::iterator it =
                     Work.edge[t].begin();
                 it != Work.edge[t].end(); ++it) {
                EncodedEdge g = *it;
                if (table.find(g) != table.end()) {
                    continue;
                }

                int u = Work.find(int(g.first >> 37));
                int v = Work.find(int((g.first >> 12) & 33554431ll));
                int edgeTime = g.second;

                if (u == v) {
                    Work.tmpedge.push_back(g);
                    continue;
                }
                if (edgeTime > te) {
                    continue;
                }
                point.push_back(u);
                point.push_back(v);
                Work.outLabel[u].push_back(g);
                Work.outLabel2[v].push_back(g);
                if (!inLabelCandidate[u]) {
                    inLabelCandidate[u] = 1;
                    labelCandidates.push_back(u);
                }
                if (!inLabelCandidate[v]) {
                    inLabelCandidate[v] = 1;
                    labelCandidates.push_back(v);
                }
            }

            std::vector<int> nextLabelCandidates;
            nextLabelCandidates.reserve(labelCandidates.size());
            for (std::vector<int>::iterator candidate =
                     labelCandidates.begin();
                 candidate != labelCandidates.end(); ++candidate) {
                int vertex = *candidate;
                if (!Work.outLabel[vertex].empty() ||
                    !Work.outLabel2[vertex].empty()) {
                    point.push_back(vertex);
                    nextLabelCandidates.push_back(vertex);
                }
                else {
                    inLabelCandidate[vertex] = 0;
                }
            }
            labelCandidates.swap(nextLabelCandidates);

            std::sort(point.begin(), point.end());
            point.erase(std::unique(point.begin(), point.end()), point.end());
            Work.edge[t] = Work.tmpedge;

            for (std::vector<int>::iterator it =
                     Work.markedVertices2.begin();
                 it != Work.markedVertices2.end(); ++it) {
                Work.Vis2[*it] = 0;
            }
            for (std::vector<int>::iterator it =
                     Work.markedVertices.begin();
                 it != Work.markedVertices.end(); ++it) {
                Work.Vis[*it] = 0;
            }
            Work.markedVertices2.clear();
            Work.markedVertices.clear();
            Work.top = 0;

            for (std::vector<int>::iterator it = point.begin();
                 it != point.end(); ++it) {
                if (!Work.Vis[*it]) {
                    Work.kosaraju1(*it);
                }
            }

            for (std::vector<int>::iterator it =
                     Work.markedVertices.begin();
                 it != Work.markedVertices.end(); ++it) {
                Work.Vis[*it] = 0;
            }
            Work.markedVertices.clear();
            Work.col = 0;

            while (Work.top) {
                int u = Work.Sta[Work.top--];
                int representative = Work.find(u);
                if (Work.Vis2[representative]) {
                    continue;
                }

                ++Work.col;
                Work.CC.clear();
                Work.kosaraju2(representative, te);
                Work.kosaraju4(representative, representative, te);

                std::vector<EncodedEdge> remaining;
                for (std::vector<int>::iterator ccIt = Work.CC.begin();
                     ccIt != Work.CC.end(); ++ccIt) {
                    int member = *ccIt;
                    for (std::vector<EncodedEdge>::iterator edgeIt =
                             Work.outLabel2[member].begin();
                         edgeIt != Work.outLabel2[member].end(); ++edgeIt) {
                        long long source = edgeIt->first >> 37;
                        if (Work.find(int(source)) != representative) {
                            remaining.push_back(*edgeIt);
                        }
                        else {
                            Work.edge[t].push_back(*edgeIt);
                        }
                    }
                }
                for (std::vector<int>::iterator ccIt = Work.CC.begin();
                     ccIt != Work.CC.end(); ++ccIt) {
                    Work.outLabel2[*ccIt].clear();
                    std::vector<EncodedEdge>().swap(Work.outLabel2[*ccIt]);
                }
                Work.outLabel2[representative] = remaining;

                remaining.clear();
                for (std::vector<int>::iterator ccIt = Work.CC.begin();
                     ccIt != Work.CC.end(); ++ccIt) {
                    int member = *ccIt;
                    for (std::vector<EncodedEdge>::iterator edgeIt =
                             Work.outLabel[member].begin();
                         edgeIt != Work.outLabel[member].end(); ++edgeIt) {
                        long long destination =
                            (edgeIt->first >> 12) & 33554431ll;
                        if (Work.find(int(destination)) != representative) {
                            remaining.push_back(*edgeIt);
                        }
                    }
                }
                for (std::vector<int>::iterator ccIt = Work.CC.begin();
                     ccIt != Work.CC.end(); ++ccIt) {
                    Work.outLabel[*ccIt].clear();
                    std::vector<EncodedEdge>().swap(Work.outLabel[*ccIt]);
                }
                Work.outLabel[representative] = remaining;
            }
        }

        if (te <= right) {
            result[te] = Work.key;
        }
    }

    return result;
}

std::map<int, std::set<OptimizedIndex::EncodedEdge>>
OptimizedIndex::computeReverseConstructorPhiRangeDiagonalPruned(
    int vertexCount,
    const std::vector<std::vector<std::pair<int,int>>>& activeEdges,
    int leftEnd,
    int rightEnd,
    int contextRightEnd,
    unsigned long long *stats,
    bool reportProgress,
    unsigned long long progressStartTime,
    OptimizedIndex *directIntervalTarget,
    const std::vector<int> *vertexRemap) {

    std::map<int, std::set<EncodedEdge>> result;
    if (vertexCount <= 0 || activeEdges.empty()) {
        return result;
    }

    int contextRight = contextRightEnd < 0 ? rightEnd : contextRightEnd;
    contextRight = std::min(contextRight, int(activeEdges.size()) - 1);
    int right = std::min(rightEnd, contextRight);
    int left = std::max(0, leftEnd);
    if (left > right) {
        return result;
    }

    const int totalEndAnchors = contextRight - left + 1;
    const int progressAnchorStep = std::max(1, totalEndAnchors / 100);
    const unsigned long long progressIntervalMicros = 30000000ULL;
    int completedEndAnchors = 0;
    const unsigned long long effectiveProgressStartTime =
        progressStartTime == 0 ? currentTime() : progressStartTime;
    unsigned long long lastProgressReport = effectiveProgressStartTime;

    const int workingN = vertexCount + 1;

    OptimizedIndex Work;
    Work.n = workingN;
    Work.m = 0;
    Work.tmax = contextRight;
    Work.t1 = contextRight;
    Work.len = std::max(1, int(std::sqrt(double(std::max(1, contextRight)))));

    Work.Sta = new int[workingN];
    Work.Vis = new int[workingN];
    Work.Vis2 = new int[workingN];
    Work.f = new int[workingN];
    Work.edge = new std::vector<std::pair<long long,int>>[contextRight + 1]();
    Work.newedge = new std::vector<std::pair<long long,int>>[contextRight + 1]();
    Work.outLabel = new std::vector<std::pair<long long,int>>[workingN]();
    Work.outLabel2 = new std::vector<std::pair<long long,int>>[workingN]();
    Work.top = 0;
    Work.key.clear();

    for (int t = 0; t <= contextRight; ++t) {
        const std::vector<std::pair<int,int>>& bucket = activeEdges[t];
        for (std::vector<std::pair<int,int>>::const_iterator iter =
                 bucket.begin(); iter != bucket.end(); ++iter) {
            long long encoded =
                (((long long)iter->first) << 37) +
                (((long long)iter->second) << 12);
            Work.edge[t].push_back(std::make_pair(encoded, t));
            ++Work.m;
        }
    }

    ComponentHistory previousHistory(vertexCount);
    ComponentHistory currentHistory(vertexCount);
    std::vector<int> componentHead(workingN, -1);
    std::vector<int> componentTail(workingN, -1);
    std::vector<int> componentNext(vertexCount, -1);
    std::vector<int> historyPosition(vertexCount, -1);
    std::vector<int> labelCandidateEpoch(workingN, 0);
    int currentCandidateEpoch = 0;
    std::vector<int> touchedNewedgeTimes;
    bool hasPreviousHistory = false;
    std::unordered_set<EncodedEdge, ExperimentalEdgeHash> table;
    table.max_load_factor(0.75f);
    const bool trackUniquePruneStats = Work.m <= 1000000;
    std::unordered_set<EncodedEdge, ExperimentalEdgeHash>
        uniqueInternalNonResPrunedEdges;
    std::unordered_set<EncodedEdge, ExperimentalEdgeHash>
        uniqueSameEndPermanentPrunedEdges;
    std::unordered_set<EncodedEdge, ExperimentalEdgeHash>
        uniqueShelvedInternalNonResEdges;
    if (stats != nullptr) {
        stats[14] = trackUniquePruneStats ? 1 : 0;
    }

    for (int te = contextRight; te >= left; --te) {
        if (stats != nullptr) {
            ++stats[4];
        }

        currentHistory.clearChanges();
        for (int u = 0; u < vertexCount; ++u) {
            componentHead[u] = u;
            componentTail[u] = u;
            componentNext[u] = -1;
        }
        for (int u = vertexCount; u < workingN; ++u) {
            componentHead[u] = -1;
            componentTail[u] = -1;
        }

        std::vector<int> labelCandidates;
        ++currentCandidateEpoch;
        if (currentCandidateEpoch == std::numeric_limits<int>::max()) {
            std::fill(
                labelCandidateEpoch.begin(),
                labelCandidateEpoch.end(),
                0);
            currentCandidateEpoch = 1;
        }
        std::map<int, std::vector<EncodedEdge>> deferredByStart;
        if (hasPreviousHistory) {
            std::fill(historyPosition.begin(), historyPosition.end(), -1);
        }

        for (int u = 0; u < workingN; ++u) {
            Work.outLabel[u].clear();
            Work.outLabel2[u].clear();
            Work.f[u] = u;
            Work.Vis[u] = 0;
            Work.Vis2[u] = 0;
        }

        for (std::vector<int>::const_iterator touched =
                 touchedNewedgeTimes.begin();
             touched != touchedNewedgeTimes.end(); ++touched) {
            Work.newedge[*touched].clear();
        }
        touchedNewedgeTimes.clear();
        table.clear();
        table.reserve(Work.key.size());
        for (std::set<EncodedEdge>::iterator it =
                 Work.key.begin(); it != Work.key.end(); ++it) {
            if (it->second >= 0 && it->second <= contextRight) {
                if (Work.newedge[it->second].empty()) {
                    touchedNewedgeTimes.push_back(it->second);
                }
                Work.newedge[it->second].push_back(*it);
            }
            table.insert(*it);
        }

        Work.key.clear();

        auto historyPositionAt = [&](int vertex, int start) -> int {
            if (vertex < 0 || vertex >= vertexCount) {
                return 0;
            }
            const std::vector<std::pair<int,int>>& changes =
                previousHistory.changes[vertex];
            int& position = historyPosition[vertex];
            if (position < 0) {
                position = int(changes.size());
            }
            while (position > 0 &&
                   changes[position - 1].first >= start) {
                --position;
            }
            return position;
        };

        auto historyComponentAt = [&](int vertex, int start) -> int {
            if (vertex < 0 || vertex >= vertexCount) {
                return vertex;
            }
            const std::vector<std::pair<int,int>>& changes =
                previousHistory.changes[vertex];
            int position = historyPositionAt(vertex, start);
            return position == int(changes.size())
                ? vertex : changes[position].second;
        };

        auto historyPreviousChangeStart =
            [&](int vertex, int start) -> int {
                if (vertex < 0 || vertex >= vertexCount) {
                    return -1;
                }
                int position = historyPositionAt(vertex, start);
                return position > 0
                    ? previousHistory.changes[vertex][position - 1].first
                    : -1;
            };

        std::vector<int> point;
        std::vector<int> nextLabelCandidates;
        for (int t = te; t >= 0; --t) {
            if (reportProgress && t % 100 == 0) {
                unsigned long long now = currentTime();
                if (now - lastProgressReport >= progressIntervalMicros) {
                    std::cout
                        << "[ERES-con] Processing end anchor t_e="
                        << te << ", current start anchor t_s=" << t
                        << "; completed end anchors: "
                        << completedEndAnchors << " / " << totalEndAnchors
                        << "; current fixed-end RES edges: "
                        << Work.key.size() << " / input edges " << Work.m
                        << "; elapsed = "
                        << timeFormatting(
                               now - effectiveProgressStartTime).str()
                        << "." << std::endl;
                    lastProgressReport = now;
                }
            }

            bool hasNewCrossingEdge = false;
            point.clear();
            Work.tmpedge.clear();

            auto shouldPrune = [&](const EncodedEdge& g) -> bool {
                if (!hasPreviousHistory) {
                    return false;
                }
                int originalSource = int(g.first >> 37);
                int originalDestination =
                    int((g.first >> 12) & 33554431ll);
                if (originalSource < 0 || originalSource >= vertexCount ||
                    originalDestination < 0 ||
                    originalDestination >= vertexCount) {
                    return false;
                }
                if (stats != nullptr) {
                    ++stats[0];
                }
                return historyComponentAt(originalSource, t) !=
                       historyComponentAt(originalDestination, t);
            };

            auto scheduleDeferred = [&](const EncodedEdge& g) {
                int originalSource = int(g.first >> 37);
                int originalDestination =
                    int((g.first >> 12) & 33554431ll);
                int nextStart = std::max(
                    historyPreviousChangeStart(originalSource, t),
                    historyPreviousChangeStart(originalDestination, t));
                if (nextStart >= 0 && nextStart < t) {
                    std::vector<EncodedEdge>& bucket =
                        deferredByStart[nextStart];
                    bucket.push_back(g);
                    if (stats != nullptr &&
                        bucket.size() > stats[3]) {
                        stats[3] = bucket.size();
                    }
                }
            };

            auto activateEdge = [&](const EncodedEdge& g,
                                    bool fromDeferred) {
                int edgeTime = g.second;
                if (edgeTime > te) {
                    return;
                }
                if (shouldPrune(g)) {
                    scheduleDeferred(g);
                    if (stats != nullptr) {
                        ++stats[1];
                        if (fromDeferred) {
                            ++stats[6];
                        }
                        else {
                            ++stats[5];
                        }
                    }
                    return;
                }

                int u = Work.find(int(g.first >> 37));
                int v = Work.find(int((g.first >> 12) & 33554431ll));
                if (u == v) {
                    bool selectedIntoRes =
                        (Work.key.find(g) != Work.key.end());
                    if (stats != nullptr) {
                        if (selectedIntoRes) {
                            ++stats[8];
                        }
                        else {
                            ++stats[7];
                        }
                    }
                    if (!selectedIntoRes) {
                        if (trackUniquePruneStats) {
                            uniqueInternalNonResPrunedEdges.insert(g);
                        }
                        if (edgeTime == te) {
                            if (trackUniquePruneStats) {
                                uniqueSameEndPermanentPrunedEdges.insert(g);
                            }
                            if (stats != nullptr) {
                                ++stats[9];
                            }
                        }
                        else {
                            if (trackUniquePruneStats) {
                                uniqueShelvedInternalNonResEdges.insert(g);
                            }
                            if (edgeTime == t) {
                                Work.tmpedge.push_back(g);
                            }
                            else if (edgeTime >= 0 &&
                                     edgeTime <= contextRight) {
                                Work.edge[edgeTime].push_back(g);
                            }
                            if (stats != nullptr) {
                                ++stats[10];
                            }
                        }
                        return;
                    }
                    Work.tmpedge.push_back(g);
                    return;
                }

                if (fromDeferred && stats != nullptr) {
                    ++stats[2];
                }
                point.push_back(u);
                point.push_back(v);
                hasNewCrossingEdge = true;
                Work.outLabel[u].push_back(g);
                Work.outLabel2[v].push_back(g);
                if (labelCandidateEpoch[u] != currentCandidateEpoch) {
                    labelCandidateEpoch[u] = currentCandidateEpoch;
                    labelCandidates.push_back(u);
                }
                if (labelCandidateEpoch[v] != currentCandidateEpoch) {
                    labelCandidateEpoch[v] = currentCandidateEpoch;
                    labelCandidates.push_back(v);
                }
            };

            std::map<int, std::vector<EncodedEdge>>::iterator deferredIt =
                deferredByStart.find(t);
            if (deferredIt != deferredByStart.end()) {
                std::vector<EncodedEdge> currentDeferred;
                currentDeferred.swap(deferredIt->second);
                deferredByStart.erase(deferredIt);
                for (std::vector<EncodedEdge>::const_iterator it =
                         currentDeferred.begin();
                     it != currentDeferred.end(); ++it) {
                    activateEdge(*it, true);
                }
            }

            for (std::vector<EncodedEdge>::iterator it =
                     Work.newedge[t].begin();
                 it != Work.newedge[t].end(); ++it) {
                activateEdge(*it, false);
            }

            for (std::vector<EncodedEdge>::iterator it =
                     Work.edge[t].begin();
                 it != Work.edge[t].end(); ++it) {
                EncodedEdge g = *it;
                if (table.find(g) != table.end()) {
                    continue;
                }
                activateEdge(g, false);
            }

            if (!hasNewCrossingEdge) {
                Work.edge[t].swap(Work.tmpedge);
                continue;
            }

            nextLabelCandidates.clear();
            nextLabelCandidates.reserve(labelCandidates.size());
            for (std::vector<int>::iterator candidate =
                     labelCandidates.begin();
                 candidate != labelCandidates.end(); ++candidate) {
                int vertex = *candidate;
                if (!Work.outLabel[vertex].empty() ||
                    !Work.outLabel2[vertex].empty()) {
                    point.push_back(vertex);
                    nextLabelCandidates.push_back(vertex);
                }
                else {
                    labelCandidateEpoch[vertex] = 0;
                }
            }
            labelCandidates.swap(nextLabelCandidates);

            std::sort(point.begin(), point.end());
            point.erase(std::unique(point.begin(), point.end()), point.end());
            Work.edge[t].swap(Work.tmpedge);

            for (std::vector<int>::iterator it =
                     Work.markedVertices2.begin();
                 it != Work.markedVertices2.end(); ++it) {
                Work.Vis2[*it] = 0;
            }
            for (std::vector<int>::iterator it =
                     Work.markedVertices.begin();
                 it != Work.markedVertices.end(); ++it) {
                Work.Vis[*it] = 0;
            }
            Work.markedVertices2.clear();
            Work.markedVertices.clear();
            Work.top = 0;

            for (std::vector<int>::iterator it = point.begin();
                 it != point.end(); ++it) {
                if (!Work.Vis[*it]) {
                    Work.kosaraju1(*it);
                }
            }

            for (std::vector<int>::iterator it =
                     Work.markedVertices.begin();
                 it != Work.markedVertices.end(); ++it) {
                Work.Vis[*it] = 0;
            }
            Work.markedVertices.clear();
            Work.col = 0;

            while (Work.top) {
                int u = Work.Sta[Work.top--];
                int representative = Work.find(u);
                if (Work.Vis2[representative]) {
                    continue;
                }

                ++Work.col;
                Work.CC.clear();
                Work.kosaraju2(representative, te);
                Work.kosaraju4(representative, representative, te);

                if (Work.CC.size() > 1) {
                    for (std::vector<int>::iterator ccIt =
                             Work.CC.begin();
                         ccIt != Work.CC.end(); ++ccIt) {
                        int member = *ccIt;
                        if (member == representative ||
                            member < 0 || member >= workingN) {
                            continue;
                        }
                        int original = componentHead[member];
                        while (original >= 0) {
                            currentHistory.addChange(
                                original, t, representative);
                            original = componentNext[original];
                        }

                        if (componentHead[member] >= 0) {
                            if (componentHead[representative] < 0) {
                                componentHead[representative] =
                                    componentHead[member];
                                componentTail[representative] =
                                    componentTail[member];
                            }
                            else {
                                componentNext[
                                    componentTail[representative]] =
                                    componentHead[member];
                                componentTail[representative] =
                                    componentTail[member];
                            }
                        }
                        componentHead[member] = -1;
                        componentTail[member] = -1;
                    }
                }

                std::vector<EncodedEdge> remaining;
                for (std::vector<int>::iterator ccIt = Work.CC.begin();
                     ccIt != Work.CC.end(); ++ccIt) {
                    int member = *ccIt;
                    for (std::vector<EncodedEdge>::iterator edgeIt =
                             Work.outLabel2[member].begin();
                         edgeIt != Work.outLabel2[member].end(); ++edgeIt) {
                        long long source = edgeIt->first >> 37;
                        if (Work.find(int(source)) != representative) {
                            remaining.push_back(*edgeIt);
                        }
                        else {
                            bool selectedIntoRes =
                                (Work.key.find(*edgeIt) != Work.key.end());
                            if (stats != nullptr) {
                                if (selectedIntoRes) {
                                    ++stats[8];
                                }
                                else {
                                    ++stats[7];
                                }
                            }
                            if (!selectedIntoRes) {
                                if (trackUniquePruneStats) {
                                    uniqueInternalNonResPrunedEdges.insert(*edgeIt);
                                }
                                if (edgeIt->second == te) {
                                    if (trackUniquePruneStats) {
                                        uniqueSameEndPermanentPrunedEdges.insert(*edgeIt);
                                    }
                                    if (stats != nullptr) {
                                        ++stats[9];
                                    }
                                }
                                else {
                                    if (trackUniquePruneStats) {
                                        uniqueShelvedInternalNonResEdges.insert(*edgeIt);
                                    }
                                    if (edgeIt->second >= 0 &&
                                        edgeIt->second <= contextRight) {
                                        Work.edge[edgeIt->second].push_back(*edgeIt);
                                    }
                                    if (stats != nullptr) {
                                        ++stats[10];
                                    }
                                }
                                continue;
                            }
                            Work.edge[t].push_back(*edgeIt);
                        }
                    }
                }
                for (std::vector<int>::iterator ccIt = Work.CC.begin();
                     ccIt != Work.CC.end(); ++ccIt) {
                    Work.outLabel2[*ccIt].clear();
                }
                Work.outLabel2[representative].swap(remaining);

                remaining.clear();
                for (std::vector<int>::iterator ccIt = Work.CC.begin();
                     ccIt != Work.CC.end(); ++ccIt) {
                    int member = *ccIt;
                    for (std::vector<EncodedEdge>::iterator edgeIt =
                             Work.outLabel[member].begin();
                         edgeIt != Work.outLabel[member].end(); ++edgeIt) {
                        long long destination =
                            (edgeIt->first >> 12) & 33554431ll;
                        if (Work.find(int(destination)) != representative) {
                            remaining.push_back(*edgeIt);
                        }
                    }
                }
                for (std::vector<int>::iterator ccIt = Work.CC.begin();
                     ccIt != Work.CC.end(); ++ccIt) {
                    Work.outLabel[*ccIt].clear();
                }
                Work.outLabel[representative].swap(remaining);
            }
        }

        currentHistory.finalize();
        previousHistory.swapContents(currentHistory);
        hasPreviousHistory = true;

        if (te <= right) {
            if (directIntervalTarget != nullptr) {
                for (std::set<EncodedEdge>::const_iterator edgeIt =
                         Work.key.begin(); edgeIt != Work.key.end(); ++edgeIt) {
                    EncodedEdge storedEdge = *edgeIt;
                    if (vertexRemap != nullptr) {
                        int denseSource = edgeSource(*edgeIt);
                        int denseDestination = edgeDestination(*edgeIt);
                        if (denseSource < 0 ||
                            denseSource >= int(vertexRemap->size()) ||
                            denseDestination < 0 ||
                            denseDestination >= int(vertexRemap->size())) {
                            continue;
                        }
                        storedEdge = encodeEdge(
                            (*vertexRemap)[denseSource],
                            (*vertexRemap)[denseDestination],
                            edgeIt->second);
                    }

                    std::map<EncodedEdge,std::pair<int,int>>::iterator interval =
                        directIntervalTarget->S.find(storedEdge);
                    if (interval != directIntervalTarget->S.end()) {
                        interval->second.first = te;
                    }
                    else {
                        directIntervalTarget->S[storedEdge] =
                            std::make_pair(te, te);
                    }
                }
            }
            else {
                result[te] = Work.key;
            }
        }

        ++completedEndAnchors;
        const bool skipsRemainingEmptyAnchors =
            Work.key.empty() && te > left;
        if (reportProgress &&
            (completedEndAnchors % progressAnchorStep == 0 ||
             te == left || skipsRemainingEmptyAnchors)) {
            unsigned long long now = currentTime();
            std::cout
                << "[ERES-con] Constructed fixed-end RES family for t_e="
                << te << "; completed end anchors: "
                << completedEndAnchors << " / " << totalEndAnchors
                << "; current fixed-end RES edges: "
                << Work.key.size() << " / input edges " << Work.m;
            if (skipsRemainingEmptyAnchors) {
                std::cout << "; remaining empty end anchors skipped: "
                          << (totalEndAnchors - completedEndAnchors);
            }
            std::cout
                << "; elapsed = "
                << timeFormatting(now - effectiveProgressStartTime).str()
                << "." << std::endl;
            lastProgressReport = now;
        }

        // Decreasing te only removes temporal edges.  Once the maintained
        // certificate is empty, the current graph has no non-trivial SCC and
        // no smaller end anchor can create one.  All remaining Phi families
        // are therefore empty as well, so avoid repeatedly initializing an
        // otherwise empty workspace.
        if (Work.key.empty()) {
            break;
        }

    }

    if (stats != nullptr) {
        stats[11] = uniqueInternalNonResPrunedEdges.size();
        stats[12] = uniqueSameEndPermanentPrunedEdges.size();
        stats[13] = uniqueShelvedInternalNonResEdges.size();
    }

    return result;
}

void OptimizedIndex::initializeAppearanceIntervals() {
    appearanceIntervals.clear();
    for (std::map<EncodedEdge,std::pair<int,int>>::const_iterator it =
             S.begin(); it != S.end(); ++it) {
        appearanceIntervals[it->first].push_back(it->second);
    }
}

std::set<OptimizedIndex::EncodedEdge>
OptimizedIndex::recoverPhiAt(int startTime) const {
    std::set<EncodedEdge> phi;
    for (std::map<EncodedEdge,
                  std::vector<std::pair<int,int>>>::const_iterator it =
             appearanceIntervals.begin();
         it != appearanceIntervals.end(); ++it) {
        const std::vector<std::pair<int,int>>& intervals = it->second;
        for (std::vector<std::pair<int,int>>::const_iterator interval =
                 intervals.begin(); interval != intervals.end(); ++interval) {
            if (interval->first <= startTime &&
                startTime <= interval->second) {
                phi.insert(it->first);
                break;
            }
        }
    }
    return phi;
}

void OptimizedIndex::addPhiMembershipAt(
    int startTime,
    const std::set<EncodedEdge>& phi) {

    for (std::set<EncodedEdge>::const_iterator edgeIt = phi.begin();
         edgeIt != phi.end(); ++edgeIt) {
        std::vector<std::pair<int,int>>& intervals =
            appearanceIntervals[*edgeIt];

        std::vector<std::pair<int,int>>::iterator position =
            intervals.begin();
        while (position != intervals.end() &&
               position->second < startTime - 1) {
            ++position;
        }

        if (position == intervals.end()) {
            intervals.push_back(std::make_pair(startTime, startTime));
        }
        else if (position->first > startTime + 1) {
            intervals.insert(
                position, std::make_pair(startTime, startTime));
        }
        else {
            position->first = std::min(position->first, startTime);
            position->second = std::max(position->second, startTime);
            std::vector<std::pair<int,int>>::iterator next = position + 1;
            while (next != intervals.end() &&
                   next->first <= position->second + 1) {
                position->second =
                    std::max(position->second, next->second);
                next = intervals.erase(next);
            }
        }
    }
}

void OptimizedIndex::removePhiMembershipRange(int left, int right) {
    if (left > right) {
        return;
    }

    std::map<EncodedEdge,
             std::vector<std::pair<int,int>>>::iterator edgeIt =
        appearanceIntervals.begin();
    while (edgeIt != appearanceIntervals.end()) {
        std::vector<std::pair<int,int>> replacement;
        const std::vector<std::pair<int,int>>& intervals =
            edgeIt->second;
        for (std::vector<std::pair<int,int>>::const_iterator interval =
                 intervals.begin(); interval != intervals.end(); ++interval) {
            if (interval->second < left || interval->first > right) {
                replacement.push_back(*interval);
                continue;
            }
            if (interval->first < left) {
                replacement.push_back(
                    std::make_pair(interval->first, left - 1));
            }
            if (interval->second > right) {
                replacement.push_back(
                    std::make_pair(right + 1, interval->second));
            }
        }
        if (replacement.empty()) {
            appearanceIntervals.erase(edgeIt++);
        }
        else {
            edgeIt->second.swap(replacement);
            ++edgeIt;
        }
    }
}

void OptimizedIndex::rebuildForwardStorage() {
    if (reverseIndex) {
        return;
    }

    for (int time = 0; time <= tmax; ++time) {
        G[time].clear();
        Chunk[time].clear();
    }
    S.clear();

    for (std::map<EncodedEdge,
                  std::vector<std::pair<int,int>>>::const_iterator it =
             appearanceIntervals.begin();
         it != appearanceIntervals.end(); ++it) {
        const std::vector<std::pair<int,int>>& intervals = it->second;
        if (!intervals.empty()) {
            S[it->first] = std::make_pair(
                intervals.front().first, intervals.back().second);
        }
        for (std::vector<std::pair<int,int>>::const_iterator interval =
                 intervals.begin(); interval != intervals.end(); ++interval) {
            int left = std::max(0, interval->first);
            int right = std::min(tmax, interval->second);
            if (left > right) {
                continue;
            }
            G[right].push_back(RES(it->first, left));
            Chunk[right / len].push_back(RES(it->first, left));
        }
    }

    for (int time = 0; time <= tmax; ++time) {
        std::sort(G[time].begin(), G[time].end(), cmp);
    }
    for (int block = 0; block <= tmax / len; ++block) {
        std::sort(Chunk[block].begin(), Chunk[block].end(), cmp);
    }
}

void OptimizedIndex::rebuildReverseStorage() {
    if (!reverseIndex) {
        return;
    }

    for (int time = 0; time <= tmax; ++time) {
        G[time].clear();
        Chunk[time].clear();
    }
    S.clear();

    for (std::map<EncodedEdge,
                  std::vector<std::pair<int,int>>>::const_iterator it =
             appearanceIntervals.begin();
         it != appearanceIntervals.end(); ++it) {
        const std::vector<std::pair<int,int>>& intervals = it->second;
        if (!intervals.empty()) {
            S[it->first] = std::make_pair(
                intervals.front().first, intervals.back().second);
        }
        for (std::vector<std::pair<int,int>>::const_iterator interval =
                 intervals.begin(); interval != intervals.end(); ++interval) {
            int left = std::max(0, interval->first);
            int right = std::min(tmax, interval->second);
            if (left > right) {
                continue;
            }
            G[left].push_back(RES(it->first, right));
            Chunk[left / len].push_back(RES(it->first, right));
        }
    }

    for (int time = 0; time <= tmax; ++time) {
        std::sort(G[time].begin(), G[time].end(), cmpReverse);
    }
    for (int block = 0; block <= tmax / len; ++block) {
        std::sort(Chunk[block].begin(), Chunk[block].end(), cmpReverse);
    }
}

void OptimizedIndex::rebuildReverseStorageCollapsed() {
    if (!reverseIndex) {
        return;
    }

    for (int time = 0; time <= tmax; ++time) {
        G[time].clear();
        Chunk[time].clear();
    }
    S.clear();

    for (std::map<EncodedEdge,
                  std::vector<std::pair<int,int>>>::const_iterator it =
             appearanceIntervals.begin();
         it != appearanceIntervals.end(); ++it) {
        const std::vector<std::pair<int,int>>& intervals = it->second;
        if (intervals.empty()) {
            continue;
        }

        int left = std::max(0, intervals.front().first);
        int right = std::min(tmax, intervals.back().second);
        if (left > right) {
            continue;
        }

        S[it->first] = std::make_pair(left, right);
        G[left].push_back(RES(it->first, right));
        Chunk[left / len].push_back(RES(it->first, right));
    }

    for (int time = 0; time <= tmax; ++time) {
        std::sort(G[time].begin(), G[time].end(), cmpReverse);
    }
    for (int block = 0; block <= tmax / len; ++block) {
        std::sort(Chunk[block].begin(), Chunk[block].end(), cmpReverse);
    }
}

OptimizedIndex * OptimizedIndex::buildSingleEdgeExperiment(
    TemporalGraph * Graph,
    double a,
    int b,
    SingleEdgeMode mode) {

    if (Graph == nullptr || Graph->numOfVertices() <= 0 ||
        Graph->tmax < 0) {
        return nullptr;
    }

    double ratio = std::max(0.0, std::min(1.0, a));
    std::size_t storedEdgeCount = 0;
    for (int time = 0; time <= Graph->tmax; ++time) {
        if (time >= int(Graph->temporal_edge.size())) {
            break;
        }
        storedEdgeCount += Graph->temporal_edge[time].size();
    }

    std::vector<StreamEdge> stream;
    stream.reserve(storedEdgeCount);
    for (int time = 0; time <= Graph->tmax; ++time) {
        if (time >= int(Graph->temporal_edge.size())) {
            break;
        }
        const std::vector<std::pair<int,int>>& bucket =
            Graph->temporal_edge[time];
        for (std::vector<std::pair<int,int>>::const_iterator it =
                 bucket.begin(); it != bucket.end(); ++it) {
            StreamEdge edge;
            edge.u = it->first;
            edge.v = it->second;
            edge.t = time;
            stream.push_back(edge);
        }
    }

    // The chronological stream now owns the materialized input edges for
    // this experiment.  Keep only metadata on Graph afterwards; otherwise
    // large-prefix experiments keep an unnecessary full extra edge copy.
    std::vector<std::vector<std::pair<int,int>>>().swap(Graph->temporal_edge);

    long long totalEdgesForPrefix = Graph->numOfTotalEdges();
    if (totalEdgesForPrefix < 0) {
        totalEdgesForPrefix = static_cast<long long>(stream.size());
    }
    std::size_t prefixCount = std::size_t(
        std::floor(ratio * double(totalEdgesForPrefix) + 1e-12));
    if (prefixCount > stream.size()) {
        std::cout << "[Single-edge experiment] Requested prefix "
                  << prefixCount << " exceeds materialized edge stream "
                  << stream.size() << "; clamping to materialized edges."
                  << std::endl;
        prefixCount = stream.size();
    }
    int requestedUpdates = std::max(0, b);
    std::size_t updateCount = std::min(
        std::size_t(requestedUpdates), stream.size() - prefixCount);

    const char *modeLabel = "RES-Single";
    if (mode == RES_ET_SINGLE_EDGE) {
        modeLabel = "RES-ET";
    }
    else if (mode == ERES_ET_SINGLE_EDGE) {
        modeLabel = "ERES-ET";
    }
    else if (mode == ERES_ET_NO_PRUNE_SINGLE_EDGE) {
        modeLabel = "ERES-ET-NoPrune";
    }
    else if (mode == ERES_SINGLE_EDGE) {
        modeLabel = "ERES";
    }
    else if (mode == ERES_BATCH) {
        modeLabel = "ERES-Batch";
    }
    else if (mode == ORIGINAL_BATCH) {
        modeLabel = "RES-Batch";
    }

    const bool usesReversePrefix =
        mode == ERES_ET_SINGLE_EDGE ||
        mode == ERES_ET_NO_PRUNE_SINGLE_EDGE ||
        mode == ERES_SINGLE_EDGE ||
        mode == ERES_BATCH;

    std::cout << "[" << modeLabel
              << "][Stage 1/2] Initial batch construction starts. "
              << "Initial edges = " << prefixCount << " / "
              << stream.size() << "." << std::endl;
    unsigned long long initialBuildStart = currentTime();

    TemporalGraph prefixGraph;
    prefixGraph.n = Graph->numOfVertices();
    prefixGraph.m = int(prefixCount);
    prefixGraph.tmax = Graph->tmax;
    prefixGraph.is_directed = true;
    prefixGraph.is_general = true;
    prefixGraph.total_m = Graph->numOfTotalEdges();
    prefixGraph.original_vertex_ids = Graph->original_vertex_ids;
    if (!usesReversePrefix) {
        prefixGraph.temporal_edge.resize(Graph->tmax + 1);
    }

    std::vector<std::vector<std::pair<int,int>>> activeEdges(
        Graph->tmax + 1);
    for (std::size_t i = 0; i < prefixCount; ++i) {
        if (!usesReversePrefix) {
            prefixGraph.temporal_edge[stream[i].t].push_back(
                std::make_pair(stream[i].u, stream[i].v));
        }
        activeEdges[stream[i].t].push_back(
            std::make_pair(stream[i].u, stream[i].v));
    }
    std::cout << "[" << modeLabel
              << "][Stage 1/2] Prefix graph loaded with "
              << prefixCount << " initial edges."
              << std::endl;

    int prefixMaximumTime =
        prefixCount == 0 ? 0 : stream[prefixCount - 1].t;
    double constructorFraction = 1.0;
    if (Graph->tmax > 0) {
        constructorFraction = std::min(
            1.0,
            (double(prefixMaximumTime) + 0.5) /
                double(Graph->tmax));
    }

    OptimizedIndex *Index = nullptr;
    if (usesReversePrefix) {
        TemporalGraph reversePrefixGraph;
        reversePrefixGraph.n = Graph->numOfVertices();
        reversePrefixGraph.m = int(prefixCount);
        reversePrefixGraph.tmax = prefixMaximumTime;
        reversePrefixGraph.is_directed = true;
        reversePrefixGraph.is_general = true;
        reversePrefixGraph.total_m = Graph->numOfTotalEdges();
        reversePrefixGraph.original_vertex_ids = Graph->original_vertex_ids;
        reversePrefixGraph.temporal_edge.resize(prefixMaximumTime + 1);
        for (int time = 0; time <= prefixMaximumTime; ++time) {
            reversePrefixGraph.temporal_edge[time] = activeEdges[time];
        }

        std::cout << "[" << modeLabel
                  << "][Stage 1/2] Prefix graph prepared. "
                  << "Starting reverse RES batch constructor..."
                  << std::endl;
        Index = OptimizedIndex::buildReverse(&reversePrefixGraph, 1.0);
        if (Index == nullptr) {
            return nullptr;
        }

        Index->tmax = Graph->tmax;
        Index->t1 = prefixMaximumTime;
        Index->len = std::max(
            1, int(std::sqrt(double(std::max(1, Index->tmax)))));

        delete [] Index->G;
        delete [] Index->Chunk;
        delete [] Index->actual_time;
        Index->G = new std::vector<RES>[Index->tmax + 1]();
        Index->Chunk = new std::vector<RES>[Index->tmax + 1]();
        Index->actual_time = new std::vector<int>[Index->tmax + 1]();
        Index->rebuildReverseStorageCollapsed();

        unsigned long long reverseConstructorEnd = currentTime();
        std::cout << "[" << modeLabel
                  << "][Stage 1/2] Reverse RES batch constructor finished in "
                  << timeFormatting(
                         reverseConstructorEnd - initialBuildStart).str()
                  << "." << std::endl;
    }
    else {
        std::cout << "[" << modeLabel
                  << "][Stage 1/2] Prefix graph prepared. "
                  << "Starting original RES batch constructor..."
                  << std::endl;
        Index = new OptimizedIndex(&prefixGraph, constructorFraction);
        unsigned long long originalConstructorEnd = currentTime();
        std::cout << "[" << modeLabel
                  << "][Stage 1/2] Original RES batch constructor finished in "
                  << timeFormatting(
                         originalConstructorEnd - initialBuildStart).str()
                  << "." << std::endl;
    }

    std::vector<std::vector<std::pair<int,int>>>().swap(prefixGraph.temporal_edge);

    Index->m = int(prefixCount);
    Index->originalVertexIds = Graph->original_vertex_ids;
    Index->lastUpdatedEdgeCount = 0;
    Index->lastUpdateTimeMicros = 0;
    Index->lastMaterializationTimeMicros = 0;

    std::size_t updateProgressStep =
        updateCount == 0 ? std::size_t(1) :
        std::max<std::size_t>(std::size_t(1), updateCount / 10);
    auto printUpdateStart = [&]() {
        std::cout << "[" << modeLabel
                  << "][Stage 2/2] Single-edge incremental update starts. "
                  << "Requested b = " << requestedUpdates
                  << ", actual updates = " << updateCount << "."
                  << std::endl;
        if (updateCount == 0) {
            std::cout << "[" << modeLabel
                      << "][Stage 2/2] No edges need to be updated."
                      << std::endl;
        }
    };
    auto printUpdateProgress = [&](
        std::size_t processed,
        const StreamEdge& edge,
        unsigned long long stageStart) {
        if (processed == updateCount ||
            processed % updateProgressStep == 0) {
            std::cout << "[" << modeLabel
                      << "][Stage 2/2] Updated edges: "
                      << processed << " / " << updateCount
                      << ", current edge = (" << edge.u << ","
                      << edge.v << "," << edge.t << ")"
                      << ", elapsed = "
                      << timeFormatting(currentTime() - stageStart).str()
                      << "." << std::endl;
        }
    };
    auto printUpdateDone = [&](unsigned long long stageStart) {
        std::cout << "[" << modeLabel
                  << "][Stage 2/2] Single-edge incremental update finished. "
                  << "Processed " << Index->lastUpdatedEdgeCount
                  << " edges in "
                  << timeFormatting(currentTime() - stageStart).str()
                  << "." << std::endl;
    };

    if (mode == ERES_BATCH) {
        std::cout << "[" << modeLabel
                  << "][Stage 1/2] Initial batch construction completed in "
                  << timeFormatting(currentTime() - initialBuildStart).str()
                  << "." << std::endl;
        std::cout << "[" << modeLabel
                  << "][Stage 2/2] Reverse batch update starts. "
                      << "Requested b = " << requestedUpdates
                      << ", actual batch edges = " << updateCount
                      << "; diagonal SCCID pruning is ENABLED"
                  << "." << std::endl;

        unsigned long long updateStageStart = currentTime();
        int activeHorizon = prefixMaximumTime;

        if (updateCount > 0) {
            int minUpdatedTime = stream[prefixCount].t;
            int newActiveHorizon = activeHorizon;
            for (std::size_t offset = 0; offset < updateCount; ++offset) {
                const StreamEdge& newEdge = stream[prefixCount + offset];
                minUpdatedTime = std::min(minUpdatedTime, newEdge.t);
                newActiveHorizon = std::max(newActiveHorizon, newEdge.t);
                activeEdges[newEdge.t].push_back(
                    std::make_pair(newEdge.u, newEdge.v));
            }

            if (minUpdatedTime > activeHorizon + 1) {
                std::set<EncodedEdge> previousPhi =
                    Index->recoverPhiAt(activeHorizon);
                for (int end = activeHorizon + 1;
                     end < minUpdatedTime; ++end) {
                    Index->addPhiMembershipAt(end, previousPhi);
                }
            }

            int left = std::max(0, minUpdatedTime);
            int right = std::max(left, newActiveHorizon);
            std::cout << "[" << modeLabel
                      << "][Stage 2/2] Recomputing affected end-family range ["
                      << left << "," << right << "] with "
                      << updateCount << " newly inserted edges."
                      << std::endl;

            unsigned long long phiStart = currentTime();
            unsigned long long diagonalStats[15] = {};
            std::map<int, std::set<EncodedEdge>> phiByEnd =
                computeReverseConstructorPhiRangeDiagonalPruned(
                    Index->n,
                    activeEdges,
                    left,
                    right,
                    right,
                    diagonalStats);
            unsigned long long phiMicros = currentTime() - phiStart;

            unsigned long long membershipStart = currentTime();
            Index->removePhiMembershipRange(left, right);
            for (int end = left; end <= right; ++end) {
                std::map<int, std::set<EncodedEdge>>::const_iterator phiIt =
                    phiByEnd.find(end);
                if (phiIt != phiByEnd.end()) {
                    Index->addPhiMembershipAt(end, phiIt->second);
                }
            }
            unsigned long long membershipMicros =
                currentTime() - membershipStart;

            Index->lastUpdatedEdgeCount = int(updateCount);
            Index->lastUpdateTimeMicros = currentTime() - updateStageStart;
            Index->m = int(prefixCount + updateCount);
            Index->t1 = newActiveHorizon;
            activeHorizon = newActiveHorizon;

            std::cout << "[" << modeLabel
                      << "][Stage 2/2] Affected reverse families recomputed in "
                      << timeFormatting(phiMicros).str()
                      << "; membership replacement = "
                      << timeFormatting(membershipMicros).str()
                      << "." << std::endl;
            {
                double pruneRatio = diagonalStats[0] == 0 ? 0.0 :
                    100.0 * double(diagonalStats[1]) /
                    double(diagonalStats[0]);
                double internalDropRatio = diagonalStats[7] == 0 ? 0.0 :
                    100.0 * double(diagonalStats[9]) /
                    double(diagonalStats[7]);
                double uniquePruneRatio = updateCount == 0 ? 0.0 :
                    100.0 * double(diagonalStats[11]) /
                    double(updateCount);
                std::cout << "[" << modeLabel
                          << "][Diagonal pruning] end anchors built = "
                          << diagonalStats[4]
                          << "; SCCID checks = " << diagonalStats[0]
                          << "; deferred/pruned events = "
                          << diagonalStats[1]
                          << " (" << std::fixed << std::setprecision(2)
                          << pruneRatio << "%)"
                          << "; fresh prune events = " << diagonalStats[5]
                          << "; repeated deferred prune events = "
                          << diagonalStats[6]
                          << "; deferred activations = "
                          << diagonalStats[2]
                          << "; max deferred bucket size = "
                          << diagonalStats[3]
                          << "." << std::endl;
                std::cout << "[" << modeLabel
                          << "][Internal non-RES pruning] redundant internal non-RES events = "
                          << diagonalStats[7]
                          << "; safe same-end deletions = "
                          << diagonalStats[9]
                          << " (" << std::fixed << std::setprecision(2)
                          << internalDropRatio << "% of internal non-RES events)"
                          << "; temporarily retained for smaller end anchors = "
                          << diagonalStats[10]
                          << "; internal RES-selected events = "
                          << diagonalStats[8]
                          << "." << std::endl;
                if (diagonalStats[14] != 0) {
                    std::cout << "[" << modeLabel
                              << "][Internal non-RES pruning] unique pruned edges = "
                              << diagonalStats[11]
                              << " (" << std::fixed << std::setprecision(2)
                              << uniquePruneRatio
                              << "% of newly inserted edges)"
                              << "; unique same-end permanent drops = "
                              << diagonalStats[12]
                              << "; unique temporary retained edges = "
                              << diagonalStats[13]
                              << "." << std::endl;
                }
                else {
                    std::cout << "[" << modeLabel
                              << "][Internal non-RES pruning] unique-event diagnostics disabled for this large construction; pruning behavior is unchanged."
                              << std::endl;
                }
            }
        }
        else {
            Index->lastUpdatedEdgeCount = 0;
            Index->lastUpdateTimeMicros = 0;
        }

        std::cout << "[" << modeLabel
                  << "][Stage 2/2] Reverse batch update finished in "
                  << timeFormatting(currentTime() - updateStageStart).str()
                  << "." << std::endl;

        if (activeHorizon < Index->tmax) {
            std::cout << "[" << modeLabel
                      << "][Stage 2/2] Extending final maintained end-family from t_e="
                      << activeHorizon << " to full query horizon tmax="
                      << Index->tmax << "..."
                      << std::endl;
            std::set<EncodedEdge> finalPhi =
                Index->recoverPhiAt(activeHorizon);
            for (int end = activeHorizon + 1;
                 end <= Index->tmax; ++end) {
                Index->addPhiMembershipAt(end, finalPhi);
            }
        }

        std::cout << "[" << modeLabel
                  << "][Stage 2/2] Materializing collapsed final reverse G/Chunk..."
                  << std::endl;
        unsigned long long materializationStart = currentTime();
        Index->rebuildReverseStorageCollapsed();
        Index->lastMaterializationTimeMicros =
            currentTime() - materializationStart;
        std::cout << "[" << modeLabel
                  << "][Stage 2/2] Collapsed final reverse G/Chunk materialized in "
                  << timeFormatting(Index->lastMaterializationTimeMicros).str()
                  << "." << std::endl;
        return Index;
    }

    if (mode == ORIGINAL_BATCH) {
        std::cout << "[" << modeLabel
                  << "][Stage 1/2] Initial batch construction completed in "
                  << timeFormatting(currentTime() - initialBuildStart).str()
                  << "." << std::endl;
        std::cout << "[" << modeLabel
                  << "][Stage 2/2] Original RES timestamp-batch update starts. "
                  << "Requested b = " << requestedUpdates
                  << ", actual batch edges = " << updateCount << "."
                  << std::endl;

        unsigned long long updateStageStart = currentTime();
        if (updateCount > 0) {
            int oldActiveHorizon = Index->t1;
            int newActiveHorizon = oldActiveHorizon;
            for (std::size_t offset = 0; offset < updateCount; ++offset) {
                const StreamEdge& edgeToAdd = stream[prefixCount + offset];
                newActiveHorizon =
                    std::max(newActiveHorizon, edgeToAdd.t);
                activeEdges[edgeToAdd.t].push_back(
                    std::make_pair(edgeToAdd.u, edgeToAdd.v));
            }

            TemporalGraph updateGraph;
            updateGraph.n = Graph->numOfVertices();
            updateGraph.m = int(prefixCount + updateCount);
            updateGraph.tmax = Graph->tmax;
            updateGraph.is_directed = true;
            updateGraph.is_general = true;
            updateGraph.total_m = Graph->numOfTotalEdges();
            updateGraph.original_vertex_ids = Graph->original_vertex_ids;
            updateGraph.temporal_edge.resize(Graph->tmax + 1);
            for (std::size_t i = 0; i < prefixCount + updateCount; ++i) {
                const StreamEdge& edgeInGraph = stream[i];
                updateGraph.temporal_edge[edgeInGraph.t].push_back(
                    std::make_pair(edgeInGraph.u, edgeInGraph.v));
            }

            std::cout << "[" << modeLabel
                      << "][Stage 2/2] Calling original modify over horizon ["
                      << oldActiveHorizon << "," << newActiveHorizon
                      << "]. Same-timestamp edges are processed together by "
                      << "Graph->temporal_edge[t]." << std::endl;

            Index->modify(&updateGraph, oldActiveHorizon, newActiveHorizon);
            Index->m = int(prefixCount + updateCount);
            Index->t1 = newActiveHorizon;
            Index->lastUpdatedEdgeCount = int(updateCount);
            Index->lastUpdateTimeMicros =
                currentTime() - updateStageStart;
        }
        else {
            Index->lastUpdatedEdgeCount = 0;
            Index->lastUpdateTimeMicros = 0;
        }
        Index->lastMaterializationTimeMicros = 0;

        std::cout << "[" << modeLabel
                  << "][Stage 2/2] Original RES timestamp-batch update finished in "
                  << timeFormatting(currentTime() - updateStageStart).str()
                  << "." << std::endl;
        return Index;
    }

    if (mode == ORIGINAL_SINGLE_EDGE) {
        std::cout << "[" << modeLabel
                  << "][Stage 1/2] Initial batch construction completed in "
                  << timeFormatting(currentTime() - initialBuildStart).str()
                  << "." << std::endl;
        printUpdateStart();
        unsigned long long updateStageStart = currentTime();
        for (std::size_t offset = 0; offset < updateCount; ++offset) {
            const StreamEdge& newEdge = stream[prefixCount + offset];
            unsigned long long updateStart = currentTime();

            Index->modifySingleEdgeOriginalFlow(
                activeEdges, newEdge.u, newEdge.v, newEdge.t);

            activeEdges[newEdge.t].push_back(
                std::make_pair(newEdge.u, newEdge.v));
            Index->lastUpdateTimeMicros +=
                currentTime() - updateStart;
            ++Index->lastUpdatedEdgeCount;
            Index->m =
                int(prefixCount) + Index->lastUpdatedEdgeCount;
            Index->t1 = std::max(Index->t1, newEdge.t);
            printUpdateProgress(offset + 1, newEdge, updateStageStart);
        }
        printUpdateDone(updateStageStart);
        std::cout << "[" << modeLabel
                  << "][Stage 2/2] Materializing maintained forward RES index..."
                  << std::endl;
        unsigned long long materializationStart = currentTime();
        Index->rebuildForwardStorage();
        Index->lastMaterializationTimeMicros =
            currentTime() - materializationStart;
        std::cout << "[" << modeLabel
                  << "][Stage 2/2] Maintained forward RES index materialized in "
                  << timeFormatting(
                         Index->lastMaterializationTimeMicros).str()
                  << "."
                  << std::endl;
        return Index;
    }

    if (mode == ERES_ET_SINGLE_EDGE ||
        mode == ERES_ET_NO_PRUNE_SINGLE_EDGE ||
        mode == ERES_SINGLE_EDGE) {
        const bool enableIntraSccPruning =
            (mode != ERES_ET_NO_PRUNE_SINGLE_EDGE);
        const bool enableInfluenceInterval =
            (mode != ERES_SINGLE_EDGE);
        std::cout << "[" << modeLabel
                  << "][Stage 1/2] Initial batch construction completed in "
                  << timeFormatting(currentTime() - initialBuildStart).str()
                  << "." << std::endl;
        std::cout << "[" << modeLabel
                  << "][Pruning] Intra-SCC non-RES edge pruning is "
                  << (enableIntraSccPruning ? "ENABLED" : "DISABLED")
                  << "." << std::endl;
        printUpdateStart();
        unsigned long long updateStageStart = currentTime();
        int activeHorizon = prefixMaximumTime;
        ERESPruningStats pruningStats;
        unsigned long long eresIntervalMicros = 0;
        unsigned long long eresSuperIncrementMicros = 0;
        unsigned long long eresFallbackMicros = 0;
        unsigned long long eresMembershipMicros = 0;
        unsigned long long eresHorizonExtendMicros = 0;
        unsigned long long eresEmptyIntervalCount = 0;
        unsigned long long eresNonEmptyIntervalCount = 0;
        unsigned long long eresStartAnchorCount = 0;
        unsigned long long eresIntervalLengthSum = 0;
        unsigned long long eresMaxIntervalLength = 0;
        unsigned long long eresFallbackCount = 0;
        for (std::size_t offset = 0; offset < updateCount; ++offset) {
            const StreamEdge& newEdge = stream[prefixCount + offset];
            unsigned long long updateStart = currentTime();
            int newActiveHorizon = std::max(activeHorizon, newEdge.t);

            if (newActiveHorizon > activeHorizon) {
                unsigned long long horizonStart = currentTime();
                std::set<EncodedEdge> previousPhi =
                    Index->recoverPhiAt(activeHorizon);
                for (int end = activeHorizon + 1;
                     end <= newActiveHorizon; ++end) {
                    Index->addPhiMembershipAt(end, previousPhi);
                }
                eresHorizonExtendMicros += currentTime() - horizonStart;
            }

            std::pair<int,int> interval;
            if (enableInfluenceInterval) {
                unsigned long long intervalStart = currentTime();
                interval = computeForwardResInfluenceInterval(
                    Index->n, activeEdges, newEdge);
                eresIntervalMicros += currentTime() - intervalStart;
            }
            else {
                interval = std::make_pair(
                    0, std::min(newActiveHorizon, newEdge.t));
            }

            std::size_t phiLimit =
                std::size_t(std::max(0, 2 * Index->n - 2));
            if (interval.first >= 0 &&
                interval.second >= interval.first) {
                int left = std::max(0, interval.first);
                int right = std::min(newActiveHorizon, interval.second);
                right = std::min(right, newEdge.t);
                if (left <= right) {
                    unsigned long long length =
                        static_cast<unsigned long long>(right - left + 1);
                    ++eresNonEmptyIntervalCount;
                    eresStartAnchorCount += length;
                    eresIntervalLengthSum += length;
                    eresMaxIntervalLength =
                        std::max(eresMaxIntervalLength, length);

                    std::set<EncodedEdge> previousPhi =
                        Index->recoverPhiAt(newActiveHorizon);

                    activeEdges[newEdge.t].push_back(
                        std::make_pair(newEdge.u, newEdge.v));

                    unsigned long long superIncrementStart = currentTime();
                    std::set<EncodedEdge> updatedPhi =
                        computeERESIncrementalPhiForStartRange(
                            Index->n,
                            activeEdges,
                            newActiveHorizon,
                            left,
                            right,
                            previousPhi,
                            enableIntraSccPruning,
                            &pruningStats);
                    eresSuperIncrementMicros +=
                        currentTime() - superIncrementStart;

                    if (updatedPhi.size() > phiLimit) {
                        unsigned long long fallbackStart = currentTime();
                        std::map<int, std::set<EncodedEdge>> phiByEnd =
                            computeReverseConstructorPhiRange(
                                Index->n,
                                activeEdges,
                                newActiveHorizon,
                                newActiveHorizon,
                                newActiveHorizon);
                        std::map<int, std::set<EncodedEdge>>::const_iterator
                            phiIt = phiByEnd.find(newActiveHorizon);
                        if (phiIt != phiByEnd.end()) {
                            updatedPhi = phiIt->second;
                        }
                        ++eresFallbackCount;
                        eresFallbackMicros +=
                            currentTime() - fallbackStart;
                    }

                    unsigned long long membershipStart = currentTime();
                    Index->removePhiMembershipRange(
                        newActiveHorizon, newActiveHorizon);
                    Index->addPhiMembershipAt(newActiveHorizon, updatedPhi);
                    eresMembershipMicros +=
                        currentTime() - membershipStart;
                }
                else {
                    ++eresEmptyIntervalCount;
                    activeEdges[newEdge.t].push_back(
                        std::make_pair(newEdge.u, newEdge.v));
                }
            }
            else {
                ++eresEmptyIntervalCount;
                activeEdges[newEdge.t].push_back(
                    std::make_pair(newEdge.u, newEdge.v));
            }

            activeHorizon = newActiveHorizon;
            Index->lastUpdateTimeMicros +=
                currentTime() - updateStart;
            ++Index->lastUpdatedEdgeCount;
            Index->m =
                int(prefixCount) + Index->lastUpdatedEdgeCount;
            Index->t1 = activeHorizon;
            printUpdateProgress(offset + 1, newEdge, updateStageStart);
        }
        printUpdateDone(updateStageStart);

        std::cout << "[" << modeLabel << "][Profile] ";
        if (enableInfluenceInterval) {
            std::cout
                << "Forward effective interval calls: " << updateCount
                << "; empty intervals = " << eresEmptyIntervalCount
                << "; non-empty intervals = " << eresNonEmptyIntervalCount;
        }
        else {
            std::cout
                << "SCC-formation influence-interval pruning is DISABLED"
                << "; full-range incremental updates = "
                << eresNonEmptyIntervalCount;
        }
        std::cout
            << "; incrementally traversed start anchors = "
            << eresStartAnchorCount
            << "; safety fallbacks = " << eresFallbackCount;
        if (enableInfluenceInterval) {
            std::cout << "; average non-empty interval length = ";
        }
        else {
            std::cout << "; average full-range traversal length = ";
        }
        if (eresNonEmptyIntervalCount == 0) {
            std::cout << "0";
        }
        else {
            std::cout
                << double(eresIntervalLengthSum) /
                   double(eresNonEmptyIntervalCount);
        }
        std::cout
            << "; max interval length = " << eresMaxIntervalLength
            << "." << std::endl;
        std::cout
            << "[" << modeLabel
            << "][Profile] Horizon extension time = "
            << timeFormatting(eresHorizonExtendMicros).str();
        if (enableInfluenceInterval) {
            std::cout << "; forward interval computation time = ";
        }
        else {
            std::cout << "; influence interval computation time (disabled) = ";
        }
        std::cout
            << timeFormatting(eresIntervalMicros).str()
            << "; incremental supergraph construction time = "
            << timeFormatting(eresSuperIncrementMicros).str()
            << "; safety fallback time = "
            << timeFormatting(eresFallbackMicros).str()
            << "; membership replace time = "
            << timeFormatting(eresMembershipMicros).str()
            << "." << std::endl;

        unsigned long long totalNonResPruned =
            pruningStats.initialInternalNonResPruned +
            pruningStats.shrinkInternalNonResPruned;
        std::set<EncodedEdge> uniqueTotalInternalNonRes =
            pruningStats.uniqueInitialInternalNonRes;
        uniqueTotalInternalNonRes.insert(
            pruningStats.uniqueShrinkInternalNonRes.begin(),
            pruningStats.uniqueShrinkInternalNonRes.end());
        std::size_t uniqueInitialPruned =
            pruningStats.uniqueInitialInternalNonRes.size();
        std::size_t uniqueShrinkPruned =
            pruningStats.uniqueShrinkInternalNonRes.size();
        std::size_t uniqueTotalPruned =
            uniqueTotalInternalNonRes.size();
        std::size_t finalActiveEdges =
            prefixCount + std::size_t(Index->lastUpdatedEdgeCount);
        double shrinkPruneEventRatio =
            updateCount == 0 ? 0.0 :
            100.0 * double(pruningStats.shrinkInternalNonResPruned) /
            double(updateCount);
        double totalPruneEventRatio =
            updateCount == 0 ? 0.0 :
            100.0 * double(totalNonResPruned) /
            double(updateCount);
        double uniqueShrinkUpdateRatio =
            updateCount == 0 ? 0.0 :
            100.0 * double(uniqueShrinkPruned) / double(updateCount);
        double uniqueTotalUpdateRatio =
            updateCount == 0 ? 0.0 :
            100.0 * double(uniqueTotalPruned) / double(updateCount);
        double uniqueTotalActiveRatio =
            finalActiveEdges == 0 ? 0.0 :
            100.0 * double(uniqueTotalPruned) /
            double(finalActiveEdges);
        std::cout
            << "[" << modeLabel
            << "][Pruning] Initial intra-SCC non-RES edges "
            << (enableIntraSccPruning ? "pruned" : "kept")
            << " = " << pruningStats.initialInternalNonResPruned
            << " events, unique = " << uniqueInitialPruned
            << "; initial intra-SCC RES-selected edges = "
            << pruningStats.initialInternalResSelected
            << "." << std::endl;
        std::cout
            << "[" << modeLabel
            << "][Pruning] Sec IV.D shrink intra-SCC non-RES edges "
            << (enableIntraSccPruning ? "pruned" : "kept")
            << " = " << pruningStats.shrinkInternalNonResPruned
            << " events, unique = " << uniqueShrinkPruned
            << "; shrink intra-SCC RES-selected edges = "
            << pruningStats.shrinkInternalResSelected
            << "; event-ratio(shrink-events/update-edges) = "
            << std::fixed << std::setprecision(2)
            << shrinkPruneEventRatio
            << "%; unique-ratio(unique-shrink/update-edges) = "
            << uniqueShrinkUpdateRatio << "%." << std::endl;
        std::cout
            << "[" << modeLabel
            << "][Pruning] Total intra-SCC non-RES edges "
            << (enableIntraSccPruning ? "pruned" : "kept")
            << " = " << totalNonResPruned
            << " events, unique = " << uniqueTotalPruned
            << "; event-ratio(total-events/update-edges) = "
            << std::fixed << std::setprecision(2)
            << totalPruneEventRatio
            << "%; unique-ratio(unique-total/update-edges) = "
            << uniqueTotalUpdateRatio
            << "%; unique-ratio(unique-total/active-edges) = "
            << uniqueTotalActiveRatio
            << "%; arrival already internal = "
            << pruningStats.arrivalAlreadyInternal
            << "; reachability redundant skipped = "
            << pruningStats.reachabilityRedundantSkipped
            << "; duplicate super-edge skipped = "
            << pruningStats.duplicateSuperEdgeSkipped
            << "." << std::endl;

        std::set<EncodedEdge> finalPhi =
            Index->recoverPhiAt(activeHorizon);
        for (int end = activeHorizon + 1;
             end <= Index->tmax; ++end) {
            Index->addPhiMembershipAt(end, finalPhi);
        }

        std::cout << "[" << modeLabel
                  << "][Stage 2/2] Materializing collapsed final reverse G/Chunk..."
                  << std::endl;
        unsigned long long materializationStart = currentTime();
        Index->rebuildReverseStorageCollapsed();
        Index->lastMaterializationTimeMicros =
            currentTime() - materializationStart;
        std::cout << "[" << modeLabel
                  << "][Stage 2/2] Collapsed final reverse G/Chunk materialized in "
                  << timeFormatting(Index->lastMaterializationTimeMicros).str()
                  << "." << std::endl;
        return Index;
    }

    std::cout << "[" << modeLabel
              << "][Stage 1/2] Initial batch construction completed in "
              << timeFormatting(currentTime() - initialBuildStart).str()
              << "." << std::endl;
    printUpdateStart();
    unsigned long long updateStageStart = currentTime();
    unsigned long long intervalMicros = 0;
    unsigned long long restrictedUpdateMicros = 0;
    unsigned long long emptyIntervalCount = 0;
    unsigned long long nonEmptyIntervalCount = 0;
    unsigned long long traversedStartAnchors = 0;
    unsigned long long maximumIntervalLength = 0;
    unsigned long long sameTimestampIntervalGuards = 0;
    for (std::size_t offset = 0; offset < updateCount; ++offset) {
        const StreamEdge& newEdge = stream[prefixCount + offset];
        unsigned long long updateStart = currentTime();

        unsigned long long intervalStart = currentTime();
        if (newEdge.t >= 0 && newEdge.t < int(activeEdges.size()) &&
            !activeEdges[newEdge.t].empty()) {
            ++sameTimestampIntervalGuards;
        }
        std::pair<int,int> interval = computeForwardResInfluenceInterval(
            Index->n, activeEdges, newEdge);
        intervalMicros += currentTime() - intervalStart;
        int left = interval.first;
        int right = interval.second;

        if (left >= 0 && right >= left) {
            ++nonEmptyIntervalCount;
            unsigned long long intervalLength =
                static_cast<unsigned long long>(right - left + 1);
            traversedStartAnchors += intervalLength;
            maximumIntervalLength = std::max(
                maximumIntervalLength, intervalLength);
            unsigned long long restrictedUpdateStart = currentTime();
            Index->modifySingleEdgeOriginalFlowRestricted(
                activeEdges, newEdge.u, newEdge.v, newEdge.t,
                left, right);
            restrictedUpdateMicros +=
                currentTime() - restrictedUpdateStart;
        }
        else {
            ++emptyIntervalCount;
        }

        activeEdges[newEdge.t].push_back(
            std::make_pair(newEdge.u, newEdge.v));
        Index->lastUpdateTimeMicros += currentTime() - updateStart;
        ++Index->lastUpdatedEdgeCount;
        Index->m = int(prefixCount) + Index->lastUpdatedEdgeCount;
        Index->t1 = std::max(Index->t1, newEdge.t);
        printUpdateProgress(offset + 1, newEdge, updateStageStart);
    }
    printUpdateDone(updateStageStart);
    double averageIntervalLength = nonEmptyIntervalCount == 0 ? 0.0 :
        double(traversedStartAnchors) / double(nonEmptyIntervalCount);
    std::cout << "[" << modeLabel
              << "][Profile] SCC-formation influence-interval computation = "
              << timeFormatting(intervalMicros).str()
              << "; restricted forward RES update = "
              << timeFormatting(restrictedUpdateMicros).str()
              << "; empty intervals = " << emptyIntervalCount
              << "; non-empty intervals = " << nonEmptyIntervalCount
              << "; traversed start anchors = " << traversedStartAnchors
              << "; average non-empty interval length = "
              << std::fixed << std::setprecision(2)
              << averageIntervalLength
              << "; maximum interval length = "
              << maximumIntervalLength
              << "; same-timestamp exact interval guards = "
              << sameTimestampIntervalGuards << "." << std::endl;

    std::cout << "[" << modeLabel
              << "][Stage 2/2] Materializing maintained forward RES index..."
              << std::endl;
    unsigned long long materializationStart = currentTime();
    Index->rebuildForwardStorage();
    Index->lastMaterializationTimeMicros =
        currentTime() - materializationStart;
    std::cout << "[" << modeLabel
              << "][Stage 2/2] Maintained forward RES index materialized in "
              << timeFormatting(Index->lastMaterializationTimeMicros).str()
              << "." << std::endl;

    return Index;
}

OptimizedIndex * OptimizedIndex::buildOriginalSingleEdge(
    TemporalGraph * Graph, double a, int b) {
    return buildSingleEdgeExperiment(
        Graph, a, b, ORIGINAL_SINGLE_EDGE);
}

OptimizedIndex * OptimizedIndex::buildRESWithETSingleEdge(
    TemporalGraph * Graph, double a, int b) {
    return buildSingleEdgeExperiment(
        Graph, a, b, RES_ET_SINGLE_EDGE);
}

OptimizedIndex * OptimizedIndex::buildOriginalBatch(
    TemporalGraph * Graph, double a, int b) {
    return buildSingleEdgeExperiment(
        Graph, a, b, ORIGINAL_BATCH);
}

OptimizedIndex * OptimizedIndex::buildERESWithETSingleEdge(
    TemporalGraph * Graph, double a, int b) {
    return buildSingleEdgeExperiment(
        Graph, a, b, ERES_ET_SINGLE_EDGE);
}

OptimizedIndex * OptimizedIndex::buildERESWithETNoPruneSingleEdge(
    TemporalGraph * Graph, double a, int b) {
    return buildSingleEdgeExperiment(
        Graph, a, b, ERES_ET_NO_PRUNE_SINGLE_EDGE);
}

OptimizedIndex * OptimizedIndex::buildERESSingleEdge(
    TemporalGraph * Graph, double a, int b) {
    return buildSingleEdgeExperiment(
        Graph, a, b, ERES_SINGLE_EDGE);
}

OptimizedIndex * OptimizedIndex::buildERESBatch(
    TemporalGraph * Graph, double a, int b) {
    return buildSingleEdgeExperiment(
        Graph, a, b, ERES_BATCH);
}

int OptimizedIndex::timeHorizon() const {

    return tmax;

}

int OptimizedIndex::updatedEdgeCount() const {
    return lastUpdatedEdgeCount;
}

unsigned long long OptimizedIndex::updateTimeMicros() const {
    return lastUpdateTimeMicros;
}

unsigned long long OptimizedIndex::materializationTimeMicros() const {
    return lastMaterializationTimeMicros;
}

double OptimizedIndex::averageUpdateTimeMicros() const {
    if (lastUpdatedEdgeCount == 0) {
        return 0.0;
    }
    return double(lastUpdateTimeMicros) /
        double(lastUpdatedEdgeCount);
}

OptimizedIndex::~OptimizedIndex() {
    delete [] Sta;
    delete [] Vis;
    delete [] f;
    delete [] Vis2;
    delete [] G;
    delete [] Chunk;
    delete [] actual_time;
    delete [] outLabel;
    delete [] outLabel2;
    delete [] newedge;
    delete [] edge;

}

std::uint64_t OptimizedIndex::size() {

    unsigned long long memory = 0;
    for (int te = 0; te <= tmax; te++) {
    //     if(actual_time[ts].empty())continue;
    //     int len=actual_time[ts].size();
    //     unsigned long long sz=0;
    //     for (int te = 0; te < len; te++) {
    //         //sz += S[ts][te].size();
    //         sz+=G[ts][te].size();
    //     }
    //    // std::cerr<<sz<<' '<<ts<<'\n';
    //     memory+=sz;
        memory+=G[te].size();
    }
    // std::cout << "number of effective edges: " << cnt << std::endl;
    memory *= 32;
    return memory;

}


void OptimizedIndex::modifySingleEdgeOriginalFlow(
    const std::vector<std::vector<std::pair<int,int>>>& activeEdges,
    int uNew,
    int vNew,
    int tNew) {

    modifySingleEdgeOriginalFlowRestricted(
        activeEdges, uNew, vNew, tNew, 0, tNew);
}

void OptimizedIndex::modifySingleEdgeOriginalFlowRestricted(
    const std::vector<std::vector<std::pair<int,int>>>& activeEdges,
    int uNew,
    int vNew,
    int tNew,
    int left,
    int right) {

    if (tNew < 0 || tNew > tmax) {
        return;
    }

    std::map<std::pair<long long,int>, std::pair<int,int> > mp;
    mp.clear();

    int tim = tNew;
    left = std::max(0, left);
    right = std::min(tim, right);
    if (left > right) {
        return;
    }

    for (int i = left; i <= right; i++) {
        for (int u = 0; u < n; u++) {
            f[u] = u;
            Vis[u] = 0;
            Vis2[u] = 0;
            outLabel2[u].clear();
            outLabel[u].clear();
        }
        key.clear();

        int ts = i, te = tim;
        for (int u = 0; u < n; u++) {
            Vis[u] = 0;
            outLabel[u].clear();
            outLabel2[u].clear();
        }

        std::set<EncodedEdge> previousPhi = recoverPhiAt(ts);
        for (std::set<EncodedEdge>::const_iterator phiIt =
                 previousPhi.begin();
             phiIt != previousPhi.end(); ++phiIt) {
            EncodedEdge g = *phiIt;
            if (g.second < ts || g.second > te) {
                continue;
            }
            int u = edgeSource(g);
            int v = edgeDestination(g);
            outLabel[u].push_back(g);
            outLabel2[v].push_back(g);
        }

        for (int u = 0; u < n; u++) {
            if (!Vis[u]) {
                kosaraju1(u);
            }
        }
        for (int u = 0; u < n; u++) {
            Vis[u] = 0;
        }
        col = 0;
        while (top) {
            int u = Sta[top];
            top--;
            if (Vis2[u]) {
                continue;
            }
            CC.clear();
            col++;
            kosaraju2(u, ts);
            kosaraju4(u, u, ts);
        }

        for (int p = i; p <= tim; p++) {
            if (p < 0 || p >= int(activeEdges.size())) {
                continue;
            }
            for (std::vector<std::pair<int,int>>::const_iterator ed =
                     activeEdges[p].begin();
                 ed != activeEdges[p].end(); ++ed) {
                int u = ed->first;
                int v = ed->second;
                if (find(u) == find(v)) {
                    continue;
                }
                std::pair<long long,int> g =
                    std::make_pair(((((long long)u) << 37) +
                                    (((long long)v) << 12)), p);
                outLabel[find(u)].push_back(g);
                outLabel2[find(v)].push_back(g);
            }
        }

        {
            std::unordered_set<int> point;
            point.clear();
            for (int vertex = 0; vertex < n; vertex++) {
                Vis[vertex] = 0;
                Vis2[vertex] = 0;
            }

            if (uNew >= 0 && uNew < n && vNew >= 0 && vNew < n &&
                find(uNew) != find(vNew)) {
                std::pair<long long,int> g =
                    std::make_pair(((((long long)uNew) << 37) +
                                    (((long long)vNew) << 12)), tNew);
                point.insert(find(uNew));
                point.insert(find(vNew));
                outLabel[find(uNew)].push_back(g);
                outLabel2[find(vNew)].push_back(g);
            }

            top = 0;
            col = 0;
            for (std::unordered_set<int>::iterator it = point.begin();
                 it != point.end(); ++it) {
                if (!Vis[*it]) {
                    kosaraju1(*it);
                }
            }
            for (int u = 0; u < n; u++) {
                Vis[u] = 0;
            }
            markedVertices.clear();
            markedVertices2.clear();

            while (top) {
                int u = Sta[top];
                top--;
                int g = find(u);
                if (!Vis2[g]) {
                    CC.clear();
                    col++;
                    kosaraju2(g, i);
                    kosaraju4(g, g, i);
                    std::vector<std::pair<long long,int>> tmp;
                    tmp.clear();
                    for (std::vector<int>::iterator ccIt = CC.begin();
                         ccIt != CC.end(); ++ccIt) {
                        int member = *ccIt;
                        for (std::vector<std::pair<long long,int>>::iterator
                                 iter = outLabel2[member].begin();
                             iter != outLabel2[member].end(); ++iter) {
                            long long v = iter->first >> 37;
                            if (find(v) != g) {
                                tmp.push_back(*iter);
                            }
                        }
                    }
                    for (std::vector<int>::iterator ccIt = CC.begin();
                         ccIt != CC.end(); ++ccIt) {
                        outLabel2[*ccIt].clear();
                        std::vector<std::pair<long long,int>>()
                            .swap(outLabel2[*ccIt]);
                    }
                    outLabel2[g] = tmp;
                    tmp.clear();
                    for (std::vector<int>::iterator ccIt = CC.begin();
                         ccIt != CC.end(); ++ccIt) {
                        int member = *ccIt;
                        for (std::vector<std::pair<long long,int>>::iterator
                                 iter = outLabel[member].begin();
                             iter != outLabel[member].end(); ++iter) {
                            long long v =
                                (iter->first >> 12) & (33554431ll);
                            if (find(v) != g) {
                                tmp.push_back(*iter);
                            }
                        }
                    }
                    for (std::vector<int>::iterator ccIt = CC.begin();
                         ccIt != CC.end(); ++ccIt) {
                        outLabel[*ccIt].clear();
                        std::vector<std::pair<long long,int>>()
                            .swap(outLabel[*ccIt]);
                    }
                    outLabel[g] = tmp;
                    tmp.clear();
                }
            }
        }

        if (!key.empty()) {
            for (std::set<std::pair<long long,int>>::iterator it =
                     key.begin(); it != key.end(); ++it) {
                std::pair<long long,int> g = *it;
                if (mp.count(g)) {
                    std::pair<int,int> alfa = mp[g];
                    alfa.second = i;
                    mp[g] = alfa;
                }
                else {
                    mp[g] = std::make_pair(i, i);
                }
            }
            key.clear();
        }
    }

    removePhiMembershipRange(left, right);
    for (std::map<EncodedEdge, std::pair<int,int>>::const_iterator state =
             mp.begin(); state != mp.end(); ++state) {
        int intervalLeft = state->second.first;
        int intervalRight = state->second.second;
        std::vector<std::pair<int,int>>& intervals =
            appearanceIntervals[state->first];
        std::vector<std::pair<int,int>>::iterator position =
            intervals.begin();
        while (position != intervals.end() &&
               position->second < intervalLeft - 1) {
            ++position;
        }
        if (position == intervals.end()) {
            intervals.push_back(
                std::make_pair(intervalLeft, intervalRight));
        }
        else if (position->first > intervalRight + 1) {
            intervals.insert(
                position, std::make_pair(intervalLeft, intervalRight));
        }
        else {
            position->first = std::min(position->first, intervalLeft);
            position->second = std::max(position->second, intervalRight);
            std::vector<std::pair<int,int>>::iterator next = position + 1;
            while (next != intervals.end() &&
                   next->first <= position->second + 1) {
                position->second =
                    std::max(position->second, next->second);
                next = intervals.erase(next);
            }
        }
    }
}


void OptimizedIndex::modify(TemporalGraph * Graph,int tpre,int tim){
    if(tpre>tim)return ;
    if(tpre==tim && (Graph==nullptr || Graph->numOfEdges()<=m))return ;
    //std::cerr<<tpre<<' '<<tim<<'\n';
    unsigned long long start_time = currentTime();
    std::map<std::pair<long long,int>, std::pair<int,int> >mp;
    mp.clear();
    
    for(int i=0;i<=tim;i++){
        for(int u=0;u<n;u++){
            f[u]=u;
            Vis[u]=0;
            Vis2[u]=0;
            outLabel2[u].clear();
            outLabel[u].clear();
        }
        key.clear();
        if(i<=tpre){
            int ts=i,te=tpre;
            for (int u = 0; u < n; u++) {
                Vis[u] = 0;
                outLabel[u].clear();
                outLabel2[u].clear();
            }

            int cnt = 0;

            for(int j=ts;j<=te;j++){
                for(int now=0;now<G[j].size();now++){
                    if(G[j][now].ts<=ts){
                        std::pair<long long,int> g=G[j][now].edge;
                        long long u = (g.first >> 37), v = (g.first >> 12) & (33554431ll);
                        if(g.second>te) continue;
                        outLabel[u].push_back(g);
                        outLabel2[v].push_back(g);
                    }
                    else break;
                }
            }
            for(int u=0;u<n;u++){
                if(!Vis[u]){
                    kosaraju1(u);
                }
            }
            for(int u=0;u<n;u++)Vis[u]=0;
            col=0;
            while(top){
                int t=0;
                int u=Sta[top];top--;
                if(Vis2[u])continue;
                CC.clear();
                col++;
                kosaraju2(u,ts);
                kosaraju4(u,u,ts);
                //std::sort(CC.begin(),CC.end());
                //CurrentCC[CC[0]]=CC;
            }    
            // for (int i = 0; i <= ts; i++) {
            //     int idx = find_an_index(i, ts, te);
            //     if (idx == -1) {
            //         continue;
            //     }
            //     for (int j = idx; j >= 0; --j) {
            //         if (actual_time[i][j] < ts) {
            //             break;
            //         }
            //         for (auto g:G[i][j]) {
            //             long long tm = g.second;
            //             if (tm > te) {
            //                 continue;
            //             }
            //             long long u = (g.first >> 37), v = (g.first >> 12) & (33554431ll);
            //             outLabel[u].push_back(g);
            //             outLabel2[v].push_back(g);
            //         }
            //     }
            //     for (int j = idx+1; j < actual_time[i].size(); ++j) {
            //         if (actual_time[i][j] > te) {
            //             break;
            //         }
            //         for (auto g:G[i][j]) {
            //             long long tm = g.second;
            //             if (tm > te) {
            //                 continue;
            //             }
            //             long long u = (g.first >> 37), v = (g.first >> 12) & (33554431ll);
            //             outLabel[u].push_back(g);
            //             outLabel2[v].push_back(g);
            //         }
            //     }
            // }
            // top=0;
            // for(int u=0;u<n;u++){
            //     int g=find(u);
            //     if(!Vis[g]){
            //         kosaraju1(g);
            //     }
            // }
            // key.clear();
            // for(int u=0;u<n;u++)Vis[u]=0;
            // markedVertices.clear();
            // markedVertices2.clear();
            // col=0;
            // while(top){
            //     int u=Sta[top];top--;
            //     int g=find(u);
            //     if(!Vis2[g]){
            //         CC.clear();
            //         col++;
            //         kosaraju2(g,ts);
            //         kosaraju4(g,g,ts);
            //     }
            // }
            // for(int u=0;u<n;u++){
            //     outLabel2[u].clear();
            //     outLabel[u].clear();
            // }
            // key.clear();
        }
        for(int p=i;p<=tpre;p++){
            for(auto ed:Graph->temporal_edge[p]){
                    int u=ed.first,v=ed.second;
                    if(find(u)==find(v))continue;
                    std::pair<long long,int> g=std::pair<long long,int>((((long long)u)<<37)+(((long long)v)<<12),p);
                    outLabel[find(u)].push_back(g);
                    outLabel2[find(v)].push_back(g);
                }
        }
        for(int st=std::max(i,tpre+1);st<=tim;st++){
            std::unordered_set<int> point;
            point.clear();
            for(int i=0;i<n;i++){
                Vis[i]=0;
                Vis2[i]=0;
            }
            for(auto ed:Graph->temporal_edge[st]){
                int u=ed.first,v=ed.second;
                if(find(u)==find(v))continue;
                std::pair<long long,int> g=std::pair<long long,int>((((long long)u)<<37)+(((long long)v)<<12),st);
                point.insert(find(u));
                point.insert(find(v));
                outLabel[find(u)].push_back(g);
                outLabel2[find(v)].push_back(g);
            }
            top=0;
            col=0;
            for(auto g:point){
                if(!Vis[g]){
                    kosaraju1(g);
                }
            }
            for(int u=0;u<n;u++)Vis[u]=0;
            markedVertices.clear();
            markedVertices2.clear();
            
                while(top){
                    int u=Sta[top];top--;
                    int g=find(u);
                    if(!Vis2[g]){
                        CC.clear();
                        col++;
                        kosaraju2(g,i);
                        kosaraju4(g,g,i);
                        std::vector<std::pair<long long,int>> tmp;
                        tmp.clear();
                        for(auto u:CC){
                            std::vector<std::pair<long long,int>>::iterator iter;
                            for(iter=outLabel2[u].begin();iter!=outLabel2[u].end();iter++){
                                long long v=(*iter).first>>37;
                                if(find(v)!=g){
                                    tmp.push_back(*iter);
                                }
                            }
                        }
                        for(auto u:CC){
                            outLabel2[u].clear();
                            std::vector<std::pair<long long,int>>().swap(outLabel2[u]);
                        }
                        outLabel2[g]=tmp;
                        tmp.clear();
                        for(auto u:CC){
                            std::vector<std::pair<long long,int>>::iterator iter;
                            for(iter=outLabel[u].begin();iter!=outLabel[u].end();iter++){
                                long long v=((*iter).first>>12)&(33554431ll);
                                if(find(v)!=g){
                                    tmp.push_back(*iter);
                                }
                            }
                        }
                        for(auto u:CC){
                            outLabel[u].clear();
                            std::vector<std::pair<long long,int>>().swap(outLabel[u]);
                        }
                        outLabel[g]=tmp;
                        tmp.clear();
                    }
                }
        }
            //std::cerr<<i<<' '<<key.size()<<'\n';
            if(!key.empty()){
                for(auto g:key){
                    //std::cerr<<g.second<<'\n';
                    if(mp.count(g)){
                        std::pair<int,int> alfa=mp[g];
                        alfa.second=i;
                        mp[g]=alfa;
                    }
                    else{
                        mp[g]=std::pair<int,int>(i,i);
                    }
                }
                key.clear();
            }
        if(i%100==0)
        putProcess(double(i) / tmax, currentTime() - start_time);
        
    }
    //std::cerr<<mp.size()<<'\n';
    for(auto state:mp){
        std::pair<long long,int> g=state.first;
        std::pair<int,int> p=state.second;
        G[p.second].push_back(RES(g,p.first));
        Chunk[p.second/len].push_back(RES(g,p.first));
        // int flag=0;
        // for(int i=0;i<actual_time[p.first].size();i++){
        //     if(actual_time[p.first][i]==p.second){
        //         flag=1;
        //         G[p.first][i].push_back(g);
        //         break;
        //     }
        // }
        // if(flag==0){
        //     actual_time[p.first].push_back(0);
        //     std::vector<std::pair<long long,int> > tmp;
        //     tmp.push_back(g);
        //     G[p.first].push_back(tmp);
        //     for(int i=actual_time[p.first].size()-2;i>=0;i--){
        //         if(actual_time[p.first][i]<p.second){
        //             flag=1;
        //             actual_time[p.first][i+1]=p.second;
        //             G[p.first][i+1]=tmp;
        //             break;
        //         }
        //         else{
        //             actual_time[p.first][i+1]=actual_time[p.first][i];
        //             G[p.first][i+1]=G[p.first][i];
        //         }
        //     }
        //     if(!flag){
        //         actual_time[p.first][0]=p.second;
        //         G[p.first][0]=tmp;
        //     }
        // }
    }    
    for(int i=0;i<=tim;i++){
        sort(G[i].begin(),G[i].end(),cmp);
    }
    for(int i=0;i<=tim/len;i++){
        sort(Chunk[i].begin(),Chunk[i].end(),cmp);
    }
}
    

void OptimizedIndex::update(TemporalGraph * Graph){
    modify(Graph,t1,tmax);
}

void optimized(OptimizedIndex * Index, int vertex_num, char * query_file, char * output_file) {

    int ts, te;
    int query_num = 0;
    int invalid_query_num = 0;
    std::ifstream fin(query_file);
    std::ofstream fout(output_file);

    const int horizon = Index->timeHorizon();
    while (fin >> ts >> te) {
        ++query_num;
        if (ts < 0 || te < 0 || ts > te || te > horizon) {
            ++invalid_query_num;
            if (invalid_query_num <= 5) {
                std::cout << "[Query validation] Invalid query ["
                          << ts << "," << te << "] for index time horizon [0,"
                          << horizon << "]." << std::endl;
            }
        }
    }

    if (query_num == 0) {
        std::cout << "No queries were read from " << query_file << "." << std::endl;
        return;
    }

    if (invalid_query_num > 0) {
        std::cout << "[Query validation] " << invalid_query_num << " / "
                  << query_num << " queries are outside the index time horizon [0,"
                  << horizon << "]. Query solving is skipped to avoid out-of-range index access."
                  << std::endl;
        fout << "Invalid query file for index time horizon [0," << horizon
             << "]; skipped " << invalid_query_num << " / " << query_num
             << " queries." << std::endl;
        return;
    }

    fin.close();
    fin.open(query_file);

    int i = 0;
    unsigned long long start_time = currentTime();
    while (fin >> ts >> te) {
        fout << Index->solve(vertex_num, ts, te).str() << std::endl;
        putProcess(double(++i) / query_num, currentTime() - start_time);
    }

    std::cout << "Average (per query): " << timeFormatting((currentTime() - start_time) / query_num).str() << std::endl;

}
