/**************************************************************************/
/*  ik_bone_segment_3d.cpp                                                */
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

#include "ik_bone_segment_3d.h"

#include "ik_effector_3d.h"
#include "ik_kusudama_3d.h"
#include "ik_limit_cone_3d.h"
#include "many_bone_ik_3d.h"
#include "math/ik_node_3d.h"
#include "scene/3d/skeleton_3d.h"

Ref<IKBone3D> IKBoneSegment3D::get_root() const {
	return root;
}
Ref<IKBone3D> IKBoneSegment3D::get_tip() const {
	return tip;
}

bool IKBoneSegment3D::is_pinned() const {
	ERR_FAIL_NULL_V(tip, false);
	return tip->is_pinned();
}

Vector<Ref<IKBoneSegment3D>> IKBoneSegment3D::get_child_segments() const {
	return child_segments;
}

void IKBoneSegment3D::create_bone_list(Vector<Ref<IKBone3D>> &p_list, bool p_recursive, bool p_debug_skeleton) const {
	if (p_recursive) {
		for (int32_t child_i = 0; child_i < child_segments.size(); child_i++) {
			child_segments[child_i]->create_bone_list(p_list, p_recursive, p_debug_skeleton);
		}
	}
	Ref<IKBone3D> current_bone = tip;
	Vector<Ref<IKBone3D>> list;
	while (current_bone.is_valid()) {
		list.push_back(current_bone);
		if (current_bone == root) {
			break;
		}
		current_bone = current_bone->get_parent();
	}
	if (p_debug_skeleton) {
		for (int32_t name_i = 0; name_i < list.size(); name_i++) {
			BoneId bone = list[name_i]->get_bone_id();

			String bone_name = skeleton->get_bone_name(bone);
			String effector;
			if (list[name_i]->is_pinned()) {
				effector += "Effector ";
			}
			String prefix;
			if (list[name_i] == root) {
				prefix += "(" + effector + "Root) ";
			}
			if (list[name_i] == tip) {
				prefix += "(" + effector + "Tip) ";
			}
			print_line(vformat("%s%s (%s)", prefix, bone_name, itos(bone)));
		}
	}
	p_list.append_array(list);
}

void IKBoneSegment3D::update_pinned_list(Vector<Vector<real_t>> &r_weights) {
	for (int32_t chain_i = 0; chain_i < child_segments.size(); chain_i++) {
		Ref<IKBoneSegment3D> chain = child_segments[chain_i];
		chain->update_pinned_list(r_weights);
	}
	if (is_pinned()) {
		effector_list.push_back(tip->get_pin());
	}
	real_t passthrough_factor = is_pinned() ? tip->get_pin()->passthrough_factor : 1.0;
	if (passthrough_factor > 0.0) {
		for (Ref<IKBoneSegment3D> child : child_segments) {
			effector_list.append_array(child->effector_list);
		}
	}
}

void IKBoneSegment3D::update_optimal_rotation(Ref<IKBone3D> p_for_bone, real_t p_damp, bool p_translate, bool p_constraint_mode) {
	ERR_FAIL_NULL(p_for_bone);
	update_target_headings(p_for_bone, &heading_weights, &target_headings);
	update_tip_headings(p_for_bone, &tip_headings);
	set_optimal_rotation(p_for_bone, &tip_headings, &target_headings, &heading_weights, p_damp, p_translate, p_constraint_mode);
}

Quaternion IKBoneSegment3D::clamp_to_quadrance_angle(Quaternion p_quat, real_t p_cos_half_angle) {
	real_t newCoeff = real_t(1.0) - (p_cos_half_angle * Math::abs(p_cos_half_angle));
	Quaternion rot = p_quat;
	real_t currentCoeff = rot.x * rot.x + rot.y * rot.y + rot.z * rot.z;
	if (newCoeff >= currentCoeff) {
		return rot;
	} else {
		rot.w = rot.w < real_t(0.0) ? -p_cos_half_angle : p_cos_half_angle;
		real_t compositeCoeff = Math::sqrt(newCoeff / currentCoeff);
		rot.x *= compositeCoeff;
		rot.y *= compositeCoeff;
		rot.z *= compositeCoeff;
	}
	return rot;
}

float IKBoneSegment3D::get_manual_msd(const PackedVector3Array &r_htip, const PackedVector3Array &r_htarget, const Vector<real_t> &p_weights) {
	float manual_RMSD = 0.0f;
	float w_sum = 0.0f;
	for (int i = 0; i < r_htarget.size(); i++) {
		float x_d = r_htarget[i].x - r_htip[i].x;
		float y_d = r_htarget[i].y - r_htip[i].y;
		float z_d = r_htarget[i].z - r_htip[i].z;
		float mag_sq = p_weights[i] * (x_d * x_d + y_d * y_d + z_d * z_d);
		manual_RMSD += mag_sq;
		w_sum += p_weights[i];
	}
	manual_RMSD /= w_sum * w_sum;
	return manual_RMSD;
}

void IKBoneSegment3D::set_optimal_rotation(Ref<IKBone3D> p_for_bone, PackedVector3Array *r_htip, PackedVector3Array *r_htarget, Vector<real_t> *r_weights, float p_dampening, bool p_translate, bool p_constraint_mode) {
	ERR_FAIL_NULL(p_for_bone);
	ERR_FAIL_NULL(r_htip);
	ERR_FAIL_NULL(r_htarget);
	ERR_FAIL_NULL(r_weights);

	update_target_headings(p_for_bone, &heading_weights, &target_headings);
	Transform3D prev_transform = p_for_bone->get_pose();
	bool got_closer = true;
	real_t bone_damp = p_for_bone->get_cos_half_dampen();

	int i = 0;
	do {
		update_tip_headings(p_for_bone, &tip_headings);
		if (!p_constraint_mode) {
			// Solved the ik transform and apply it.
			QCP qcp = QCP(evec_prec, eval_prec);
			Quaternion rot = qcp.weighted_superpose(*r_htip, *r_htarget, *r_weights, p_translate);
			Vector3 translation = qcp.get_translation();
			real_t dampening = (p_dampening != real_t(-1.0)) ? p_dampening : bone_damp;
			rot = clamp_to_quadrance_angle(rot, cos(dampening / 2.0)).normalized();
			p_for_bone->get_ik_transform()->rotate_local_with_global(rot);
			Transform3D result = Transform3D(p_for_bone->get_global_pose().basis, p_for_bone->get_global_pose().origin + translation);
			result.orthonormalize();
			p_for_bone->set_global_pose(result);
		}
		// Calculate orientation before twist to avoid exceeding the twist bound when updating the rotation.
		if (p_for_bone->is_orientationally_constrained() && p_for_bone->get_parent().is_valid()) {
			p_for_bone->get_constraint()->set_axes_to_orientation_snap(p_for_bone->get_bone_direction_transform(), p_for_bone->get_ik_transform(), p_for_bone->get_constraint_orientation_transform(), bone_damp, p_for_bone->get_cos_half_dampen());
		}
		if (p_for_bone->is_axially_constrained() && p_for_bone->get_parent().is_valid()) {
			p_for_bone->get_constraint()->set_snap_to_twist_limit(p_for_bone->get_bone_direction_transform(), p_for_bone->get_ik_transform(), p_for_bone->get_constraint_twist_transform(), bone_damp, p_for_bone->get_cos_half_dampen());
		}

		if (default_stabilizing_pass_count > 0) {
			update_tip_headings(p_for_bone, &tip_headings_uniform);
			real_t current_msd = get_manual_msd(tip_headings_uniform, target_headings, heading_weights);
			if (current_msd <= previous_deviation * 1.0001) {
				previous_deviation = current_msd;
				got_closer = true;
				break;
			} else {
				got_closer = false;
			}
		}
		i++;
	} while (i < default_stabilizing_pass_count && !got_closer);

	if (!got_closer) {
		p_for_bone->set_pose(prev_transform);
	}

	if (root == p_for_bone) {
		previous_deviation = INFINITY;
	}
}

void IKBoneSegment3D::update_target_headings(Ref<IKBone3D> p_for_bone, Vector<real_t> *r_weights, PackedVector3Array *r_target_headings) {
	ERR_FAIL_NULL(p_for_bone);
	ERR_FAIL_NULL(r_weights);
	ERR_FAIL_NULL(r_target_headings);
	int32_t last_index = 0;
	for (int32_t effector_i = 0; effector_i < effector_list.size(); effector_i++) {
		Ref<IKEffector3D> effector = effector_list[effector_i];
		if (effector.is_null()) {
			continue;
		}
		last_index = effector->update_effector_target_headings(r_target_headings, last_index, p_for_bone, &heading_weights);
	}
}

void IKBoneSegment3D::update_tip_headings(Ref<IKBone3D> p_for_bone, PackedVector3Array *r_heading_tip) {
	ERR_FAIL_NULL(r_heading_tip);
	ERR_FAIL_NULL(p_for_bone);
	int32_t last_index = 0;
	for (int32_t effector_i = 0; effector_i < effector_list.size(); effector_i++) {
		Ref<IKEffector3D> effector = effector_list[effector_i];
		if (effector.is_null()) {
			continue;
		}
		last_index = effector->update_effector_tip_headings(r_heading_tip, last_index, p_for_bone);
	}
}

void IKBoneSegment3D::segment_solver(const Vector<float> &p_damp, float p_default_damp, bool p_constraint_mode) {
	for (Ref<IKBoneSegment3D> child : child_segments) {
		if (child.is_null()) {
			continue;
		}
		child->segment_solver(p_damp, p_default_damp, p_constraint_mode);
	}
	bool is_translate = parent_segment.is_null();
	if (is_translate) {
		Vector<float> damp = p_damp;
		damp.fill(Math_PI);
		qcp_solver(damp, Math_PI, is_translate, p_constraint_mode);
		return;
	}
	qcp_solver(p_damp, p_default_damp, is_translate, p_constraint_mode);
}

void IKBoneSegment3D::qcp_solver(const Vector<float> &p_damp, float p_default_damp, bool p_translate, bool p_constraint_mode) {
	for (Ref<IKBone3D> current_bone : bones) {
		float damp = p_default_damp;
		bool is_valid_access = !(unlikely((p_damp.size()) < 0 || (current_bone->get_bone_id()) >= (p_damp.size())));
		if (is_valid_access) {
			damp = p_damp[current_bone->get_bone_id()];
		}
		bool is_non_default_damp = p_default_damp < damp;
		if (is_non_default_damp) {
			damp = p_default_damp;
		}
		update_optimal_rotation(current_bone, damp, p_translate && current_bone == root, p_constraint_mode);
	}
}

void IKBoneSegment3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_pinned"), &IKBoneSegment3D::is_pinned);
	ClassDB::bind_method(D_METHOD("get_ik_bone", "bone"), &IKBoneSegment3D::get_ik_bone);
}

Ref<IKBoneSegment3D> IKBoneSegment3D::get_parent_segment() {
	return parent_segment;
}

IKBoneSegment3D::IKBoneSegment3D(Skeleton3D *p_skeleton, StringName p_root_bone_name, Vector<Ref<IKEffectorTemplate3D>> &p_pins, ManyBoneIK3D *p_many_bone_ik, const Ref<IKBoneSegment3D> &p_parent,
		BoneId p_root, BoneId p_tip, int32_t p_stabilizing_pass_count) {
	root = p_root;
	tip = p_tip;
	skeleton = p_skeleton;
	root = Ref<IKBone3D>(memnew(IKBone3D(p_root_bone_name, p_skeleton, p_parent, p_pins, Math_PI, p_many_bone_ik)));
	if (p_parent.is_valid()) {
		root_segment = p_parent->root_segment;
	} else {
		root_segment = Ref<IKBoneSegment3D>(this);
	}
	root_segment->bone_map[root->get_bone_id()] = root;
	if (p_parent.is_valid()) {
		parent_segment = p_parent;
		root->set_parent(p_parent->get_tip());
	}
	default_stabilizing_pass_count = p_stabilizing_pass_count;
}

void IKBoneSegment3D::enable_pinned_descendants() {
	pinned_descendants = true;
}

bool IKBoneSegment3D::has_pinned_descendants() {
	return pinned_descendants;
}

Vector<Ref<IKBone3D>> IKBoneSegment3D::get_bone_list() const {
	return bones;
}

Ref<IKBone3D> IKBoneSegment3D::get_ik_bone(BoneId p_bone) const {
	if (!bone_map.has(p_bone)) {
		return Ref<IKBone3D>();
	}
	return bone_map[p_bone];
}

void IKBoneSegment3D::create_headings_arrays() {
	Vector<Vector<real_t>> penalty_array;
	Vector<Ref<IKBone3D>> new_pinned_bones;
	recursive_create_penalty_array(this, penalty_array, new_pinned_bones, 1.0);
	pinned_bones.resize(new_pinned_bones.size());
	int32_t total_headings = 0;
	for (const Vector<real_t> &current_penalty_array : penalty_array) {
		total_headings += current_penalty_array.size();
	}
	for (int32_t bone_i = 0; bone_i < new_pinned_bones.size(); bone_i++) {
		pinned_bones.write[bone_i] = new_pinned_bones[bone_i];
	}
	target_headings.resize(total_headings);
	tip_headings.resize(total_headings);
	tip_headings_uniform.resize(total_headings);
	heading_weights.resize(total_headings);
	int currentHeading = 0;
	for (const Vector<real_t> &current_penalty_array : penalty_array) {
		for (real_t ad : current_penalty_array) {
			heading_weights.write[currentHeading] = ad;
			target_headings.write[currentHeading] = Vector3();
			tip_headings.write[currentHeading] = Vector3();
			tip_headings_uniform.write[currentHeading] = Vector3();
			currentHeading++;
		}
	}
}

void IKBoneSegment3D::recursive_create_penalty_array(Ref<IKBoneSegment3D> p_bone_segment, Vector<Vector<real_t>> &r_penalty_array, Vector<Ref<IKBone3D>> &r_pinned_bones, real_t p_falloff) {
	if (p_falloff <= 0.0) {
		return;
	}

	real_t current_falloff = 1.0;

	if (p_bone_segment->is_pinned()) {
		Ref<IKBone3D> current_tip = p_bone_segment->get_tip();
		Ref<IKEffector3D> pin = current_tip->get_pin();
		real_t weight = pin->get_weight();
		Vector<real_t> inner_weight_array;
		inner_weight_array.push_back(weight * p_falloff);

		real_t max_pin_weight = MAX(MAX(pin->get_direction_priorities().x, pin->get_direction_priorities().y), pin->get_direction_priorities().z);
		max_pin_weight = max_pin_weight == 0.0 ? 1.0 : max_pin_weight;

		for (int i = 0; i < 3; ++i) {
			real_t priority = pin->get_direction_priorities()[i];
			if (priority > 0.0) {
				real_t sub_target_weight = weight * (priority / max_pin_weight) * p_falloff;
				inner_weight_array.push_back(sub_target_weight);
				inner_weight_array.push_back(sub_target_weight);
			}
		}

		r_penalty_array.push_back(inner_weight_array);
		r_pinned_bones.push_back(current_tip);
		current_falloff = pin->get_passthrough_factor();
	}

	for (Ref<IKBoneSegment3D> s : p_bone_segment->get_child_segments()) {
		recursive_create_penalty_array(s, r_penalty_array, r_pinned_bones, p_falloff * current_falloff);
	}
}

void IKBoneSegment3D::recursive_create_headings_arrays_for(Ref<IKBoneSegment3D> p_bone_segment) {
	p_bone_segment->create_headings_arrays();
	for (Ref<IKBoneSegment3D> segments : p_bone_segment->get_child_segments()) {
		recursive_create_headings_arrays_for(segments);
	}
}

void IKBoneSegment3D::generate_default_segments(Vector<Ref<IKEffectorTemplate3D>> &p_pins, BoneId p_root_bone, BoneId p_tip_bone, ManyBoneIK3D *p_many_bone_ik) {
	Ref<IKBone3D> current_tip = root;

	while (true) {
		if (_is_parent_of_tip(current_tip, p_tip_bone)) {
			break;
		}

		Vector<BoneId> children = skeleton->get_bone_children(current_tip->get_bone_id());

		if (_has_multiple_children_or_pinned(children, current_tip)) {
			_process_children(children, current_tip, p_pins, p_root_bone, p_tip_bone, p_many_bone_ik);
			break;
		} else if (children.size() == 1) {
			current_tip = _create_next_bone(children[0], current_tip, p_pins, p_many_bone_ik);
		} else {
			break;
		}
	}

	_finalize_segment(current_tip);
}

bool IKBoneSegment3D::_is_parent_of_tip(Ref<IKBone3D> p_current_tip, BoneId p_tip_bone) {
	return skeleton->get_bone_parent(p_current_tip->get_bone_id()) >= p_tip_bone && p_tip_bone != -1;
}

bool IKBoneSegment3D::_has_multiple_children_or_pinned(Vector<BoneId> &r_children, Ref<IKBone3D> p_current_tip) {
	return r_children.size() > 1 || p_current_tip->is_pinned();
}

void IKBoneSegment3D::_process_children(Vector<BoneId> &r_children, Ref<IKBone3D> p_current_tip, Vector<Ref<IKEffectorTemplate3D>> &r_pins, BoneId p_root_bone, BoneId p_tip_bone, ManyBoneIK3D *p_many_bone_ik) {
	tip = p_current_tip;
	Ref<IKBoneSegment3D> parent(this);

	for (int32_t child_i = 0; child_i < r_children.size(); child_i++) {
		BoneId child_bone = r_children[child_i];
		String child_name = skeleton->get_bone_name(child_bone);
		Ref<IKBoneSegment3D> child_segment = _create_child_segment(child_name, r_pins, p_root_bone, p_tip_bone, p_many_bone_ik, parent);

		child_segment->generate_default_segments(r_pins, p_root_bone, p_tip_bone, p_many_bone_ik);

		if (child_segment->has_pinned_descendants()) {
			enable_pinned_descendants();
			child_segments.push_back(child_segment);
		}
	}
}

Ref<IKBoneSegment3D> IKBoneSegment3D::_create_child_segment(String &p_child_name, Vector<Ref<IKEffectorTemplate3D>> &p_pins, BoneId p_root_bone, BoneId p_tip_bone, ManyBoneIK3D *p_many_bone_ik, Ref<IKBoneSegment3D> &p_parent) {
	return Ref<IKBoneSegment3D>(memnew(IKBoneSegment3D(skeleton, p_child_name, p_pins, p_many_bone_ik, p_parent, p_root_bone, p_tip_bone)));
}

Ref<IKBone3D> IKBoneSegment3D::_create_next_bone(BoneId p_bone_id, Ref<IKBone3D> p_current_tip, Vector<Ref<IKEffectorTemplate3D>> &p_pins, ManyBoneIK3D *p_many_bone_ik) {
	String bone_name = skeleton->get_bone_name(p_bone_id);
	Ref<IKBone3D> next_bone = Ref<IKBone3D>(memnew(IKBone3D(bone_name, skeleton, p_current_tip, p_pins, p_many_bone_ik->get_default_damp(), p_many_bone_ik)));
	root_segment->bone_map[p_bone_id] = next_bone;

	return next_bone;
}

void IKBoneSegment3D::_finalize_segment(Ref<IKBone3D> p_current_tip) {
	tip = p_current_tip;

	if (tip->is_pinned()) {
		enable_pinned_descendants();
	}

	set_name(vformat("IKBoneSegment%sRoot%sTip", root->get_name(), tip->get_name()));
	bones.clear();
	create_bone_list(bones, false);
}

int32_t IKBoneSegment3D::get_stabilization_passes() const {
	return default_stabilizing_pass_count;
}

void IKBoneSegment3D::set_stabilization_passes(int32_t p_passes) {
	default_stabilizing_pass_count = p_passes;
}
