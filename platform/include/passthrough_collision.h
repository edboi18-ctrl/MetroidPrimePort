#pragma once

// Dead Space Prime's passthrough bridge: Metroid Prime collision built from the host's
// triangles (passthrough_protocol.h's TriangleSet), so Samus walks in the host's world
// (port_passthrough.h). A tree as a room's collision is (CAreaOctTree), which the game's
// collision code reads as it reads a room's. Apart from the game itself, so tests build trees.

#include "passthrough_protocol.h"

#include "WorldFormat/CAreaOctTree.hpp"
#include "WorldFormat/CCollisionEdge.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace PassthroughCollision {

// CMetroidAreaCollider marks what a query has already tested in lists of this many vertices,
// edges and triangles of a tree (sDupVertexList, sDupEdgeList, sDupTriangleList), indexed by
// the tree's own indices, which nothing checks against them; a tree stays within them.
constexpr size_t kMostVertices = 0x2800;
constexpr size_t kMostEdges = 0x6000;
constexpr size_t kMostTriangles = 0x4000;

// Which way a triangle faces. Prime's collision is one-sided: a box moving into a triangle from
// behind passes through (CollisionUtil::RayTriangleIntersection_Double gives up when the move
// isn't against its normal, (b - a) x (c - a)). AsHost keeps the host's winding; TowardSamus
// turns each triangle to face where Samus is as the tree is built.
enum class Facing { AsHost, TowardSamus };

struct Options {
  Passthrough::Vec3 nearest{}; // the triangles nearest this go first, up to the limits
  Facing facing = Facing::AsHost;
};

struct Stats {
  uint32_t offered = 0;    // triangles in the set
  uint32_t moving = 0;     // of those taken, on things the host moves (kTriangleMoves): doors,
                           // lifts; a host sends them again as they move
  uint32_t degenerate = 0; // left out: no area to speak of, or not finite
  uint32_t overLimits = 0; // left out: past the tree's limits (the furthest go)
  uint32_t taken = 0;      // in the tree
  uint32_t vertices = 0;
  uint32_t edges = 0;
  uint32_t smoothEdges = 0; // between two faces in one plane, so not collided with as edges
  uint32_t floors = 0;      // by which way they face, as the game's materials say
  uint32_t walls = 0;
  uint32_t ceilings = 0;
  uint32_t turned = 0;     // turned round to face Samus (Facing::TowardSamus)
  uint32_t facingAway = 0; // within 3 m of `nearest`, facing away from it as built
  uint32_t nearby = 0;     // within 3 m of `nearest`
  uint32_t leaves = 0;
  uint32_t depth = 0;
  uint32_t largestLeaf = 0;
  uint32_t treeBytes = 0;
};

// What a tree is made of. CAreaOctTree points into it, so it stays where it is (it is only ever
// held through a unique_ptr) for as long as the tree is used.
class Tree {
public:
  const CAreaOctTree& OctTree() const { return *mOctTree; }
  const Stats& GetStats() const { return mStats; }

  // The host's number for the body (Triangle::flags >> kTriangleBodyShift) of the wall or ceiling
  // nearest the middle of a box that touches it (floors left out: she always stands on one); 0
  // when none does.
  uint32_t Touching(const float centre[3], const float half[3]) const;

  Tree(const Tree&) = delete;
  Tree& operator=(const Tree&) = delete;
  ~Tree();

private:
  Tree();
  friend std::unique_ptr< Tree > Build(const Passthrough::Triangle*, size_t, const Options&);

  std::vector< CVector3f > mVertices;
  std::vector< CCollisionEdge > mEdges;
  std::vector< ushort > mTriangleEdges;
  std::vector< uint > mMaterials;
  std::vector< uchar > mVertexMaterials;
  std::vector< uchar > mEdgeMaterials;
  std::vector< uchar > mTriangleMaterials;
  std::vector< uchar > mNodes; // big-endian, as in a room's collision resource
  std::vector< ushort > mCorners;  // three vertex indices a triangle, as wound
  std::vector< uint32_t > mBodies; // the host's body number, a triangle
  std::vector< bool > mFloors;     // a triangle is a floor
  std::unique_ptr< CAreaOctTree > mOctTree;
  Stats mStats;
};

// A tree from the set's triangles (wire space, which is the guest's world space), those on
// things the host moves (doors, lifts) with the rest, the nearest to `options.nearest` first up
// to the limits above. Null when none can be taken.
std::unique_ptr< Tree > Build(const Passthrough::Triangle* triangles, size_t count,
                              const Options& options);

// The triangle-box overlap test the build sorts triangles into the tree's boxes with
// (separating axes, after Akenine-Moller's "Fast 3D Triangle-Box Overlap Testing", 2001).
// Touching counts as overlapping.
bool TriangleTouchesBox(const float centre[3], const float half[3], const float a[3],
                        const float b[3], const float c[3]);

} // namespace PassthroughCollision
