/* SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup stl
 */

#include "BKE_customdata.h"
#include "BKE_lib_id.h"
#include "BKE_main.h"
#include "BKE_mesh.hh"

#include "BLI_array.hh"
#include "BLI_math_vector.h"
#include "BLI_math_vector.hh"
#include "BLI_task.hh"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"

#include "stl_import_mesh.hh"

namespace blender::io::stl {

STLMeshHelper::STLMeshHelper(int tris_num, bool use_custom_normals)
    : use_custom_normals_(use_custom_normals)
{
  degenerate_tris_num_ = 0;
  duplicate_tris_num_ = 0;
  tris_.reserve(tris_num);
  /* Upper bound (all vertices are unique). */
  verts_.reserve(tris_num * 3);
  if (use_custom_normals) {
    loop_normals_.reserve(tris_num * 3);
  }
}

bool STLMeshHelper::add_triangle(const float3 &a, const float3 &b, const float3 &c)
{
  int v1_id = verts_.index_of_or_add(a);
  int v2_id = verts_.index_of_or_add(b);
  int v3_id = verts_.index_of_or_add(c);
  if ((v1_id == v2_id) || (v1_id == v3_id) || (v2_id == v3_id)) {
    degenerate_tris_num_++;
    return false;
  }
  if (!tris_.add({v1_id, v2_id, v3_id})) {
    duplicate_tris_num_++;
    return false;
  }
  return true;
}

void STLMeshHelper::add_triangle(const float3 &a,
                                 const float3 &b,
                                 const float3 &c,
                                 const float3 &custom_normal)
{
  if (add_triangle(a, b, c)) {
    loop_normals_.append_n_times(custom_normal, 3);
  }
}

Mesh *STLMeshHelper::to_mesh(Main *bmain, char *mesh_name)
{
  if (degenerate_tris_num_ > 0) {
    std::cout << "STL Importer: " << degenerate_tris_num_ << " degenerate triangles were removed"
              << std::endl;
  }
  if (duplicate_tris_num_ > 0) {
    std::cout << "STL Importer: " << duplicate_tris_num_ << " duplicate triangles were removed"
              << std::endl;
  }

  Mesh *mesh = BKE_mesh_add(bmain, mesh_name);
  /* User count is already 1 here, but will be set later in #BKE_mesh_assign_object. */
  id_us_min(&mesh->id);

  mesh->totvert = verts_.size();
  CustomData_add_layer_named(
      &mesh->vdata, CD_PROP_FLOAT3, CD_CONSTRUCT, mesh->totvert, "position");
  mesh->vert_positions_for_write().copy_from(verts_);

  mesh->totpoly = tris_.size();
  mesh->totloop = tris_.size() * 3;
  BKE_mesh_poly_offsets_ensure_alloc(mesh);
  CustomData_add_layer_named(
      &mesh->ldata, CD_PROP_INT32, CD_SET_DEFAULT, mesh->totloop, ".corner_vert");

  MutableSpan<int> poly_offsets = mesh->poly_offsets_for_write();
  threading::parallel_for(poly_offsets.index_range(), 4096, [&](const IndexRange range) {
    for (const int i : range) {
      poly_offsets[i] = i * 3;
    }
  });

  MutableSpan<int> corner_verts = mesh->corner_verts_for_write();
  threading::parallel_for(tris_.index_range(), 2048, [&](IndexRange tris_range) {
    for (const int i : tris_range) {
      corner_verts[3 * i] = tris_[i].v1;
      corner_verts[3 * i + 1] = tris_[i].v2;
      corner_verts[3 * i + 2] = tris_[i].v3;
    }
  });

  /* NOTE: edges must be calculated first before setting custom normals. */
  BKE_mesh_calc_edges(mesh, false, false);

  if (use_custom_normals_ && loop_normals_.size() == mesh->totloop) {
    BKE_mesh_set_custom_normals(mesh, reinterpret_cast<float(*)[3]>(loop_normals_.data()));
    mesh->flag |= ME_AUTOSMOOTH;
  }

  return mesh;
}

}  // namespace blender::io::stl
