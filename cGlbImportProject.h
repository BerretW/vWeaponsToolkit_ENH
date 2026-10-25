#pragma once
#include "cGlbImporter.h"
#include "cSkeleton.h"

enum class GlbPartRole
{
	Unassigned, Body, Magazine, Bolt, Trigger, ChargingHandle, MovingPart,
	Scope, Suppressor, Flashlight, Grip, Excluded
};

struct GlbRoleInfo
{
	GlbPartRole role;
	const char* id;
	const char* label;
};

struct GlbAlignment
{
	float scale = 1;
	std::array<float, 3> rotationDegrees = {};
	std::array<float, 3> origin = {};
	std::array<float, 3> offset = {};
	bool confirmed = false;
};

struct GlbPivot
{
	std::array<float, 3> position = {};
	std::array<float, 3> axis = { 1, 0, 0 };
	bool confirmed = false;
};

inline bool operator==(const GlbPivot& a, const GlbPivot& b)
{
	return a.position == b.position && a.axis == b.axis && a.confirmed == b.confirmed;
}
inline bool operator==(const GlbAlignment& a, const GlbAlignment& b)
{
	return a.scale == b.scale && a.rotationDegrees == b.rotationDegrees && a.origin == b.origin &&
		a.offset == b.offset && a.confirmed == b.confirmed;
}

struct GlbImportProject
{
	wxString sourcePath;
	std::string sourceSha256;
	std::string modelName;
	std::string weaponTemplate;
	std::vector<GlbPartRole> roles;
	GlbAlignment alignment;
	std::vector<GlbPivot> pivots;
	ReferenceSkeleton skeleton;
	std::string skeletonTemplate;
	std::vector<int> boneBindings;
};

const std::vector<GlbRoleInfo>& glbPartRoles();
const GlbRoleInfo& glbPartRoleInfo(GlbPartRole role);
GlbImportProject createGlbImportProject(const GlbModel& model, const wxString& sourcePath, const std::string& weaponTemplate);
std::vector<GlbPartRole> suggestGlbPartRoles(const GlbModel& model);
void assignGlbPartRole(const GlbModel& model, GlbImportProject& project, size_t part, GlbPartRole role);
std::vector<std::string> glbAssignmentErrors(const GlbModel& model, const GlbImportProject& project);
std::vector<size_t> includedGlbParts(const GlbModel& model, const GlbImportProject& project);
void assignGlbPivot(const GlbModel& model, GlbImportProject& project, size_t part, const GlbPivot& pivot);

// Saves drafts too: Unassigned is retained, but cannot pass assignment validation.
// Saves version 3 with embedded reference skeleton/rig. Accepts versions 1/2.
// Source-bound by SHA-256; changed GLBs must be imported anew.
void saveGlbImportProject(const wxString& path, const GlbModel& model, const GlbImportProject& project);
GlbImportProject loadGlbImportProject(const wxString& path, GlbModel& model);
