#include "passthrough_collision.h"

#include "Collision/CMaterialList.hpp"
#include "Kyoto/Math/CAABox.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace PassthroughCollision {

namespace {

// Vertices go onto a grid of 1/1024 m, so corners the host's bodies share, or nearly share,
// become one vertex, and their triangles share edges.
constexpr float kGrid = 1024.f;
// Further out than this a coordinate isn't taken (it wouldn't fit the grid's integers).
constexpr float kFarthest = 1.0e6f;
// Twice a triangle's area, |(b - a) x (c - a)|, below which it isn't taken (half of a 1 cm
// square has 1e-4): its normal would be mostly rounding.
constexpr float kLeastDoubleArea = 1.0e-4f;
// Faces in one plane: their normals' dot product above this.
constexpr float kSamePlane = 0.9999f;
// Floors and ceilings: normals within 45 degrees of straight up or down; walls between.
// (port_remastered_movers.cpp's is_floor takes 0.7071 too.)
constexpr float kFloorSlope = 0.70710677f;
// Within this of Options::nearest, a triangle counts toward Stats::nearby and facingAway.
constexpr float kNearby = 3.f;

// The tree: a box splits into eight while it holds more than kLeafMost triangles and is at
// least kSplitEdge across (so a leaf is at least half that). A collision query gathers the
// leaves its box meets, at most 64 (COctreeLeafCache); Samus's, about 2 x 2 x 5 m, meets at
// most 3 x 3 x 4 leaves of 2 m or more.
constexpr size_t kLeafMost = 32;
constexpr float kSplitEdge = 4.f;
constexpr int kDeepest = 10;
// A triangle goes into each box it touches, the boxes grown by this for rounding.
constexpr float kSlack = 1.0e-3f;

using Passthrough::Vec3;

Vec3 Sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float Length(const Vec3& a) { return std::sqrt(Dot(a, a)); }

bool Finite(const Vec3& v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
         std::fabs(v.x) < kFarthest && std::fabs(v.y) < kFarthest && std::fabs(v.z) < kFarthest;
}

struct GridKey {
  int32_t x, y, z;
  bool operator==(const GridKey& o) const { return x == o.x && y == o.y && z == o.z; }
};
struct GridHash {
  size_t operator()(const GridKey& k) const {
    uint64_t h = uint32_t(k.x) * 0x9E3779B1u;
    h ^= uint64_t(uint32_t(k.y)) * 0x85EBCA77u + (h << 6) + (h >> 2);
    h ^= uint64_t(uint32_t(k.z)) * 0xC2B2AE3Du + (h << 6) + (h >> 2);
    return size_t(h);
  }
};

GridKey KeyOf(const Vec3& v) {
  return {int32_t(std::lrint(v.x * kGrid)), int32_t(std::lrint(v.y * kGrid)),
          int32_t(std::lrint(v.z * kGrid))};
}
Vec3 PointOf(const GridKey& k) { return {k.x / kGrid, k.y / kGrid, k.z / kGrid}; }

uint32_t EdgeKey(ushort a, ushort b) {
  return a < b ? (uint32_t(a) << 16) | b : (uint32_t(b) << 16) | a;
}

constexpr uint Bit(EMaterialTypes type) { return 1u << uint(type); }

// A candidate: its corners on the grid, how near Options::nearest it reaches, where it was.
struct Candidate {
  GridKey corner[3];
  float reach;
  uint32_t order;
  bool moves; // on something the host moves (kTriangleMoves)
};

struct Box {
  float lo[3];
  float hi[3];
};

void PutU16(std::vector< uchar >& out, size_t at, uint32_t v) {
  out[at] = uchar(v >> 8);
  out[at + 1] = uchar(v);
}
void PutU32(std::vector< uchar >& out, size_t at, uint32_t v) {
  PutU16(out, at, v >> 16);
  PutU16(out, at + 2, v & 0xffffu);
}
void PutFloat(std::vector< uchar >& out, size_t at, float f) {
  uint32_t v;
  std::memcpy(&v, &f, sizeof(v));
  PutU32(out, at, v);
}

// Lays the tree out as CAreaOctTree reads it (big-endian): a branch is its children's types
// (two bits each: 0 none, 1 branch, 2 leaf), two bytes unused, eight offsets to its children
// from the end of those 36 bytes, then the children; a leaf is its bounds (CAABox: min, then
// max), a 16-bit count and the triangles' 16-bit indices. A branch's children's boxes are its
// own halved (CAreaOctTree::Node::GetChild), worked out here the same way.
class Layout {
public:
  Layout(const std::vector< CVector3f >& vertices, const std::vector< ushort >& corners,
         std::vector< uchar >& out, Stats& stats)
  : mVertices(vertices), mCorners(corners), mOut(out), mStats(stats) {}

  CAreaOctTree::Node::ETreeType Node(const Box& box, const std::vector< ushort >& triangles,
                                     int depth) {
    float edge = box.hi[0] - box.lo[0];
    for (int i = 1; i < 3; ++i) {
      edge = (std::min)(edge, box.hi[i] - box.lo[i]);
    }
    mStats.depth = (std::max)(mStats.depth, uint32_t(depth));
    // The root is always a branch: the collision code starts at its children.
    if (depth > 0 && (triangles.size() <= kLeafMost || edge < kSplitEdge || depth >= kDeepest)) {
      Leaf(box, triangles);
      return CAreaOctTree::Node::kTT_Leaf;
    }
    const size_t start = mOut.size();
    mOut.resize(start + 36, 0);
    float centre[3];
    for (int i = 0; i < 3; ++i) {
      centre[i] = 0.5f * (box.lo[i] + box.hi[i]);
    }
    uint32_t flags = 0;
    for (int child = 0; child < 8; ++child) {
      Box part;
      for (int i = 0; i < 3; ++i) {
        const bool upper = (child >> i) & 1;
        part.lo[i] = upper ? centre[i] : box.lo[i];
        part.hi[i] = upper ? box.hi[i] : centre[i];
      }
      std::vector< ushort > inside;
      for (ushort t : triangles) {
        if (Touches(part, t)) {
          inside.push_back(t);
        }
      }
      if (inside.empty()) {
        continue;
      }
      const size_t at = mOut.size();
      const CAreaOctTree::Node::ETreeType type = Node(part, inside, depth + 1);
      flags |= uint32_t(type) << (2 * child);
      PutU32(mOut, start + 4 + 4 * child, uint32_t(at - (start + 36)));
    }
    PutU16(mOut, start, flags);
    return CAreaOctTree::Node::kTT_Branch;
  }

private:
  void Corner(ushort t, int k, float out[3]) const {
    const CVector3f& v = mVertices[mCorners[3 * t + k]];
    out[0] = v.GetX();
    out[1] = v.GetY();
    out[2] = v.GetZ();
  }

  bool Touches(const Box& box, ushort t) const {
    float centre[3], half[3], a[3], b[3], c[3];
    for (int i = 0; i < 3; ++i) {
      centre[i] = 0.5f * (box.lo[i] + box.hi[i]);
      half[i] = 0.5f * (box.hi[i] - box.lo[i]) + kSlack;
    }
    Corner(t, 0, a);
    Corner(t, 1, b);
    Corner(t, 2, c);
    return TriangleTouchesBox(centre, half, a, b, c);
  }

  // Its bounds: its triangles', within its box (each triangle is also in the other boxes it
  // reaches into), so a query near a large floor triangle gathers only the leaves near it.
  void Leaf(const Box& box, const std::vector< ushort >& triangles) {
    float lo[3] = {INFINITY, INFINITY, INFINITY};
    float hi[3] = {-INFINITY, -INFINITY, -INFINITY};
    for (ushort t : triangles) {
      for (int k = 0; k < 3; ++k) {
        float p[3];
        Corner(t, k, p);
        for (int i = 0; i < 3; ++i) {
          lo[i] = (std::min)(lo[i], p[i]);
          hi[i] = (std::max)(hi[i], p[i]);
        }
      }
    }
    const size_t start = mOut.size();
    const size_t bytes = 24 + 2 + 2 * triangles.size();
    mOut.resize(start + ((bytes + 3) & ~size_t(3)), 0);
    for (int i = 0; i < 3; ++i) {
      PutFloat(mOut, start + 4 * i, (std::max)(lo[i], box.lo[i] - kSlack));
      PutFloat(mOut, start + 12 + 4 * i, (std::min)(hi[i], box.hi[i] + kSlack));
    }
    PutU16(mOut, start + 24, uint32_t(triangles.size()));
    for (size_t j = 0; j < triangles.size(); ++j) {
      PutU16(mOut, start + 26 + 2 * j, triangles[j]);
    }
    ++mStats.leaves;
    mStats.largestLeaf = (std::max)(mStats.largestLeaf, uint32_t(triangles.size()));
  }

  const std::vector< CVector3f >& mVertices;
  const std::vector< ushort >& mCorners;
  std::vector< uchar >& mOut;
  Stats& mStats;
};

} // namespace

bool TriangleTouchesBox(const float centre[3], const float half[3], const float a[3],
                        const float b[3], const float c[3]) {
  // The triangle about the box's centre.
  const Vec3 v[3] = {{a[0] - centre[0], a[1] - centre[1], a[2] - centre[2]},
                     {b[0] - centre[0], b[1] - centre[1], b[2] - centre[2]},
                     {c[0] - centre[0], c[1] - centre[1], c[2] - centre[2]}};
  const Vec3 h = {half[0], half[1], half[2]};
  // Apart along an axis: the triangle's corners all beyond the box's reach along it.
  auto apart = [&](const Vec3& axis) {
    const float p0 = Dot(v[0], axis), p1 = Dot(v[1], axis), p2 = Dot(v[2], axis);
    const float reach = h.x * std::fabs(axis.x) + h.y * std::fabs(axis.y) + h.z * std::fabs(axis.z);
    return (std::min)({p0, p1, p2}) > reach || (std::max)({p0, p1, p2}) < -reach;
  };
  // The box's own axes (the triangle's bounds against the box).
  if (apart({1.f, 0.f, 0.f}) || apart({0.f, 1.f, 0.f}) || apart({0.f, 0.f, 1.f})) {
    return false;
  }
  // The triangle's plane.
  const Vec3 e[3] = {Sub(v[1], v[0]), Sub(v[2], v[1]), Sub(v[0], v[2])};
  if (apart(Cross(e[0], e[1]))) {
    return false;
  }
  // Each of the box's axes crossed with each of the triangle's edges (an edge along that axis
  // gives no axis, and nothing to test).
  const Vec3 units[3] = {{1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}};
  for (const Vec3& edge : e) {
    for (const Vec3& unit : units) {
      const Vec3 axis = Cross(unit, edge);
      if (Dot(axis, axis) > 1e-12f && apart(axis)) {
        return false;
      }
    }
  }
  return true;
}

Tree::Tree() = default;
Tree::~Tree() = default;

std::unique_ptr< Tree > Build(const Passthrough::Triangle* triangles, size_t count,
                              const Options& options) {
  std::unique_ptr< Tree > tree(new Tree());
  Stats& stats = tree->mStats;
  stats.offered = uint32_t(count);

  // The candidates, on the grid, nearest first.
  std::vector< Candidate > candidates;
  candidates.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    const Passthrough::Triangle& t = triangles[i];
    if (!Finite(t.a) || !Finite(t.b) || !Finite(t.c)) {
      ++stats.degenerate;
      continue;
    }
    Candidate c;
    c.corner[0] = KeyOf(t.a);
    c.corner[1] = KeyOf(t.b);
    c.corner[2] = KeyOf(t.c);
    const Vec3 a = PointOf(c.corner[0]), b = PointOf(c.corner[1]), p = PointOf(c.corner[2]);
    if (c.corner[0] == c.corner[1] || c.corner[1] == c.corner[2] || c.corner[2] == c.corner[0] ||
        Length(Cross(Sub(b, a), Sub(p, a))) < kLeastDoubleArea) {
      ++stats.degenerate;
      continue;
    }
    const Vec3 middle = {(a.x + b.x + p.x) / 3.f, (a.y + b.y + p.y) / 3.f, (a.z + b.z + p.z) / 3.f};
    const float spread =
        (std::max)({Length(Sub(a, middle)), Length(Sub(b, middle)), Length(Sub(p, middle))});
    c.reach = (std::max)(0.f, Length(Sub(middle, options.nearest)) - spread);
    c.order = uint32_t(i);
    c.moves = (t.flags & Passthrough::kTriangleMoves) != 0;
    candidates.push_back(c);
  }
  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const Candidate& l, const Candidate& r) { return l.reach < r.reach; });

  // Taken in that order while the tree's limits allow: vertices and edges shared.
  std::unordered_map< GridKey, ushort, GridHash > vertexIds;
  std::unordered_map< uint32_t, ushort > edgeIds;
  std::vector< ushort > corners; // three vertex indices a triangle, as wound in the tree
  std::vector< uint8_t > edgeUses;
  std::vector< bool > edgeSmooth;
  std::vector< Vec3 > edgeNormal;
  std::vector< uint > triangleFlags;
  for (const Candidate& c : candidates) {
    GridKey k[3] = {c.corner[0], c.corner[1], c.corner[2]};
    Vec3 p[3] = {PointOf(k[0]), PointOf(k[1]), PointOf(k[2])};
    Vec3 n = Cross(Sub(p[1], p[0]), Sub(p[2], p[0]));
    if (options.facing == Facing::TowardSamus && Dot(n, Sub(options.nearest, p[0])) < 0.f) {
      std::swap(k[1], k[2]);
      std::swap(p[1], p[2]);
      n = {-n.x, -n.y, -n.z};
      ++stats.turned;
    }
    // What it would add, within the tree's limits.
    size_t newVertices = 0;
    size_t newEdges = 0;
    ushort known[3] = {};
    bool have[3];
    for (int i = 0; i < 3; ++i) {
      const auto found = vertexIds.find(k[i]);
      have[i] = found != vertexIds.end();
      if (have[i]) {
        known[i] = found->second;
      } else {
        ++newVertices;
      }
    }
    for (int i = 0; i < 3; ++i) {
      const int j = (i + 1) % 3;
      if (!have[i] || !have[j] || edgeIds.count(EdgeKey(known[i], known[j])) == 0) {
        ++newEdges;
      }
    }
    if (tree->mVertices.size() + newVertices > kMostVertices ||
        tree->mEdges.size() + newEdges > kMostEdges || corners.size() / 3 + 1 > kMostTriangles) {
      ++stats.overLimits;
      continue;
    }
    ushort id[3];
    for (int i = 0; i < 3; ++i) {
      const auto found = vertexIds.find(k[i]);
      if (found != vertexIds.end()) {
        id[i] = found->second;
      } else {
        id[i] = ushort(tree->mVertices.size());
        vertexIds.emplace(k[i], id[i]);
        tree->mVertices.emplace_back(p[i].x, p[i].y, p[i].z);
      }
    }
    const Vec3 unit = [&] {
      const float length = Length(n);
      return Vec3{n.x / length, n.y / length, n.z / length};
    }();
    uint flags = Bit(kMT_Solid) | Bit(kMT_Metal);
    if (unit.z > kFloorSlope) {
      flags |= Bit(kMT_Floor);
      ++stats.floors;
    } else if (unit.z < -kFloorSlope) {
      flags |= Bit(kMT_Ceiling);
      ++stats.ceilings;
    } else {
      flags |= Bit(kMT_Wall);
      ++stats.walls;
    }
    for (int i = 0; i < 3; ++i) {
      const ushort from = id[i], to = id[(i + 1) % 3];
      const uint32_t key = EdgeKey(from, to);
      const auto found = edgeIds.find(key);
      ushort edge;
      if (found != edgeIds.end()) {
        edge = found->second;
        // Two faces in one plane, facing the same way: no edge to collide with between them.
        edgeUses[edge] = uint8_t((std::min)(edgeUses[edge] + 1, 3));
        edgeSmooth[edge] = edgeUses[edge] == 2 && Dot(edgeNormal[edge], unit) > kSamePlane;
      } else {
        edge = ushort(tree->mEdges.size());
        edgeIds.emplace(key, edge);
        tree->mEdges.emplace_back(from, to);
        edgeUses.push_back(1);
        edgeSmooth.push_back(false);
        edgeNormal.push_back(unit);
      }
      tree->mTriangleEdges.push_back(edge);
      // The tree reads a triangle's first two corners off its first edge, the other way round
      // when it's flagged (CAreaOctTree::GetTriangleVertexIndices).
      if (i == 0 && tree->mEdges[edge].GetVertIndex1() != from) {
        flags |= Bit(kMT_RedundantEdgeOrFlippedTri);
      }
    }
    corners.insert(corners.end(), id, id + 3);
    stats.moving += c.moves ? 1 : 0;
    triangleFlags.push_back(flags);
    if (c.reach < kNearby) {
      ++stats.nearby;
      if (Dot(unit, Sub(options.nearest, p[0])) < 0.f) {
        ++stats.facingAway;
      }
    }
  }
  stats.taken = uint32_t(corners.size() / 3);
  if (stats.taken == 0) {
    return nullptr;
  }

  // Materials: a table of the game's material flags, and an index into it for each vertex,
  // edge and triangle.
  auto material = [&](uint flags) {
    for (size_t i = 0; i < tree->mMaterials.size(); ++i) {
      if (tree->mMaterials[i] == flags) {
        return uchar(i);
      }
    }
    tree->mMaterials.push_back(flags);
    return uchar(tree->mMaterials.size() - 1);
  };
  const uchar solid = material(Bit(kMT_Solid) | Bit(kMT_Metal));
  const uchar smooth = material(Bit(kMT_Solid) | Bit(kMT_Metal) | Bit(kMT_NoEdgeCollision));
  tree->mVertexMaterials.assign(tree->mVertices.size(), solid);
  for (size_t e = 0; e < tree->mEdges.size(); ++e) {
    tree->mEdgeMaterials.push_back(edgeSmooth[e] ? smooth : solid);
    stats.smoothEdges += edgeSmooth[e] ? 1 : 0;
  }
  for (uint flags : triangleFlags) {
    tree->mTriangleMaterials.push_back(material(flags));
  }
  stats.vertices = uint32_t(tree->mVertices.size());
  stats.edges = uint32_t(tree->mEdges.size());

  // The root: a cube around them all, so its children are cubes too.
  Box root;
  float widest = 0.f;
  for (int i = 0; i < 3; ++i) {
    root.lo[i] = INFINITY;
    root.hi[i] = -INFINITY;
  }
  for (const CVector3f& v : tree->mVertices) {
    const float p[3] = {v.GetX(), v.GetY(), v.GetZ()};
    for (int i = 0; i < 3; ++i) {
      root.lo[i] = (std::min)(root.lo[i], p[i]);
      root.hi[i] = (std::max)(root.hi[i], p[i]);
    }
  }
  for (int i = 0; i < 3; ++i) {
    widest = (std::max)(widest, root.hi[i] - root.lo[i]);
  }
  const float half = 0.5f * widest + 0.5f;
  for (int i = 0; i < 3; ++i) {
    const float middle = 0.5f * (root.lo[i] + root.hi[i]);
    root.lo[i] = middle - half;
    root.hi[i] = middle + half;
  }
  std::vector< ushort > all(stats.taken);
  for (uint32_t i = 0; i < stats.taken; ++i) {
    all[i] = ushort(i);
  }
  Layout(tree->mVertices, corners, tree->mNodes, stats).Node(root, all, 0);
  stats.treeBytes = uint32_t(tree->mNodes.size());

  tree->mOctTree = std::make_unique< CAreaOctTree >(
      CAABox(CVector3f(root.lo[0], root.lo[1], root.lo[2]),
             CVector3f(root.hi[0], root.hi[1], root.hi[2])),
      CAreaOctTree::Node::kTT_Branch, tree->mNodes.data(), tree->mNodes.data(),
      uint(tree->mMaterials.size()), tree->mMaterials.data(), tree->mVertexMaterials.data(),
      tree->mEdgeMaterials.data(), tree->mTriangleMaterials.data(), uint(tree->mEdges.size()),
      tree->mEdges.data(), uint(stats.taken), tree->mTriangleEdges.data(),
      uint(tree->mVertices.size()), tree->mVertices.data());
  return tree;
}

} // namespace PassthroughCollision
