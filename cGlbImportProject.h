#pragma once
#include "cGlbImporter.h"

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

struct GlbImportProject
{
	wxString sourcePath;
	std::string sourceSha256;
	std::string modelName;
	std::string weaponTemplate;
	std::vector<GlbPartRole> roles;
};

const std::vector<GlbRoleInfo>& glbPartRoles();
const GlbRoleInfo& glbPartRoleInfo(GlbPartRole role);
GlbImportProject createGlbImportProject(const GlbModel& model, const wxString& sourcePath, const std::string& weaponTemplate);
std::vector<GlbPartRole> suggestGlbPartRoles(const GlbModel& model);
void assignGlbPartRole(const GlbModel& model, GlbImportProject& project, size_t part, GlbPartRole role);
std::vector<std::string> glbAssignmentErrors(const GlbModel& model, const GlbImportProject& project);
std::vector<size_t> includedGlbParts(const GlbModel& model, const GlbImportProject& project);

// Saves drafts too: Unassigned is retained, but cannot pass assignment validation.
// XML is versioned and source-bound by SHA-256; changed GLBs must be imported anew.
void saveGlbImportProject(const wxString& path, const GlbModel& model, const GlbImportProject& project);
GlbImportProject loadGlbImportProject(const wxString& path, GlbModel& model);
