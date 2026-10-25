#include "cGlbPlacement.h"
#include <cmath>
#include <limits>
#include <set>

namespace
{
	using V3 = std::array<float, 3>;
	[[noreturn]] void fail(const char* text) { throw std::invalid_argument(std::string("GLB placement: ") + text); }
	void finite(float value, float limit = 1000000)
	{
		if (!std::isfinite(value) || std::fabs(value) > limit) fail("Numeric value is non-finite or out of range.");
	}
	void alignmentValues(const GlbAlignment& a)
	{
		if (!std::isfinite(a.scale) || a.scale < 0.0001f || a.scale > 10000)
			fail("Scale must be 0.0001..10000.");
		for (float x : a.rotationDegrees) finite(x, 360);
		for (float x : a.origin) finite(x);
		for (float x : a.offset) finite(x);
	}
	struct Transform
	{
		const GlbAlignment& a;
		std::array<double, 9> r;
		explicit Transform(const GlbAlignment& value) : a(value)
		{
			alignmentValues(a);
			const double k = std::acos(-1.0)/180;
			const double cx = std::cos(a.rotationDegrees[0]*k), sx = std::sin(a.rotationDegrees[0]*k);
			const double cy = std::cos(a.rotationDegrees[1]*k), sy = std::sin(a.rotationDegrees[1]*k);
			const double cz = std::cos(a.rotationDegrees[2]*k), sz = std::sin(a.rotationDegrees[2]*k);
			r = {cz*cy, cz*sy*sx-sz*cx, cz*sy*cx+sz*sx,
				sz*cy, sz*sy*sx+cz*cx, sz*sy*cx-cz*sx, -sy, cy*sx, cy*cx};
		}
		V3 direction(const V3& p) const
		{
			V3 out;
			for (unsigned k = 0; k < 3; ++k) out[k] = float(r[k*3]*p[0] + r[k*3+1]*p[1] + r[k*3+2]*p[2]);
			return out;
		}
		V3 forward(const V3& p) const
		{
			V3 out;
			for (float x : p) finite(x);
			for (unsigned k = 0; k < 3; ++k)
			{
				double x = 0;
				for (unsigned j = 0; j < 3; ++j) x += r[k*3+j] * (double(p[j])-a.origin[j])*a.scale;
				out[k] = float(x+a.offset[k]); finite(out[k]);
			}
			return out;
		}
		V3 inverse(const V3& p) const
		{
			V3 out;
			for (float x : p) finite(x);
			for (unsigned k = 0; k < 3; ++k)
			{
				double x = 0;
				for (unsigned j = 0; j < 3; ++j) x += r[j*3+k] * (double(p[j])-a.offset[j])/a.scale;
				out[k] = float(x+a.origin[k]); finite(out[k]);
			}
			return out;
		}
	};
	V3 rotate(const V3& v, const V3& axis, float degrees)
	{
		const double angle = degrees*std::acos(-1.0)/180, c = std::cos(angle), s = std::sin(angle);
		const double dot = v[0]*axis[0] + v[1]*axis[1] + v[2]*axis[2];
		const V3 cross = {axis[1]*v[2]-axis[2]*v[1], axis[2]*v[0]-axis[0]*v[2], axis[0]*v[1]-axis[1]*v[0]};
		V3 out;
		for (unsigned k = 0; k < 3; ++k) out[k] = float(v[k]*c + cross[k]*s + axis[k]*dot*(1-c));
		return out;
	}
	V3 unit(V3 axis)
	{
		const double length = std::sqrt(double(axis[0])*axis[0] + double(axis[1])*axis[1] + double(axis[2])*axis[2]);
		for (float& x : axis) x = float(x/length);
		return axis;
	}
	bool needsPivot(GlbPartRole role)
	{
		return role == GlbPartRole::Magazine || role == GlbPartRole::Bolt || role == GlbPartRole::Trigger ||
			role == GlbPartRole::ChargingHandle || role == GlbPartRole::MovingPart;
	}
}

void validateGlbPlacement(const GlbImportProject& project)
{
	alignmentValues(project.alignment);
	for (const auto& pivot : project.pivots)
	{
		double length = 0;
		for (float x : pivot.position) finite(x);
		for (float x : pivot.axis) { finite(x); length += double(x)*x; }
		if (std::fabs(length-1) > 0.0001) fail("Pivot axis must be a unit vector (for example 1,0,0).");
	}
}

std::array<float, 3> glbAlignedPoint(const GlbAlignment& a, const V3& p) { return Transform(a).forward(p); }
std::array<float, 3> glbSourcePoint(const GlbAlignment& a, const V3& p) { return Transform(a).inverse(p); }

std::vector<std::string> glbPlacementErrors(const GlbModel& model, const GlbImportProject& project)
{
	auto errors = glbAssignmentErrors(model, project);
	validateGlbPlacement(project);
	if (!project.alignment.confirmed) errors.push_back("Confirm the model alignment (including identity alignment if already correct).");
	std::set<size_t> checked;
	const Transform transform(project.alignment);
	for (size_t i = 0; i < model.parts.size(); ++i)
	{
		if (needsPivot(project.roles[i]) && !project.pivots.at(i).confirmed && checked.insert(model.parts[i].nodeIndex).second)
			errors.push_back("Confirm a pivot / motion axis for: " + model.parts[i].name);
		if (project.roles[i] != GlbPartRole::Unassigned && project.roles[i] != GlbPartRole::Excluded)
		{
			transform.forward(project.pivots.at(i).position);
			for (const auto& v : model.parts[i].geometry.vertices) transform.forward(v.position);
		}
	}
	return errors;
}

PreviewModel makePreparedGlbPreview(const GlbModel& model, const GlbImportProject& project, int selectedPart, const GlbMotionPreview& motion)
{
	validateGlbPlacement(project);
	if (selectedPart < -1 || selectedPart >= static_cast<int>(model.parts.size())) fail("Selected pivot part is out of range.");
	finite(motion.rotationDegrees, 180); finite(motion.slideMetres, 1);
	const auto selected = includedGlbParts(model, project);
	if ((motion.rotationDegrees != 0 || motion.slideMetres != 0) &&
		(selectedPart < 0 || project.roles[size_t(selectedPart)] == GlbPartRole::Excluded))
		fail("Select an included source object before testing motion.");
	auto view = makeGlbPreview(model, selected);
	const Transform transform(project.alignment);
	for (unsigned k = 0; k < 3; ++k) { view.bmin[k] = std::numeric_limits<float>::max(); view.bmax[k] = -view.bmin[k]; }
	for (size_t i = 0; i < selected.size(); ++i)
	{
		auto& mesh = view.meshes[i];
		const bool moving = (motion.rotationDegrees != 0 || motion.slideMetres != 0) && selectedPart >= 0 &&
			model.parts[selected[i]].nodeIndex == model.parts[size_t(selectedPart)].nodeIndex;
		const auto axis = moving ? unit(project.pivots[size_t(selectedPart)].axis) : V3{1,0,0};
		for (size_t v = 0; v < mesh.pos.size()/3; ++v)
		{
			V3 p = {mesh.pos[v*3], mesh.pos[v*3+1], mesh.pos[v*3+2]};
			V3 n = {mesh.nrm[v*3], mesh.nrm[v*3+1], mesh.nrm[v*3+2]};
			if (moving)
			{
				const auto& pivot = project.pivots[size_t(selectedPart)];
				for (unsigned k = 0; k < 3; ++k) p[k] -= pivot.position[k];
				p = rotate(p, axis, motion.rotationDegrees); n = rotate(n, axis, motion.rotationDegrees);
				for (unsigned k = 0; k < 3; ++k) p[k] += pivot.position[k] + axis[k]*motion.slideMetres/project.alignment.scale;
			}
			p = transform.forward(p); n = transform.direction(n);
			for (unsigned k = 0; k < 3; ++k)
			{
				mesh.pos[v*3+k] = p[k]; mesh.nrm[v*3+k] = n[k];
				view.bmin[k] = std::min(view.bmin[k], p[k]); view.bmax[k] = std::max(view.bmax[k], p[k]);
			}
		}
	}
	if (selectedPart >= 0 && project.roles[size_t(selectedPart)] != GlbPartRole::Excluded)
	{
		const auto& pivot = project.pivots[size_t(selectedPart)];
		view.markers.push_back({transform.forward(pivot.position), transform.direction(unit(pivot.axis)), "Pivot (rest)", false});
	}
	view.markers.push_back({{0,0,0}, {1,0,0}, "Output origin", true});
	view.info += " | aligned (not rigged)";
	return view;
}
