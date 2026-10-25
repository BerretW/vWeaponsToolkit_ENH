#include <wx/wx.h>
#include <wx/file.h>
#include <wx/filename.h>
#include "cGlbImportDialog.h"
#include "cRscWriter.h"
#include <functional>
#include <iostream>
#include <Windows.h>

namespace
{
	void require(bool ok, const char* message)
	{
		if (!ok) throw std::runtime_error(message);
	}
	void fails(const std::function<void()>& action, const std::string& reason)
	{
		try { action(); }
		catch (const std::exception& error)
		{
			if (std::string(error.what()).find(reason) == std::string::npos)
				throw std::runtime_error("Expected " + reason + ", got: " + error.what());
			return;
		}
		throw std::runtime_error("Expected project validation failure: " + reason);
	}
	std::string read(const wxString& path)
	{
		wxFile file(path);
		require(file.IsOpened(), "Test input could not open.");
		std::string result(static_cast<size_t>(file.Length()), '\0');
		require(file.Read(&result[0], result.size()) == wxFileOffset(result.size()), "Test read failed.");
		return result;
	}
	void write(const wxString& path, const std::string& text)
	{
		cRscWriter::save(path, std::vector<uint8_t>(text.begin(), text.end()));
	}
	void replace(std::string& text, const std::string& from, const std::string& to)
	{
		const auto at = text.find(from);
		require(at != std::string::npos, "Project fixture replacement missing.");
		text.replace(at, from.size(), to);
	}
	void button(wxWindow* window, int id)
	{
		auto* control = window->FindWindow(id);
		require(control && control->IsEnabled(), "Wizard button missing/disabled.");
		wxCommandEvent event(wxEVT_BUTTON, id);
		event.SetEventObject(control);
		control->GetEventHandler()->ProcessEvent(event);
	}
	void capture(wxDialog& dialog, const wxString& path)
	{
		wxTheApp->Yield(true);
		dialog.Update();
		wxBitmap bitmap(dialog.GetSize());
		wxMemoryDC dc(bitmap);
		const bool painted = ::PrintWindow(static_cast<HWND>(dialog.GetHandle()), static_cast<HDC>(dc.GetHDC()), 2) != 0;
		dc.SelectObject(wxNullBitmap);
		require(painted && bitmap.SaveFile(path, wxBITMAP_TYPE_PNG), "Could not capture own wizard window.");
	}
	void wizard(const GlbModel& model, const GlbImportProject& project, const wxString& output)
	{
		wxArrayString templates;
		templates.Add("WEAPON_CARBINERIFLE");
		cGlbImportDialog dialog(nullptr, model, project, templates);
		require(dialog.GetEscapeId() == wxID_CLOSE, "Escape must use the guarded Close action.");
		std::exception_ptr failure;
		wxTheApp->CallAfter([&] {
			try
			{
				button(&dialog, wxID_FORWARD);
				auto* list = dynamic_cast<wxListCtrl*>(wxWindow::FindWindowByName("glb-parts", &dialog));
				auto* role = dynamic_cast<wxChoice*>(wxWindow::FindWindowByName("glb-part-role", &dialog));
				require(list && role && list->GetItemCount() == 3, "Wizard object list.");
				for (long row = 1; row <= 2; ++row)
				{
					list->SetItemState(row, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
						wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
					require(list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) == row,
						"Native source-object selection.");
					require(role->IsEnabled(), "Selected object role should be enabled.");
					role->SetStringSelection(row == 1 ? "Body / fixed detail" : "Exclude from weapon");
					wxCommandEvent changed(wxEVT_CHOICE, role->GetId());
					role->GetEventHandler()->ProcessEvent(changed);
				}
				require(list->GetItemText(1, 3) == "Body / fixed detail" &&
					list->GetItemText(2, 3) == "Exclude from weapon", "Wizard role selection.");
				capture(dialog, wxFileName(output, "glb_assignment_ui.png").GetFullPath());
				button(&dialog, wxID_FORWARD);
				require(!dialog.FindWindow(wxID_FORWARD)->IsEnabled() && !list->IsEnabled(), "Review must not offer fake export.");
				auto* report = dynamic_cast<wxTextCtrl*>(wxWindow::FindWindowByName("glb-report", &dialog));
				require(report && report->GetValue().Contains("Included size (metres)") &&
					report->GetValue().Contains("Exclude from weapon: 1"), "Wizard review summary.");
				capture(dialog, wxFileName(output, "glb_review_ui.png").GetFullPath());
				button(&dialog, wxID_BACKWARD);
				require(list->IsEnabled() && list->GetItemText(2, 3) == "Exclude from weapon", "Back must preserve assignments.");
			}
			catch (...) { failure = std::current_exception(); }
			dialog.EndModal(wxID_OK);
		});
		dialog.ShowModal();
		if (failure) std::rethrow_exception(failure);
	}
	void realWizard(const GlbModel& model, const GlbImportProject& project, const wxString& output)
	{
		wxArrayString templates;
		templates.Add("WEAPON_CARBINERIFLE");
		cGlbImportDialog dialog(nullptr, model, project, templates);
		std::exception_ptr failure;
		wxTheApp->CallAfter([&] {
			try
			{
				button(&dialog, wxID_FORWARD);
				capture(dialog, wxFileName(output, "m4a1_assignment_ui.png").GetFullPath());
				button(&dialog, wxID_FORWARD);
				require(!dialog.FindWindow(wxID_FORWARD)->IsEnabled(), "Real GLB review state.");
				capture(dialog, wxFileName(output, "m4a1_review_ui.png").GetFullPath());
			}
			catch (...) { failure = std::current_exception(); }
			dialog.EndModal(wxID_OK);
		});
		dialog.ShowModal();
		if (failure) std::rethrow_exception(failure);
	}
}

void testGlbImportProjects(const wxString& repo, const wxString& output)
{
	const auto source = wxFileName(output, "static_import.glb").GetFullPath();
	const auto originalSource = read(source);
	auto model = loadGlb(source);
	require(model.parts[0].nodeIndex == 1 && model.parts[0].primitiveIndex == 0, "Source identity.");
	require(model.parts[0].nodeNames == std::vector<std::string>({"weapon_root", "body"}), "Source hierarchy.");
	require(model.parts[0].sourceWorldTransform[12] == 11 && model.parts[0].sourceWorldTransform[0] == -2,
		"World transform retained separately from baked vertices.");
	auto project = createGlbImportProject(model, source, "WEAPON_CARBINERIFLE");
	require(project.roles[0] == GlbPartRole::Unassigned && glbAssignmentErrors(model, project).size() == 3,
		"Unassigned objects must not be silently treated as body.");
	const auto suggestions = suggestGlbPartRoles(model);
	require(suggestions == std::vector<GlbPartRole>({GlbPartRole::Body, GlbPartRole::Magazine}), "Role suggestions.");
	require(project.roles[0] == GlbPartRole::Unassigned, "Suggestions require explicit application.");
	const auto path = wxFileName(output, "mapping.vwtimport.xml").GetFullPath();
	saveGlbImportProject(path, model, project);
	GlbModel reopened;
	const auto draft = loadGlbImportProject(path, reopened);
	require(draft.roles == project.roles && reopened.sourceSha256 == model.sourceSha256, "Draft round trip.");
	assignGlbPartRole(model, project, 0, GlbPartRole::Body);
	assignGlbPartRole(model, project, 1, GlbPartRole::Excluded);
	require(glbAssignmentErrors(model, project).empty(), "Complete assignment should validate.");
	require(makeGlbPreview(model, includedGlbParts(model, project)).meshes.size() == 1, "Excluded geometry must not appear in review.");
	saveGlbImportProject(path, model, project);
	const auto completeXml = read(path);
	const auto complete = loadGlbImportProject(path, reopened);
	require(complete.roles == project.roles && complete.weaponTemplate == project.weaponTemplate, "Complete project round trip.");
	const auto portableDir = wxFileName(output, "portable_projects").GetFullPath();
	require(wxDirExists(portableDir) || wxMkdir(portableDir), "Project test folder.");
	const auto nested = wxFileName(portableDir, "nested.vwtimport.xml").GetFullPath();
	saveGlbImportProject(nested, model, project);
	require(loadGlbImportProject(nested, reopened).sourcePath == project.sourcePath, "Relative GLB path resolution.");
	const auto unicodeSource = wxFileName(output, wxString::FromUTF8("source_&_\xC5\xBE.glb")).GetFullPath();
	write(unicodeSource, originalSource);
	auto unicode = project; unicode.sourcePath = unicodeSource;
	saveGlbImportProject(nested, model, unicode);
	require(loadGlbImportProject(nested, reopened).sourcePath == unicodeSource, "Unicode/XML escaping.");
	const auto saved = read(path);
	auto invalid = project; invalid.roles[0] = static_cast<GlbPartRole>(999);
	fails([&] { saveGlbImportProject(path, model, invalid); }, "Unknown part role");
	require(read(path) == saved, "Failed project save must preserve previous file.");
	fails([&] { saveGlbImportProject(source, model, project); }, "cannot overwrite");
	fails([&] { saveGlbImportProject(wxFileName(output, "mapping.xml").GetFullPath(), model, project); }, ".vwtimport.xml");
	require(read(source) == originalSource, "Source GLB must remain unchanged.");
	invalid = project; invalid.sourceSha256[0] = invalid.sourceSha256[0] == '0' ? '1' : '0';
	fails([&] { saveGlbImportProject(path, model, invalid); }, "SHA-256");
	invalid = project; invalid.roles.pop_back();
	fails([&] { glbAssignmentErrors(model, invalid); }, "Part count");
	invalid = project; invalid.modelName = "invalid name";
	fails([&] { saveGlbImportProject(path, model, invalid); }, "Invalid Model");
	invalid = project; invalid.weaponTemplate.clear();
	fails([&] { saveGlbImportProject(path, model, invalid); }, "template");
	auto multi = model; multi.parts.push_back(model.parts[0]); multi.parts.back().primitiveIndex = 1;
	auto grouped = createGlbImportProject(multi, source, project.weaponTemplate);
	assignGlbPartRole(multi, grouped, 0, GlbPartRole::Body);
	require(grouped.roles[2] == GlbPartRole::Body, "All material primitives of an object must share a role.");
	grouped.roles[2] = GlbPartRole::Trigger;
	fails([&] { glbAssignmentErrors(multi, grouped); }, "share a role");
	const auto broken = wxFileName(output, "invalid_mapping.vwtimport.xml").GetFullPath();
	auto corrupt = [&](const std::string& from, const std::string& to, const std::string& reason) {
		auto xml = completeXml; replace(xml, from, to); write(broken, xml);
		fails([&] { loadGlbImportProject(broken, reopened); }, reason);
	};
	corrupt("version=\"1\"", "version=\"99\"", "version");
	corrupt("role=\"body\"", "role=\"unknown\"", "Unknown saved");
	corrupt("index=\"1\"", "index=\"0\"", "identity/order");
	corrupt("node=\"1\"", "node=\"99\"", "identity/order");
	corrupt("role=\"body\"", "role=\"body\" role=\"bolt\"", "duplicate");
	corrupt("</glb-import>", "</incorrect>", "Invalid XML");
	corrupt("<glb-import", "<!DOCTYPE x><glb-import", "without DTD");
	write(broken, completeXml + std::string(1, '\0'));
	fails([&] { loadGlbImportProject(broken, reopened); }, "NUL");
	// Same part names/counts are not sufficient to accept a changed source.
	auto changedSource = originalSource; changedSource.back() ^= 1;
	write(unicodeSource, changedSource);
	fails([&] { loadGlbImportProject(nested, reopened); }, "SHA-256");
	wizard(model, createGlbImportProject(model, source, "WEAPON_CARBINERIFLE"), output);
	std::cout << "GLB part assignment/project persistence/wizard event tests passed.\n";

	const auto realSource = wxFileName(repo + "\\test", "m4a1.glb").GetFullPath();
	if (!wxFileExists(realSource))
	{
		std::cout << "Optional user m4a1.glb fixture not present; real-asset test skipped.\n";
		return;
	}
	const auto real = loadGlb(realSource);
	require(real.parts.size() == 15, "User M4A1 expected 15 primitive instances.");
	auto realProject = createGlbImportProject(real, realSource, "WEAPON_CARBINERIFLE");
	const auto proposed = suggestGlbPartRoles(real);
	size_t suggested = 0;
	bool trigger = false, magazine = false;
	for (size_t i = 0; i < proposed.size(); ++i)
	{
		if (proposed[i] != GlbPartRole::Unassigned) ++suggested;
		if (real.parts[i].name.find("part.013_low") != std::string::npos)
		{
			require(proposed[i] == GlbPartRole::Trigger, "Trigger role must use parent hierarchy.");
			trigger = true;
		}
		if (real.parts[i].name.find("follower_M") != std::string::npos)
		{
			require(proposed[i] == GlbPartRole::Magazine, "Follower must inherit magazine role.");
			magazine = true;
		}
		assignGlbPartRole(real, realProject, i, proposed[i] == GlbPartRole::Unassigned ? GlbPartRole::Body : proposed[i]);
	}
	require(trigger && magazine && suggested > 0 && glbAssignmentErrors(real, realProject).empty(), "Real source roles.");
	const auto realPath = wxFileName(output, "m4a1_preparation.vwtimport.xml").GetFullPath();
	saveGlbImportProject(realPath, real, realProject);
	const auto roundTrip = loadGlbImportProject(realPath, reopened);
	require(roundTrip.roles == realProject.roles && reopened.sourceSha256 == real.sourceSha256, "M4A1 project/source round trip.");
	realWizard(real, realProject, output);
	std::cout << "User M4A1: " << real.parts.size() << " parts; " << suggested << " name suggestions; source SHA256 " << real.sourceSha256 << "\n";
}
