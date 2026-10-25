#pragma once

#include <wx/string.h>
#include <cstdint>
#include <string>
#include <vector>

enum class WeaponResourceComponentKind
{
	Clip, Scope, Suppressor, Flashlight, Grip
};

struct WeaponResourceComponent
{
	std::string componentId;
	std::string modelName;
	// The real weapon-skeleton bone, e.g. WAPClip (not component-local AAPClip).
	std::string attachBone;
	WeaponResourceComponentKind kind = WeaponResourceComponentKind::Clip;
	bool defaultEnabled = true;
	// Empty preserves the donor AAP* anchor. Nonempty names an actual bone
	// supplied by the component drawable, e.g. its identity Gun_Root.
	std::string localAttachBone;
};

struct WeaponResourceSettings
{
	std::string weaponTemplate;
	std::string weaponId;
	std::string modelName;
	std::string displayName;
	std::string resourceName;
	std::vector<WeaponResourceComponent> components;
};

struct WeaponResourceFile
{
	// Resource-relative paths use '/' as required by the FiveM manifest.
	std::string relativePath;
	std::vector<uint8_t> bytes;
};

// templatesRoot is the "templates" directory, not "templates/weapons".
// Returns metadata only: the caller stages the base and component YDR/YTDs separately.
// Components replace (never supplement) the stock template AttachPoints.
// An empty components list retains the integrated-model behavior: no AttachPoints.
// One component per kind is supported; defaultEnabled defaults to true.
// Clip requires a matching shipped COMPONENT_<weapon suffix>_CLIP_01 template.
// Other kinds use stock attachment templates when present, otherwise shipped
// AT_SCOPE_MEDIUM / AT_AR_SUPP / AT_AR_FLSH / AT_AR_AFGRIP defaults
// (PI_SUPP / PI_FLSH for pistols; SCOPE_LARGE_FIXED_ZOOM for WAPScop_2).
// Component-local attachment names are template-defined unless localAttachBone
// explicitly overrides them. Effect/helper bones are never rewritten; the caller
// must supply compatible component rigs, not just a matching WAP attachment bone.
// Stock audio, animation clips, ammo and PickupHash references remain unchanged.
// Component animations/effects still depend on supplied bones and GTA clips.
// Throws std::invalid_argument for invalid settings, std::runtime_error for bad templates.
// Does not create directories or write files.
std::vector<WeaponResourceFile> buildWeaponResourceFiles(
	const WeaponResourceSettings& settings, const wxString& templatesRoot);
