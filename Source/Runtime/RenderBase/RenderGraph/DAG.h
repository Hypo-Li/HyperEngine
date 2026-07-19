#pragma once
#include <vector>
#include <unordered_map>
#include <span>
#include <Core/Memory/Allocator.h>
#include <Core/Common/LogSystem.h>

namespace hyp::rg
{
    class DAG
    {
    public:
        DAG(Arena& arena);

        Arena& getArena() noexcept { return mArena; }

        class Node
        {
            friend class DAG;
        public:
            enum class Type
            {
                Pass,
                Resource,
            };

            Node(DAG& dag, Type type);

            Type getType() const noexcept
            {
                return mType;
            }

            virtual ~Node() noexcept = default;

            void sideEffect() noexcept
            {
                mRefCount = kTarget;
            }

            bool isTarget() const noexcept
            {
                return mRefCount >= kTarget;
            }

            bool isCulled() const noexcept
            {
                return mRefCount == 0;
            }

        private:
            static constexpr uint32_t kTarget = 0x80000000u;
            uint32_t mRefCount = 0;
            Type mType;

            ArenaVector<uint32_t> mIncomingEdges;
            ArenaVector<uint32_t> mOutgoingEdges;
        };

        struct Edge
        {
            Node* const from;
            Node* const to;

            Edge(Node* from, Node* to) :
                from(from), to(to)
            {
            }
        };

        void link(Node* from, Node* to);

        void cull();

        void clear();

        bool isAcyclic() const
        {
            return true;
        }

        std::span<Node* const> getNodes() const
        {
            return mNodes;
        }

        std::span<const Edge> getEdges() const
        {
            return mEdges;
        }

        bool precedes(Node* a, Node* b) const;

    private:
        void addNode(Node* node)
        {
            mNodes.push_back(node);
        }

        void buildReachability();

    private:
        Arena& mArena;
        ArenaVector<Node*> mNodes;
        ArenaVector<Edge> mEdges;

        /*std::unordered_map<Node*, uint32_t> mPassNodeIndexMap;
        std::vector<std::vector<uint8_t>> mReachability;*/
    };
}
