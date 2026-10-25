#include <wx/wx.h>
#include <wx/filename.h>
#include <wx/file.h>
#include "cGlbPlacement.h"
#include "cRscWriter.h"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>

namespace
{
	void require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
	void checkNear(float a, float b) { require(std::fabs(a-b) < 0.0001f, "Placement numeric result differs."); }
	void fails(const std::function<void()>& action, const std::string& reason)
	{
		try { action(); }
		catch (const std::exception& error)
		{
			if (std::string(error.what()).find(reason) == std::string::npos)
				throw std::runtime_error("Expected " + reason + ", got: " + error.what());
			return;
		}
		throw std::runtime_error("Missing placement failure: " + reason);
	}
	void write(const wxString& path, const std::string& xml)
	{
		cRscWriter::save(path, std::vector<uint8_t>(xml.begin(), xml.end()));
	}
	std::string read(const wxString& path)
	{
		wxFile file(path);
		require(file.IsOpened(), "Placement test file missing.");
		std::string result(static_cast<size_t>(file.Length()), 0);
		require(file.Read(&result[0], result.size()) == wxFileOffset(result.size()), "Placement test read failed.");
		return result;
	}
}

void testGlbPlacement(const wxString& output)
{
	auto model = loadGlb(wxFileName(output, "static_import.glb").GetFullPath());
	auto project = createGlbImportProject(model, wxFileName(output, "static_import.glb").GetFullPath(), "WEAPON_CARBINERIFLE");
	assignGlbPartRole(model, project, 0, GlbPartRole::Body);
	assignGlbPartRole(model, project, 1, GlbPartRole::Excluded);
	require(glbPlacementErrors(model, project).size() == 1, "Even identity alignment must be explicitly confirmed.");
	project.alignment.scale = 2;
	project.alignment.origin = {11,-33,22};
	project.alignment.rotationDegrees = {0,0,90};
	project.alignment.offset = {1,2,3};
	project.alignment.confirmed = true;
	require(glbPlacementErrors(model, project).empty(), "Confirmed static placement.");
	auto preview = makePreparedGlbPreview(model, project);
	checkNear(preview.meshes[0].pos[0], 1); checkNear(preview.meshes[0].pos[1], 2); checkNear(preview.meshes[0].pos[2], 3);
	checkNear(preview.meshes[0].pos[3], 1); checkNear(preview.meshes[0].pos[4], -2);
	checkNear(preview.meshes[0].nrm[0], 0); checkNear(preview.meshes[0].nrm[1], -3/std::sqrt(13.f));
	const auto restored = glbSourcePoint(project.alignment, {1,-2,3});
	checkNear(restored[0], 9); checkNear(restored[1], -33); checkNear(restored[2], 22);
	GlbAlignment rotation;
	rotation.rotationDegrees = {90,0,90};
	const auto order = glbAlignedPoint(rotation, {0,1,0});
	checkNear(order[0], 0); checkNear(order[1], 0); checkNear(order[2], 1);
	rotation.rotationDegrees = {32,-47,11}; rotation.scale = .25f; rotation.offset = {.3f,-.1f,2};
	const auto inverse = glbSourcePoint(rotation, glbAlignedPoint(rotation, {1.25f,-3.5f,4.75f}));
	checkNear(inverse[0], 1.25f); checkNear(inverse[1], -3.5f); checkNear(inverse[2], 4.75f);
	auto pivot = project.pivots[0];
	pivot.position = project.alignment.origin; pivot.axis = {0,0,1}; pivot.confirmed = true;
	assignGlbPivot(model, project, 0, pivot);
	const auto originalPosition = model.parts[0].geometry.vertices[1].position;
	const auto originalAlignment = project.alignment;
	const auto moved = makePreparedGlbPreview(model, project, 0, {90,0});
	checkNear(moved.meshes[0].pos[3], 5); checkNear(moved.meshes[0].pos[4], 2); checkNear(moved.meshes[0].pos[5], 3);
	const auto slide = makePreparedGlbPreview(model, project, 0, {0,.02f});
	checkNear(slide.meshes[0].pos[2], 3.02f);
	require(model.parts[0].geometry.vertices[1].position == originalPosition && project.alignment == originalAlignment,
		"Test motion must never accumulate or change saved rest data.");
	require(makePreparedGlbPreview(model, project).meshes[0].pos == preview.meshes[0].pos, "Reset returns exactly to rest.");
	auto assembly = project;
	assignGlbPartRole(model, assembly, 1, GlbPartRole::Magazine);
	const auto still = makePreparedGlbPreview(model, assembly);
	const auto posed = makePreparedGlbPreview(model, assembly, 0, {20,0});
	require(posed.meshes[1].pos == still.meshes[1].pos && posed.meshes[1].nrm == still.meshes[1].nrm,
		"Testing one source object must not move another.");
	auto invalid = project; invalid.alignment.scale = 0;
	fails([&] { validateGlbPlacement(invalid); }, "Scale");
	invalid = project; invalid.alignment.scale = 10000.1f;
	fails([&] { validateGlbPlacement(invalid); }, "Scale");
	invalid = project; invalid.alignment.rotationDegrees[0] = std::numeric_limits<float>::infinity();
	fails([&] { validateGlbPlacement(invalid); }, "non-finite");
	invalid = project; invalid.pivots[0].axis = {0,0,0};
	fails([&] { validateGlbPlacement(invalid); }, "unit vector");
	invalid = project; invalid.pivots[0].position[0] = std::numeric_limits<float>::quiet_NaN();
	fails([&] { validateGlbPlacement(invalid); }, "non-finite");
	fails([&] { makePreparedGlbPreview(model, project, 1, {10,0}); }, "included");
	assignGlbPartRole(model, project, 0, GlbPartRole::Trigger);
	require(!project.pivots[0].confirmed, "Changing a semantic role invalidates its pivot confirmation.");
	require(!glbPlacementErrors(model, project).empty(), "Missing body and unconfirmed pivot must not pass.");
	assignGlbPartRole(model, project, 0, GlbPartRole::Body);

	const auto saved = wxFileName(output, "placement.vwtimport.xml").GetFullPath();
	saveGlbImportProject(saved, model, project);
	GlbModel reopened;
	const auto roundTrip = loadGlbImportProject(saved, reopened);
	require(roundTrip.alignment == project.alignment && roundTrip.pivots == project.pivots, "Placement/flags round trip.");
	const auto xml = read(saved);
	auto corrupt = [&](const std::string& from, const std::string& to, const std::string& reason) {
		auto text = xml;
		const auto at = text.find(from);
		require(at != std::string::npos, "Placement fixture substitution.");
		text.replace(at, from.size(), to);
		write(saved, text);
		fails([&] { loadGlbImportProject(saved, reopened); }, reason);
	};
	corrupt("scale=\"2\"", "scale=\"0\"", "Scale");
	corrupt("axis-z=\"1\"", "axis-z=\"0\"", "unit vector");
	corrupt("confirmed=\"1\"", "confirmed=\"yes\"", "0 or 1");
	corrupt("scale=\"2\"", "scale=\"2,5\"", "numeric");
	write(saved, xml);

	const auto legacyPath = wxFileName(output, "legacy.vwtimport.xml").GetFullPath();
	const std::string legacy = "<glb-import version=\"1\" source=\"static_import.glb\" sha256=\"" + model.sourceSha256 +
		"\" model=\"w_ar_legacy\" template=\"WEAPON_CARBINERIFLE\">"
		"<part index=\"0\" node=\"1\" primitive=\"0\" role=\"body\"/>"
		"<part index=\"1\" node=\"2\" primitive=\"0\" role=\"excluded\"/></glb-import>";
	write(legacyPath, legacy);
	const auto migrated = loadGlbImportProject(legacyPath, reopened);
	require(migrated.roles == project.roles && !migrated.alignment.confirmed && migrated.alignment.scale == 1,
		"Version 1 migration preserves roles and does not invent alignment.");
	require(!migrated.pivots[0].confirmed && migrated.pivots[0].position == std::array<float,3>({11,-33,22}),
		"Legacy pivot uses source transform, not a guessed centre.");
	saveGlbImportProject(legacyPath, reopened, migrated);
	require(read(legacyPath).find("version=\"2\"") != std::string::npos, "Legacy projects upgrade on save.");

	PreviewModel pick;
	PreviewMesh triangle; triangle.pos = {-1,0,-1, 1,0,-1, 0,0,1}; triangle.idx = {0,1,2};
	pick.meshes.push_back(triangle);
	for (size_t v = 0; v < triangle.pos.size()/3; ++v) triangle.pos[v*3+1] = -.2f;
	pick.meshes.push_back(triangle);
	pick.bmin[0] = -1; pick.bmin[1] = -.2f; pick.bmin[2] = -1;
	pick.bmax[0] = 1; pick.bmax[1] = 0; pick.bmax[2] = 1;
	std::array<float, 3> hit;
	require(pickPreviewSurface(pick, 200, 200, 0, 0, 1, {99,99}, hit), "Surface picker should hit.");
	checkNear(hit[1], -.2f);
	checkNear(hit[0], -.5f*std::sqrt(2.01f)/95); checkNear(hit[2], .5f*std::sqrt(2.01f)/95);
	require(!pickPreviewSurface(pick, 200, 200, 0, 0, 1, {0,0}, hit), "Outside triangles should miss.");
	fails([&] { pickPreviewSurface(pick, 0, 200, 0, 0, 1, {99,99}, hit); }, "camera");
	std::cout << "GLB alignment/pivot/motion/picking/version migration tests passed.\n";
}
