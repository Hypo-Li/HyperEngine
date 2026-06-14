#include "DAG.h"
#include <stack>
#include <queue>

namespace hyp::rg
{
    DAG::DAG(Arena& arena)
        : mNodes(StlAllocator<Node*>(arena)),
        mEdges(StlAllocator<Edge>(arena))
    {
        mNodes.reserve(8);
        mEdges.reserve(16);
    }

    void DAG::link(Node* from, Node* to)
    {
        mEdges.emplace_back(from, to);
    }

    void DAG::cull()
    {
        for (const Edge& edge : mEdges)
            edge.from->mRefCount++;

        std::stack<Node*> culledNodeStack;
        for (Node* node : mNodes)
        {
            if (node->isCulled())
                culledNodeStack.push(node);
        }

        while (!culledNodeStack.empty())
        {
            Node* node = culledNodeStack.top();
            culledNodeStack.pop();

            for (Edge& edge : mEdges)
            {
                if (edge.to == node)
                {
                    if (--edge.from->mRefCount == 0)
                    {
                        culledNodeStack.push(edge.from);
                    }
                }
            }
        }

        buildReachability();
    }

    void DAG::clear()
    {
        mNodes.clear();
        mEdges.clear();
    }

    bool DAG::precedes(Node* a, Node* b) const
    {
        /*uint32_t indexA = mPassNodeIndexMap.at(a);
        uint32_t indexB = mPassNodeIndexMap.at(b);
        return mReachability[indexA][indexB] != 0;*/
        return false;
    }

    void DAG::buildReachability()
    {
        //std::vector<Node*> passNodes;
        //passNodes.reserve(mNodes.size());
        //for (Node* node : mNodes)
        //{
        //    if (!node->isCulled() &&
        //        node->getType() == Node::Type::Pass)
        //    {
        //        mPassNodeIndexMap.emplace(node, static_cast<uint32_t>(passNodes.size()));
        //        passNodes.push_back(node);
        //    }
        //}

        //mReachability.resize(passNodes.size());
        //const size_t N = passNodes.size();
        //mReachability.assign(N, std::vector<uint8_t>(N, 0));

        //// --- 构建邻接表（只考虑 Pass 节点之间的边） ---
        //std::vector<std::vector<uint32_t>> adj(N);
        //std::vector<uint32_t> indegree(N, 0);
        //for (Edge* e : mEdges)
        //{
        //    auto itFrom = mPassNodeIndexMap.find(e->from);
        //    auto itTo = mPassNodeIndexMap.find(e->to);
        //    if (itFrom != mPassNodeIndexMap.end() && itTo != mPassNodeIndexMap.end())
        //    {
        //        uint32_t u = itFrom->second;
        //        uint32_t v = itTo->second;
        //        adj[u].push_back(v);
        //        indegree[v]++;
        //    }
        //}

        //// --- 拓扑排序 (Kahn 算法) ---
        //std::vector<uint32_t> topo;
        //topo.reserve(N);
        //std::queue<uint32_t> q;
        //for (uint32_t i = 0; i < N; ++i)
        //{
        //    if (indegree[i] == 0)
        //        q.push(i);
        //}

        //while (!q.empty())
        //{
        //    uint32_t u = q.front();
        //    q.pop();
        //    topo.push_back(u);
        //    for (uint32_t v : adj[u])
        //    {
        //        if (--indegree[v] == 0)
        //            q.push(v);
        //    }
        //}

        //// --- 逆拓扑序传播可达性 ---
        //for (int i = static_cast<int>(topo.size()) - 1; i >= 0; --i)
        //{
        //    uint32_t u = topo[i];
        //    for (uint32_t v : adj[u])
        //    {
        //        mReachability[u][v] = 1; // 直接可达
        //        // 合并 v 的可达集合
        //        for (size_t k = 0; k < N; ++k)
        //            if (mReachability[v][k])
        //                mReachability[u][k] = 1;
        //    }
        //}
    }
}
