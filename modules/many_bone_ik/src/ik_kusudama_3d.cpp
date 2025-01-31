/**************************************************************************/
/*  ik_kusudama_3d.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "ik_kusudama_3d.h"

#include "core/math/quaternion.h"
#include "ik_limit_cone_3d.h"
#include "math/ik_node_3d.h"

void IKKusudama3D::_update_constraint() {
	update_tangent_radii();
}

void IKKusudama3D::update_tangent_radii() {
	for (int i = 0; i < limit_cones.size(); i++) {
		Ref<IKLimitCone3D> current = limit_cones[i];
		Ref<IKLimitCone3D> next;
		if (i < limit_cones.size() - 1) {
			next = limit_cones[i + 1];
		}
		Ref<IKLimitCone3D> cone = limit_cones[i];
		cone->update_tangent_handles(next);
	}
}

IKKusudama3D::IKKusudama3D(Ref<IKNode3D> to_set, Ref<IKNode3D> bone_direction, Ref<IKNode3D> limiting_axes, real_t cos_half_angle_dampen) {
	Vector<real_t> in_bounds = { 1 };
}

void IKKusudama3D::set_axial_limits(real_t min_angle, real_t in_range) {
	min_axial_angle = min_angle;
	range_angle = in_range;
	Vector3 y_axis = Vector3(0.0f, 1.0f, 0.0f);
	Vector3 z_axis = Vector3(0.0f, 0.0f, 1.0f);
	twist_min_rot = Quaternion(y_axis, min_axial_angle);
	twist_min_vec = twist_min_rot.xform(z_axis);
	twist_center_vec = twist_min_rot.xform(twist_min_vec);
	twist_center_rot = Quaternion(z_axis, twist_center_vec);
	twist_tan = twist_center_vec.cross(y_axis);
	twist_half_range_half_cos = cos(in_range / real_t(4.0)); // For the quadrance angle. We need half the range angle since starting from the center, and half of that since quadrance takes cos(angle/2).
	twist_max_vec = Quaternion(y_axis, in_range).xform(twist_min_vec);
	twist_max_rot = Quaternion(z_axis, twist_max_vec);
	Vector3 max_cross = twist_max_vec.cross(y_axis);
	flipped_bounds = twist_tan.cross(max_cross).y < real_t(0.0);
	twist_min_rot.normalize();
	twist_center_rot.normalize();
	twist_max_rot.normalize();
}
void IKKusudama3D::set_snap_to_twist_limit(Ref<IKNode3D> bone_direction, Ref<IKNode3D> to_set, Ref<IKNode3D> constraint_axes, real_t p_dampening, real_t p_cos_half_dampen) {
	if (!is_axially_constrained()) {
		return;
	}

	Transform3D global_transform_constraint = constraint_axes->get_global_transform();
	Quaternion global_twist_center = twist_center_rot;

	if (global_transform_constraint.basis.is_orthogonal() && Math::is_equal_approx(global_transform_constraint.basis.determinant(), real_t(1.0))) {
		global_twist_center = global_transform_constraint.basis.get_rotation_quaternion() * twist_center_rot;
	}
	global_twist_center.normalize();

	Transform3D global_transform_to_set = to_set->get_global_transform();

	Quaternion align_rot = global_twist_center.inverse();
	if (global_transform_to_set.basis.is_orthogonal() && Math::is_equal_approx(global_transform_to_set.basis.determinant(), real_t(1.0))) {
		align_rot = align_rot * global_transform_to_set.basis.get_rotation_quaternion();
	}
	align_rot.normalize();

	Transform3D parent_global_transform = to_set->get_parent()->get_global_transform().basis.inverse();
	Quaternion parent_global_inverse = Quaternion();
	if (parent_global_transform.basis.is_orthogonal() && Math::is_equal_approx(parent_global_transform.basis.determinant(), real_t(1.0))) {
		parent_global_inverse = parent_global_transform.basis.get_rotation_quaternion();
	}
	parent_global_inverse.normalize();

	Quaternion twist_rotation, swing_rotation; // Hold the ik transform's decomposed swing and twist away from global_twist_centers's global basis.
	get_swing_twist(align_rot, Vector3(0, 1, 0), swing_rotation, twist_rotation);

	if (!twist_rotation.is_equal_approx(Basis())) {
		twist_rotation = IKBoneSegment3D::clamp_to_quadrance_angle(twist_rotation, twist_half_range_half_cos);
	}

	Quaternion recomposition = global_twist_center * (swing_rotation * twist_rotation);
	Quaternion rotation = parent_global_inverse * recomposition;

	Transform3D ik_transform = to_set->get_transform();
	to_set->set_transform(Transform3D(rotation, ik_transform.origin));
}

void IKKusudama3D::get_swing_twist(
		Quaternion p_rotation,
		Vector3 p_axis,
		Quaternion &r_swing,
		Quaternion &r_twist) {
	p_rotation.normalize();
	if (p_rotation.w < 0.0) {
		p_rotation *= -1;
	}
	// Swing-twist decomposition in Clifford algebra
	// https://arxiv.org/abs/1506.05481
	Vector3 p = p_axis * (p_rotation.x * p_axis.x + p_rotation.y * p_axis.y + p_rotation.z * p_axis.z);
	r_twist = Quaternion(p.x, p.y, p.z, p_rotation.w);
	real_t d = Vector3(r_twist.x, r_twist.y, r_twist.z).dot(p_axis);
	if (d < real_t(0.0)) {
		r_twist *= real_t(-1.0);
	}
	r_twist = r_twist.normalized();
	r_swing = p_rotation * r_twist.inverse();
}

void IKKusudama3D::add_limit_cone(Vector3 new_cone_local_point, double radius) {
	Ref<IKLimitCone3D> cone = Ref<IKLimitCone3D>(memnew(IKLimitCone3D(new_cone_local_point, radius, Ref<IKKusudama3D>(this))));
	limit_cones.push_back(cone);
}

void IKKusudama3D::remove_limit_cone(Ref<IKLimitCone3D> limitCone) {
	limit_cones.erase(limitCone);
}

real_t IKKusudama3D::get_min_axial_angle() {
	return min_axial_angle;
}

real_t IKKusudama3D::get_range_angle() {
	return range_angle;
}

bool IKKusudama3D::is_axially_constrained() {
	return axially_constrained;
}

bool IKKusudama3D::is_orientationally_constrained() {
	return orientationally_constrained;
}

void IKKusudama3D::disable_orientational_limits() {
	orientationally_constrained = false;
}

void IKKusudama3D::enable_orientational_limits() {
	orientationally_constrained = true;
}

void IKKusudama3D::toggle_orientational_limits() {
	orientationally_constrained = !orientationally_constrained;
}

void IKKusudama3D::disable_axial_limits() {
	axially_constrained = false;
}

void IKKusudama3D::enable_axial_limits() {
	axially_constrained = true;
}

void IKKusudama3D::toggle_axial_limits() {
	axially_constrained = !axially_constrained;
}

bool IKKusudama3D::is_enabled() {
	return axially_constrained || orientationally_constrained;
}

void IKKusudama3D::disable() {
	axially_constrained = false;
	orientationally_constrained = false;
}

void IKKusudama3D::enable() {
	axially_constrained = true;
	orientationally_constrained = true;
}

TypedArray<IKLimitCone3D> IKKusudama3D::get_limit_cones() const {
	return limit_cones;
}

Vector3 IKKusudama3D::local_point_on_path_sequence(Vector3 in_point, Ref<IKNode3D> limiting_axes) {
	double closest_point_dot = 0;
	Vector3 point = limiting_axes->get_transform().xform(in_point);
	point.normalize();
	Vector3 result = point;

	if (limit_cones.size() == 1) {
		Ref<IKLimitCone3D> cone = limit_cones[0];
		result = cone->get_control_point();
	} else {
		for (int i = 0; i < limit_cones.size() - 1; i++) {
			Ref<IKLimitCone3D> next_cone = limit_cones[i + 1];
			Ref<IKLimitCone3D> cone = limit_cones[i];
			Vector3 closestPathPoint = cone->get_closest_path_point(next_cone, point);
			double closeDot = closestPathPoint.dot(point);
			if (closeDot > closest_point_dot) {
				result = closestPathPoint;
				closest_point_dot = closeDot;
			}
		}
	}

	return result;
}

/**
 * Given a point (in global coordinates), checks to see if a ray can be extended from the Kusudama's
 * origin to that point, such that the ray in the Kusudama's reference frame is within the range_angle allowed by the Kusudama's
 * coneLimits.
 * If such a ray exists, the original point is returned (the point is within the limits).
 * If it cannot exist, the tip of the ray within the kusudama's limits that would require the least rotation
 * to arrive at the input point is returned.
 * @param in_point the point to test.
 * @param in_bounds returns a number from -1 to 1 representing the point's distance from the boundary, 0 means the point is right on
 * the boundary, 1 means the point is within the boundary and on the path furthest from the boundary. any negative number means
 * the point is outside of the boundary, but does not signify anything about how far from the boundary the point is.
 * @return the original point, if it's in limits, or the closest point which is in limits.
 */
Vector3 IKKusudama3D::get_local_point_in_limits(Vector3 in_point, Vector<double> *in_bounds) {
	Vector3 point = in_point.normalized();
	real_t closest_cos = -2.0;
	in_bounds->write[0] = -1;
	Vector3 closest_collision_point = Vector3(NAN, NAN, NAN);
	// This is an exact check for being inside the bounds of each individual cone.
	for (int i = 0; i < limit_cones.size(); i++) {
		Ref<IKLimitCone3D> cone = limit_cones[i];
		Vector3 collision_point = cone->closest_to_cone(point, in_bounds);
		if (Math::is_nan(collision_point.x) || Math::is_nan(collision_point.y) || Math::is_nan(collision_point.z)) {
			in_bounds->write[0] = 1;
			return point;
		}
		real_t this_cos = collision_point.dot(point);
		if (Math::is_nan(closest_collision_point.x) || Math::is_nan(closest_collision_point.y) || Math::is_nan(closest_collision_point.z) || this_cos > closest_cos) {
			closest_collision_point = collision_point;
			closest_cos = this_cos;
		}
	}
	if ((*in_bounds)[0] == -1) {
		// Case where there are multiple cones and we're out of bounds of all cones.
		// Are we in the paths between the cones.
		for (int i = 0; i < limit_cones.size() - 1; i++) {
			Ref<IKLimitCone3D> currCone = limit_cones[i];
			Ref<IKLimitCone3D> nextCone = limit_cones[i + 1];
			Vector3 collision_point = currCone->get_on_great_tangent_triangle(nextCone, point);
			if (Math::is_nan(collision_point.x)) {
				continue;
			}
			real_t this_cos = collision_point.dot(point);
			if (Math::is_equal_approx(this_cos, real_t(1.0))) {
				in_bounds->write[0] = 1;
				return point;
			}
			if (this_cos > closest_cos) {
				closest_collision_point = collision_point;
				closest_cos = this_cos;
			}
		}
	}
	// Return the closest boundary point between cones.
	return closest_collision_point;
}

void IKKusudama3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_limit_cones"), &IKKusudama3D::get_limit_cones);
	ClassDB::bind_method(D_METHOD("set_limit_cones", "limit_cones"), &IKKusudama3D::set_limit_cones);
}

void IKKusudama3D::set_limit_cones(TypedArray<IKLimitCone3D> p_cones) {
	limit_cones = p_cones;
}

void IKKusudama3D::set_current_twist_rotation(Ref<IKBone3D> bone_attached_to, real_t p_rotation) {
	p_rotation = (p_rotation * range_angle) + min_axial_angle;
	Transform3D constraint_transform = bone_attached_to->get_constraint_orientation_transform()->get_global_transform();
	Quaternion inv_rot = constraint_transform.basis.inverse().get_rotation_quaternion();

	if (!inv_rot.is_finite() || inv_rot.is_equal_approx(Quaternion())) {
		return;
	}

	Transform3D bone_direction_transform = bone_attached_to->get_bone_direction_transform()->get_global_transform();
	Quaternion align_rot = inv_rot * bone_direction_transform.basis.get_rotation_quaternion();

	if (align_rot.is_equal_approx(Quaternion())) {
		return;
	}

	Quaternion swing, twist;
	get_swing_twist(align_rot, Vector3(0, 1, 0), swing, twist);
	real_t angle_delta_2 = twist.get_angle() * twist.get_axis().y * -1;
	angle_delta_2 = _to_tau(angle_delta_2);
	real_t dist_to_target_rotation = _to_tau(signed_angle_difference(angle_delta_2, Math_TAU - p_rotation));

	Vector3 limiting_axes_origin = constraint_transform.origin;
	Vector3 bone_axis_y = bone_direction_transform.xform(Vector3(0, 1, 0));
	Vector3 axis_y = bone_axis_y - limiting_axes_origin;

	real_t turn_diff = dist_to_target_rotation;
	Basis rot = Quaternion(axis_y, turn_diff).normalized();
	bone_attached_to->get_ik_transform()->rotate_local_with_global(rot);
}

real_t IKKusudama3D::get_current_twist_rotation(Ref<IKBone3D> bone_attached_to) {
	Quaternion inv_rot = bone_attached_to->get_constraint_orientation_transform()->get_global_transform().basis.inverse().get_rotation_quaternion();
	if (inv_rot.is_finite() && !inv_rot.is_equal_approx(Quaternion())) {
		Quaternion align_rot = inv_rot * bone_attached_to->get_bone_direction_transform()->get_global_transform().basis.get_rotation_quaternion();
		if (!align_rot.is_equal_approx(Quaternion())) {
			Quaternion swing;
			Quaternion twist;
			get_swing_twist(align_rot, Vector3(0, 1, 0), swing, twist);
			real_t angle = twist.get_angle() * twist.get_axis().y;
			if (range_angle == 0.0) {
				return 0;
			}
			return CLAMP(_to_tau(signed_angle_difference(angle, min_axial_angle)) / range_angle, 0, 1);
		}
	}

	return 0;
}

void IKKusudama3D::set_axes_to_orientation_snap(Ref<IKNode3D> bone_direction, Ref<IKNode3D> to_set, Ref<IKNode3D> limiting_axes, real_t p_dampening, real_t p_cos_half_angle_dampen) {
	Vector<double> in_bounds = { 1.0 };
	Vector3 limiting_origin = limiting_axes->get_global_transform().origin;
	Vector3 bone_dir_xform = bone_direction->get_global_transform().xform(Vector3(0.0, 1.0, 0.0));

	bone_ray->set_point_1(limiting_origin);
	bone_ray->set_point_2(bone_dir_xform);

	Vector3 bone_tip = limiting_axes->to_local(bone_ray->get_point_2());
	Vector3 in_limits = get_local_point_in_limits(bone_tip, &in_bounds);

	if (in_bounds[0] < 0 && !is_nan_vector(in_limits)) {
		constrained_ray->set_point_1(bone_ray->get_point_1());
		constrained_ray->set_point_2(limiting_axes->to_global(in_limits));

		Quaternion rectified_rot = Quaternion(bone_ray->get_heading(), constrained_ray->get_heading());
		to_set->rotate_local_with_global(rectified_rot);
	}
}

bool IKKusudama3D::is_nan_vector(const Vector3 &vec) {
	return Math::is_nan(vec.x) || Math::is_nan(vec.y) || Math::is_nan(vec.z);
}
