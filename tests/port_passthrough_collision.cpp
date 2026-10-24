// Dead Space Prime's passthrough collision (passthrough_collision.h): trees built from a host's
// triangles, checked through the game's own collision code (CAreaOctTree's ray tests and
// CMetroidAreaCollider's box tests), as Samus's movement uses them.
#include "passthrough_collision.h"

#include "Collision/CCollisionInfo.hpp"
#include "Collision/CMaterialFilter.hpp"
#include "Kyoto/Math/CLine.hpp"
#include "WorldFormat/CMetroidAreaCollider.hpp"
#include "port_test.h"

#include <cmath>
#include <cstdio>
#include <set>
#include <vector>

using Passthrough::Triangle;
using Passthrough::Vec3;
using PassthroughCollision::Build;
using PassthroughCollision::Facing;
using PassthroughCollision::Options;
using PassthroughCollision::Tree;

namespace {

void Quad(std::vector< Triangle >& out, Vec3 a, Vec3 b, Vec3 c, Vec3 d, uint32_t flags = 0) {
  out.push_back({a, b, c, flags});
  out.push_back({a, c, d, flags});
}

// A room 10 m square and 3 m high from (0, 0, 0), its faces facing in, as a level's do: a floor,
// four walls, a ceiling; a 1 m crate on the floor at (6..7, 6..7) facing out; a door (on
// something the host moves) facing in, 0.5 m from the wall at y = 0; and a sliver with no area.
std::vector< Triangle > Room() {
  std::vector< Triangle > room;
  Quad(room, {0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0});   // floor, up
  Quad(room, {0, 0, 3}, {0, 10, 3}, {10, 10, 3}, {10, 0, 3});   // ceiling, down
  Quad(room, {0, 0, 0}, {0, 10, 0}, {0, 10, 3}, {0, 0, 3});     // x = 0, facing +x
  Quad(room, {10, 0, 0}, {10, 0, 3}, {10, 10, 3}, {10, 10, 0}); // x = 10, facing -x
  Quad(room, {0, 0, 0}, {0, 0, 3}, {10, 0, 3}, {10, 0, 0});     // y = 0, facing +y
  Quad(room, {0, 10, 0}, {10, 10, 0}, {10, 10, 3}, {0, 10, 3}); // y = 10, facing -y
  Quad(room, {6, 6, 1}, {7, 6, 1}, {7, 7, 1}, {6, 7, 1});       // crate top, up
  Quad(room, {6, 6, 0}, {6, 6, 1}, {6, 7, 1}, {6, 7, 0});       // crate x = 6, facing -x
  Quad(room, {4, 0.5f, 0}, {4, 0.5f, 2}, {5, 0.5f, 2}, {5, 0.5f, 0}, Passthrough::kTriangleMoves);
  room.push_back({{1, 1, 0}, {2, 2, 0}, {3, 3, 0}, 0}); // no area
  return room;
}

Vec3 Normal(const CVector3f& a, const CVector3f& b, const CVector3f& c) {
  const CVector3f n = CVector3f::Cross(b - a, c - a).AsNormalized();
  return {n.GetX(), n.GetY(), n.GetZ()};
}

// The nearest hit of a ray through the tree, as the game's ray tests find it: its distance and
// the hit face's normal z; false for none within `length`.
bool Ray(const Tree& tree, Vec3 from, Vec3 dir, float length, float& t, float& nz) {
  const CAreaOctTree& octTree = tree.OctTree();
  CAreaOctTree::Node root(octTree.GetTreeMemory(), octTree.GetBoundingBox(), octTree,
                          octTree.GetTreeType());
  CAreaOctTree::SRayResult result;
  CLine line(CVector3f(from.x, from.y, from.z), CUnitVector3f(dir.x, dir.y, dir.z));
  root.LineTestEx(line, CMaterialFilter::skPassEverything, result, length);
  if (!result.x10_surface.valid()) {
    return false;
  }
  t = result.x3c_t;
  nz = result.x0_plane.GetNormal().GetZ();
  return true;
}

// How far a box can move along `dir` (up to `length`) before the tree stops it, as Samus's
// movement asks (CMetroidAreaCollider::MovingAABoxCollisionCheck_Cached over the leaves its
// path meets); `length` when nothing does.
double Move(const Tree& tree, const CAABox& box, Vec3 dir, float length, Vec3* normal = nullptr) {
  const CVector3f d(dir.x, dir.y, dir.z);
  CAABox path = box;
  path.AccumulateBounds(box.GetMinPoint() + d * length);
  path.AccumulateBounds(box.GetMaxPoint() + d * length);
  const CAreaOctTree& octTree = tree.OctTree();
  CMetroidAreaCollider::COctreeLeafCache leaves(octTree);
  CMetroidAreaCollider::BuildOctreeLeafCache(CAreaOctTree::Node(octTree.GetTreeMemory(),
                                                                octTree.GetBoundingBox(), octTree,
                                                                octTree.GetTreeType()),
                                             path, leaves);
  CCollisionInfo info;
  double out = length;
  if (CMetroidAreaCollider::MovingAABoxCollisionCheck_Cached(
          leaves, box, CMaterialFilter::skPassEverything, CMaterialList(kMT_Solid), d, length, info,
          out) &&
      normal != nullptr) {
    const CVector3f n = info.GetNormalLeft();
    *normal = {n.GetX(), n.GetY(), n.GetZ()};
  }
  return out;
}

// Every leaf's triangles, by walking the tree from its root.
void Walk(const CAreaOctTree::Node& node, std::set< int >& seen, int& leaves, bool& boundsOk,
          const CAABox& root) {
  for (int i = 0; i < 8; ++i) {
    const CAreaOctTree::Node::ETreeType type = node.GetChildType(i);
    if (type == CAreaOctTree::Node::kTT_Invalid) {
      continue;
    }
    const CAreaOctTree::Node child = node.GetChild(i);
    if (type == CAreaOctTree::Node::kTT_Leaf) {
      ++leaves;
      const CAABox& b = child.GetBoundingBox();
      for (int k = 0; k < 3; ++k) {
        if (b.GetMinPoint()[k] < root.GetMinPoint()[k] - 0.01f ||
            b.GetMaxPoint()[k] > root.GetMaxPoint()[k] + 0.01f ||
            b.GetMinPoint()[k] > b.GetMaxPoint()[k]) {
          boundsOk = false;
        }
      }
      const CAreaOctTree::TriListReference list = child.GetTriangleArray();
      for (int j = 0; j < list.GetSize(); ++j) {
        seen.insert(list.GetAt(j));
      }
    } else {
      Walk(child, seen, leaves, boundsOk, root);
    }
  }
}

} // namespace

int main() {
  // The overlap test the build sorts with.
  {
    const float centre[3] = {0, 0, 0}, half[3] = {1, 1, 1};
    const float a[3] = {-5, -5, 0.5f}, b[3] = {5, -5, 0.5f}, c[3] = {0, 5, 0.5f};
    CHECK_MSG(PassthroughCollision::TriangleTouchesBox(centre, half, a, b, c),
              "a large triangle through the box touches it");
    const float a2[3] = {2, 2, 0}, b2[3] = {3, 2, 0}, c2[3] = {2, 3, 0};
    CHECK_MSG(!PassthroughCollision::TriangleTouchesBox(centre, half, a2, b2, c2),
              "one beside it doesn't");
    // Its bounds overlap the box's, but its plane passes by the corner.
    const float a4[3] = {3.1f, 0, 0}, b4[3] = {0, 3.1f, 0}, c4[3] = {0, 0, 3.1f};
    CHECK_MSG(!PassthroughCollision::TriangleTouchesBox(centre, half, a4, b4, c4),
              "one past the box's corner, its bounds overlapping, doesn't");
    const float a5[3] = {1, -1, -1}, b5[3] = {1, 1, -1}, c5[3] = {1, 0, 1};
    CHECK_MSG(PassthroughCollision::TriangleTouchesBox(centre, half, a5, b5, c5),
              "touching a face counts");
  }

  const std::vector< Triangle > room = Room();
  Options options;
  options.nearest = {3, 3, 1};
  const std::unique_ptr< Tree > tree = Build(room.data(), room.size(), options);
  REQUIRE_MSG(tree != nullptr, "a tree from the room");
  const PassthroughCollision::Stats& stats = tree->GetStats();
  CHECK_EQ(stats.offered, uint32_t(room.size()));
  CHECK_EQ(stats.moving, 2u); // the door's, taken too
  CHECK_EQ(stats.degenerate, 1u);
  CHECK_EQ(stats.taken, 18u);
  CHECK_EQ(stats.floors, 4u); // the floor and the crate's top
  CHECK_EQ(stats.ceilings, 2u);
  CHECK_EQ(stats.walls, 12u);
  CHECK_EQ(stats.vertices, 18u); // the room's 8 corners, the crate's 6 used, the door's 4
  CHECK_EQ(stats.overLimits, 0u);
  // Each quad's diagonal lies between two faces in one plane.
  CHECK_EQ(stats.smoothEdges, 9u);
  CHECK_EQ(stats.facingAway, 0u); // all face (3, 3, 1)
  CHECK_EQ(stats.turned, 0u);
  std::printf("room: %u triangles, %u vertices, %u edges, %u leaves, depth %u, %u bytes\n",
              stats.taken, stats.vertices, stats.edges, stats.leaves, stats.depth, stats.treeBytes);

  // The tree's triangles wind as the host's did, so face the same ways.
  {
    const CAreaOctTree& t = tree->OctTree();
    REQUIRE(t.PortTriangleCount() == 18u);
    int matched = 0;
    for (const Triangle& in : room) {
      if (in.a.x == 1 && in.b.x == 2) {
        continue; // the sliver
      }
      const Vec3 want = Normal(CVector3f(in.a.x, in.a.y, in.a.z), CVector3f(in.b.x, in.b.y, in.b.z),
                               CVector3f(in.c.x, in.c.y, in.c.z));
      for (uint i = 0; i < t.PortTriangleCount(); ++i) {
        ushort index[3];
        t.GetTriangleVertexIndices(ushort(i), index);
        const CVector3f& a = t.GetVert(index[0]);
        const CVector3f& b = t.GetVert(index[1]);
        const CVector3f& c = t.GetVert(index[2]);
        const CVector3f middle = (a + b + c) * (1.f / 3.f);
        const CVector3f inMiddle =
            CVector3f((in.a.x + in.b.x + in.c.x) / 3.f, (in.a.y + in.b.y + in.c.y) / 3.f,
                      (in.a.z + in.b.z + in.c.z) / 3.f);
        if ((middle - inMiddle).Magnitude() < 1e-3f) {
          const Vec3 got = Normal(a, b, c);
          CHECK_MSG(got.x * want.x + got.y * want.y + got.z * want.z > 0.999f,
                    "a triangle faces as the host had it");
          // And the game reads it the same way (GetMasterListTriangle).
          const CCollisionSurface s = t.GetMasterListTriangle(ushort(i));
          CHECK_MSG((s.GetVert(0) - a).Magnitude() < 1e-6f &&
                        (s.GetVert(1) - b).Magnitude() < 1e-6f,
                    "the surface's corners as its indices'");
          ++matched;
        }
      }
    }
    CHECK_EQ(matched, 18);
  }

  // Every triangle is in a leaf, and the leaves lie within the root.
  {
    const CAreaOctTree& t = tree->OctTree();
    std::set< int > seen;
    int leaves = 0;
    bool boundsOk = true;
    Walk(CAreaOctTree::Node(t.GetTreeMemory(), t.GetBoundingBox(), t, t.GetTreeType()), seen,
         leaves, boundsOk, t.GetBoundingBox());
    CHECK_EQ(int(seen.size()), 18);
    CHECK_EQ(uint32_t(leaves), stats.leaves);
    CHECK_MSG(boundsOk, "leaf bounds within the root");
  }

  // Rays, as shots and the game's line-of-sight checks cast them.
  {
    float t = 0, nz = 0;
    CHECK_MSG(Ray(*tree, {2, 2, 2}, {0, 0, -1}, 10, t, nz) && std::fabs(t - 2.f) < 1e-3f &&
                  nz > 0.99f,
              "straight down onto the floor, 2 m");
    CHECK_MSG(Ray(*tree, {6.5f, 6.5f, 2.5f}, {0, 0, -1}, 10, t, nz) && std::fabs(t - 1.5f) < 1e-3f,
              "onto the crate's top");
    CHECK_MSG(Ray(*tree, {2, 2, 1}, {0, 0, 1}, 10, t, nz) && std::fabs(t - 2.f) < 1e-3f &&
                  nz < -0.99f,
              "up to the ceiling");
    CHECK_MSG(Ray(*tree, {5, 5, 1}, {1, 0, 0}, 10, t, nz) && std::fabs(t - 5.f) < 1e-3f,
              "along to the wall at x = 10");
    CHECK_MSG(!Ray(*tree, {5, 5, 1}, {1, 0, 0}, 4, t, nz), "nothing within 4 m that way");
  }

  // Samus's box (Isaac's size: 0.76 m across, 1.8 m tall), moving as her movement asks.
  {
    const CAABox box(CVector3f(2.62f, 2.62f, 1.f), CVector3f(3.38f, 3.38f, 2.8f));
    Vec3 n{};
    const double fall = Move(*tree, box, {0, 0, -1}, 3.f, &n);
    CHECK_MSG(std::fabs(fall - 1.0) < 1e-3, "falling, she lands on the floor 1 m down");
    CHECK_MSG(n.z > 0.99f, "pushed up by it");
    const double rise = Move(*tree, box, {0, 0, 1}, 3.f);
    CHECK_MSG(std::fabs(rise - 0.2) < 1e-3, "jumping, her head meets the ceiling 0.2 m up");
    const double walk = Move(*tree, box, {-1, 0, 0}, 5.f);
    CHECK_MSG(std::fabs(walk - 2.62) < 1e-3, "walking into the wall at x = 0");
    // The crate's side at x = 6 faces -x, toward her coming from the room's middle.
    const CAABox low(CVector3f(4.62f, 6.12f, 0.01f), CVector3f(5.38f, 6.88f, 1.81f));
    const double toCrate = Move(*tree, low, {1, 0, 0}, 3.f);
    CHECK_MSG(std::fabs(toCrate - 0.62) < 1e-3, "walking into the crate's side");
    // The game's collision is one-sided: from inside the crate, its side lets her out.
    const CAABox inside(CVector3f(6.1f, 6.1f, 0.2f), CVector3f(6.4f, 6.4f, 0.8f));
    const double out = Move(*tree, inside, {-1, 0, 0}, 1.f);
    CHECK_MSG(out > 0.99, "moving out through a face from behind isn't stopped");
    // The door, on something the host moves, stops her too (the host sends it again as it
    // opens).
    const CAABox atDoor(CVector3f(4.1f, 1.f, 0.1f), CVector3f(4.9f, 1.76f, 1.9f));
    CHECK_MSG(std::fabs(Move(*tree, atDoor, {0, -1, 0}, 1.f) - 0.5) < 1e-3,
              "the closed door stops her 0.5 m on");
    {
      std::vector< Triangle > opened = room;
      for (Triangle& t : opened) {
        if ((t.flags & Passthrough::kTriangleMoves) != 0) {
          t.a.z += 2.5f; // slid up out of the way
          t.b.z += 2.5f;
          t.c.z += 2.5f;
        }
      }
      const std::unique_ptr< Tree > open = Build(opened.data(), opened.size(), options);
      REQUIRE(open != nullptr);
      CHECK_MSG(Move(*open, atDoor, {0, -1, 0}, 0.9f) > 0.899, "the open door lets her by");
    }
    CHECK_MSG(!CMetroidAreaCollider::AABoxCollisionCheckBoolean(tree->OctTree(), box,
                                                                CMaterialFilter::skPassEverything),
              "standing free, she overlaps nothing");
    const CAABox sunk(CVector3f(2.62f, 2.62f, -0.1f), CVector3f(3.38f, 3.38f, 1.7f));
    CHECK_MSG(CMetroidAreaCollider::AABoxCollisionCheckBoolean(tree->OctTree(), sunk,
                                                               CMaterialFilter::skPassEverything),
              "sunk into the floor, she overlaps it");
  }

  // Wound away from her, as a host may have a face: TowardSamus turns it to face her.
  {
    std::vector< Triangle > backwards;
    Quad(backwards, {0, 0, 0}, {0, 10, 0}, {10, 10, 0}, {10, 0, 0}); // a floor facing down
    Options as;
    as.nearest = {5, 5, 1};
    const std::unique_ptr< Tree > kept = Build(backwards.data(), backwards.size(), as);
    REQUIRE(kept != nullptr);
    CHECK_EQ(kept->GetStats().facingAway, 2u);
    CHECK_EQ(kept->GetStats().ceilings, 2u);
    const CAABox box(CVector3f(4.62f, 4.62f, 1.f), CVector3f(5.38f, 5.38f, 2.8f));
    CHECK_MSG(Move(*kept, box, {0, 0, -1}, 3.f) > 2.99, "a floor facing down doesn't hold her");
    Options toward = as;
    toward.facing = Facing::TowardSamus;
    const std::unique_ptr< Tree > turned = Build(backwards.data(), backwards.size(), toward);
    REQUIRE(turned != nullptr);
    CHECK_EQ(turned->GetStats().turned, 2u);
    CHECK_EQ(turned->GetStats().facingAway, 0u);
    CHECK_EQ(turned->GetStats().floors, 2u);
    CHECK_MSG(std::fabs(Move(*turned, box, {0, 0, -1}, 3.f) - 1.0) < 1e-3, "turned, it holds her");
  }

  // Past the limits: a field of separate triangles with 3 corners each, more than the tree's
  // 10240 vertices; the nearest are taken.
  {
    std::vector< Triangle > field;
    for (int y = 0; y < 80; ++y) {
      for (int x = 0; x < 80; ++x) {
        const float fx = float(x) * 0.5f, fy = float(y) * 0.5f;
        field.push_back({{fx, fy, 0}, {fx + 0.4f, fy, 0}, {fx, fy + 0.4f, 0}, 0});
      }
    }
    Options near;
    near.nearest = {0, 0, 0};
    const std::unique_ptr< Tree > big = Build(field.data(), field.size(), near);
    REQUIRE(big != nullptr);
    const PassthroughCollision::Stats& s = big->GetStats();
    CHECK_EQ(s.taken, uint32_t(PassthroughCollision::kMostVertices / 3));
    CHECK_EQ(s.overLimits, uint32_t(field.size()) - s.taken);
    CHECK_MSG(s.vertices <= PassthroughCollision::kMostVertices, "within the vertex limit");
    CHECK_MSG(s.edges <= PassthroughCollision::kMostEdges, "within the edge limit");
    float t = 0, nz = 0;
    CHECK_MSG(Ray(*big, {0.1f, 0.1f, 1}, {0, 0, -1}, 5, t, nz), "the nearest corner is there");
    CHECK_MSG(!Ray(*big, {39.6f, 39.6f, 1}, {0, 0, -1}, 5, t, nz), "the furthest isn't");
    std::printf("field: %u of %u taken, %u leaves, largest %u, depth %u\n", s.taken, s.offered,
                s.leaves, s.largestLeaf, s.depth);
  }

  // A large floor triangle (as the host's are: one under Isaac in build 45's run was 13 x 30 m)
  // and a box on it: a query by the box gathers only leaves near it.
  {
    std::vector< Triangle > hall;
    hall.push_back({{-20, -20, 0}, {20, -20, 0}, {-20, 20, 0}, 0});
    hall.push_back({{20, -20, 0}, {20, 20, 0}, {-20, 20, 0}, 0});
    for (int i = 0; i < 40; ++i) {
      const float x = -18.f + float(i);
      hall.push_back({{x, 0, 0}, {x + 0.5f, 0, 1}, {x, 0, 1}, 0});
    }
    Options o;
    const std::unique_ptr< Tree > t = Build(hall.data(), hall.size(), o);
    REQUIRE(t != nullptr);
    const CAreaOctTree& oct = t->OctTree();
    CMetroidAreaCollider::COctreeLeafCache leaves(oct);
    CMetroidAreaCollider::BuildOctreeLeafCache(
        CAreaOctTree::Node(oct.GetTreeMemory(), oct.GetBoundingBox(), oct, oct.GetTreeType()),
        CAABox(CVector3f(5, 5, -1), CVector3f(7, 7, 4)), leaves);
    CHECK_MSG(leaves.GetNumLeaves() > 0 && leaves.GetNumLeaves() <= 8,
              "a 2 m query gathers a few leaves");
    CHECK_MSG(!leaves.HasCacheOverflowed(), "not all of them");
    const CAABox box(CVector3f(5.62f, 5.62f, 1.f), CVector3f(6.38f, 6.38f, 2.8f));
    CHECK_MSG(std::fabs(Move(*t, box, {0, 0, -1}, 3.f) - 1.0) < 1e-3, "the large floor holds her");
    std::printf("hall: %u leaves, largest %u, depth %u, %zu leaves near the box\n",
                t->GetStats().leaves, t->GetStats().largestLeaf, t->GetStats().depth,
                size_t(leaves.GetNumLeaves()));
  }

  // Nothing to take.
  {
    std::vector< Triangle > none;
    none.push_back({{0, 0, 0}, {1, 1, 1}, {2, 2, 2}, 0});
    none.push_back({{0, 0, 0}, {NAN, 0, 0}, {0, 1, 0}, 0});
    Options o;
    CHECK_MSG(Build(none.data(), none.size(), o) == nullptr,
              "only triangles with no area: no tree");
    CHECK_MSG(Build(nullptr, 0, o) == nullptr, "no triangles: no tree");
  }

  if (port_test::Failures() == 0) {
    std::printf("ok\n");
  }
  return PORT_TEST_RESULT();
}
