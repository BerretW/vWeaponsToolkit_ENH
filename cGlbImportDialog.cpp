#include "cGlbImportDialog.h"
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <map>
#include <set>

cGlbImportDialog::cGlbImportDialog(wxWindow* parent, GlbModel imported, GlbImportProject assignments,
	const wxArrayString& templates, const wxString& savedPath)
	: wxDialog(parent, wxID_ANY, "GLB preparation - " + wxFileName(assignments.sourcePath).GetFullName(),
		wxDefaultPosition, wxSize(1120, 720), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	model(std::move(imported)), project(std::move(assignments)), projectPath(savedPath)
{
	auto* layout = new wxBoxSizer(wxVERTICAL);
	heading = new wxStaticText(this, wxID_ANY, wxEmptyString);
	layout->Add(heading, 0, wxALL | wxEXPAND, 10);
	auto* row = new wxBoxSizer(wxHORIZONTAL);
	parts = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(540, 360), wxLC_REPORT | wxLC_SINGLE_SEL);
	parts->SetName("glb-parts");
	parts->SetMinSize(wxSize(450, 200));
	parts->InsertColumn(0, "Source object / primitive", wxLIST_FORMAT_LEFT, 240);
	parts->InsertColumn(1, "Vertices", wxLIST_FORMAT_RIGHT, 65);
	parts->InsertColumn(2, "Tris", wxLIST_FORMAT_RIGHT, 55);
	parts->InsertColumn(3, "Role", wxLIST_FORMAT_LEFT, 160);
	parts->InsertItem(0, "All parts");
	for (size_t i = 0; i < model.parts.size(); ++i)
	{
		const auto& part = model.parts[i];
		const auto index = parts->InsertItem(static_cast<long>(i+1), wxString::FromUTF8(part.name));
		parts->SetItem(index, 1, wxString::Format("%zu", part.geometry.vertices.size()));
		parts->SetItem(index, 2, wxString::Format("%zu", part.geometry.indices.size()/3));
	}
	preview = new cModelPreviewPanel(this, wxDefaultPosition, wxSize(500, 360));
	preview->SetMinSize(wxSize(400, 200));
	row->Add(parts, 1, wxEXPAND | wxRIGHT, 8); row->Add(preview, 1, wxEXPAND);
	layout->Add(row, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);

	assignmentPanel = new wxPanel(this);
	auto* fields = new wxFlexGridSizer(2, 6, 10);
	fields->AddGrowableCol(1);
	fields->Add(new wxStaticText(assignmentPanel, wxID_ANY, "Output model name"), 0, wxALIGN_CENTER_VERTICAL);
	modelName = new wxTextCtrl(assignmentPanel, wxID_ANY, wxString::FromUTF8(project.modelName));
	modelName->SetName("glb-model-name");
	fields->Add(modelName, 1, wxEXPAND);
	fields->Add(new wxStaticText(assignmentPanel, wxID_ANY, "Planned weapon template"), 0, wxALIGN_CENTER_VERTICAL);
	templateChoice = new wxChoice(assignmentPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, templates);
	templateChoice->SetName("glb-weapon-template");
	if (!templateChoice->SetStringSelection(wxString::FromUTF8(project.weaponTemplate)))
		throw std::invalid_argument("The import project references an unsupported weapon template.");
	fields->Add(templateChoice, 1, wxEXPAND);
	fields->Add(new wxStaticText(assignmentPanel, wxID_ANY, "Role of selected source object"), 0, wxALIGN_CENTER_VERTICAL);
	roleChoice = new wxChoice(assignmentPanel, wxID_ANY);
	roleChoice->SetName("glb-part-role");
	for (const auto& info : glbPartRoles()) roleChoice->Append(info.label);
	fields->Add(roleChoice, 1, wxEXPAND);
	auto* assignLayout = new wxBoxSizer(wxVERTICAL);
	assignLayout->Add(fields, 0, wxEXPAND | wxBOTTOM, 6);
	auto* suggestButton = new wxButton(assignmentPanel, wxID_ANY, "Apply name suggestions to unassigned objects");
	assignLayout->Add(suggestButton, 0, wxBOTTOM, 4);
	assignLayout->Add(new wxStaticText(assignmentPanel, wxID_ANY,
		"A role applies to all material primitives of the source object. Suggestions are not bone bindings; review them."), 0);
	assignmentPanel->SetSizer(assignLayout);
	layout->Add(assignmentPanel, 0, wxEXPAND | wxALL, 10);
	report = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 110), wxTE_MULTILINE | wxTE_READONLY);
	report->SetName("glb-report");
	layout->Add(report, 0, wxEXPAND | wxALL, 10);
	auto* buttons = new wxBoxSizer(wxHORIZONTAL);
	backButton = new wxButton(this, wxID_BACKWARD, "< Back");
	nextButton = new wxButton(this, wxID_FORWARD, "Next: assign parts >");
	auto* saveButton = new wxButton(this, wxID_SAVE, "Save project...");
	auto* closeButton = new wxButton(this, wxID_CLOSE, "Close");
	SetEscapeId(wxID_CLOSE);
	buttons->Add(backButton, 0, wxRIGHT, 6); buttons->Add(nextButton, 0, wxRIGHT, 6);
	buttons->AddStretchSpacer(); buttons->Add(saveButton, 0, wxRIGHT, 6); buttons->Add(closeButton);
	layout->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
	SetSizer(layout); SetMinSize(wxSize(900, 640));
	refreshRoles();
	parts->SetItemState(0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
	setStage(0);

	parts->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& evt) {
		try { selectPart(evt.GetIndex()); }
		catch (const std::exception& error) {
			wxMessageBox(wxString::FromUTF8(error.what()), "GLB preview failed", wxOK | wxICON_ERROR, this);
		}
	});
	roleChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
		try
		{
			if (selectedPart < 0) throw std::runtime_error("Select a source object first.");
			const auto index = roleChoice->GetSelection();
			if (index < 0) throw std::runtime_error("Select a role.");
			assignGlbPartRole(model, project, size_t(selectedPart), glbPartRoles().at(size_t(index)).role);
			dirty = true; refreshRoles();
		}
		catch (const std::exception& error) {
			wxMessageBox(wxString::FromUTF8(error.what()), "Part assignment failed", wxOK | wxICON_ERROR, this);
		}
	});
	modelName->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { dirty = true; });
	templateChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { dirty = true; });
	suggestButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		try { suggest(); }
		catch (const std::exception& error) {
			wxMessageBox(wxString::FromUTF8(error.what()), "Role suggestions failed", wxOK | wxICON_ERROR, this);
		}
	});
	backButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		try { setStage(stage-1); }
		catch (const std::exception& error) {
			wxMessageBox(wxString::FromUTF8(error.what()), "GLB preparation failed", wxOK | wxICON_ERROR, this);
		}
	});
	nextButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		try { next(); }
		catch (const std::exception& error) {
			wxMessageBox(wxString::FromUTF8(error.what()), "Cannot continue", wxOK | wxICON_ERROR, this);
		}
	});
	saveButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { save(); });
	closeButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); });
	Bind(wxEVT_CLOSE_WINDOW, &cGlbImportDialog::close, this);
	CentreOnParent();
}

void cGlbImportDialog::selectPart(long row)
{
	selectedPart = static_cast<int>(row)-1;
	roleChoice->Enable(stage == 1 && selectedPart >= 0);
	if (selectedPart < 0)
	{
		roleChoice->SetSelection(wxNOT_FOUND);
		preview->showModel(makeGlbPreview(model));
		return;
	}
	const auto& part = model.parts.at(size_t(selectedPart));
	const auto role = project.roles.at(size_t(selectedPart));
	for (size_t i = 0; i < glbPartRoles().size(); ++i)
		if (glbPartRoles()[i].role == role) roleChoice->SetSelection(static_cast<int>(i));
	std::vector<size_t> object;
	for (size_t i = 0; i < model.parts.size(); ++i)
		if (model.parts[i].nodeIndex == part.nodeIndex) object.push_back(i);
	wxBusyCursor busy;
	auto view = makeGlbPreview(model, object);
	view.info += ", " + model.materials.at(part.geometry.material).name;
	preview->showModel(std::move(view));
}

void cGlbImportDialog::refreshRoles()
{
	for (size_t i = 0; i < project.roles.size(); ++i)
	{
		parts->SetItem(static_cast<long>(i+1), 3, glbPartRoleInfo(project.roles[i]).label);
		parts->SetItemTextColour(static_cast<long>(i+1),
			project.roles[i] == GlbPartRole::Unassigned ? wxColour(170, 60, 0) :
			project.roles[i] == GlbPartRole::Excluded ? wxColour(110, 110, 110) : wxColour(0, 100, 40));
	}
}

void cGlbImportDialog::readMetadata()
{
	project.modelName = normalizeAssetName(modelName->GetValue().ToStdString(), "Model");
	modelName->ChangeValue(wxString::FromUTF8(project.modelName));
	if (templateChoice->GetSelection() == wxNOT_FOUND) throw std::runtime_error("Select a weapon template.");
	project.weaponTemplate = templateChoice->GetStringSelection().ToStdString();
}

void cGlbImportDialog::setStage(int value)
{
	if (value < 0 || value > 2) throw std::invalid_argument("Invalid GLB preparation step.");
	stage = value;
	assignmentPanel->Show(stage == 1);
	parts->Enable(stage != 2);
	backButton->Enable(stage > 0);
	nextButton->Enable(stage < 2);
	nextButton->SetLabel(stage == 0 ? "Next: assign parts >" : stage == 1 ? "Next: review >" : "Export not yet available");
	wxString text;
	if (stage == 2)
	{
		heading->SetLabel("Step 3 / 3 - Review the assigned assembly. This is preparation data, NOT a rigged weapon or FiveM resource.");
		auto view = makeGlbPreview(model, includedGlbParts(model, project));
		std::map<GlbPartRole, std::set<size_t>> objects;
		for (size_t i = 0; i < model.parts.size(); ++i) objects[project.roles[i]].insert(model.parts[i].nodeIndex);
		text = "Model: " + wxString::FromUTF8(project.modelName) + "\nTemplate: " + wxString::FromUTF8(project.weaponTemplate) + "\n";
		for (const auto& info : glbPartRoles())
			if (!objects[info.role].empty())
				text += wxString::FromUTF8(info.label) + wxString::Format(": %zu source objects\n", objects[info.role].size());
		text += wxString::Format("Included size (metres): %.4f x %.4f x %.4f\n",
			view.bmax[0]-view.bmin[0], view.bmax[1]-view.bmin[1], view.bmax[2]-view.bmin[2]);
		text += "Source pivots/transforms are retained, not calibrated. Bones, material conversion, alignment and LODs remain pending.\n"
			"Save the project to resume later. No .ydr/.ytd or resource is generated by this step.\n\n";
		preview->showModel(std::move(view));
	}
	else
	{
		heading->SetLabel(stage == 0 ?
			"Step 1 / 3 - Inspect the GLB. Select an object on the left; drag to rotate, wheel to zoom. Units: metres, Z-up." :
			"Step 2 / 3 - Assign every object a role, or explicitly exclude it. At least one Body object is required.");
		selectPart(selectedPart+1);
	}
	for (const auto& item : model.warnings) text += "- " + wxString::FromUTF8(item) + "\n";
	report->ChangeValue(text);
	Layout();
}

void cGlbImportDialog::next()
{
	if (stage == 1)
	{
		readMetadata();
		const auto errors = glbAssignmentErrors(model, project);
		if (!errors.empty())
		{
			wxString message;
			for (const auto& error : errors) message += "- " + wxString::FromUTF8(error) + "\n";
			wxMessageBox(message, "Part assignments incomplete", wxOK | wxICON_WARNING, this);
			return;
		}
	}
	setStage(stage+1);
}

void cGlbImportDialog::suggest()
{
	const auto proposed = suggestGlbPartRoles(model);
	size_t applied = 0;
	for (size_t i = 0; i < proposed.size(); ++i)
		if (project.roles[i] == GlbPartRole::Unassigned && proposed[i] != GlbPartRole::Unassigned)
		{
			assignGlbPartRole(model, project, i, proposed[i]); ++applied;
		}
	if (applied) dirty = true;
	refreshRoles();
	selectPart(selectedPart+1);
	wxMessageBox(wxString::Format("Suggested roles applied to %zu source objects.\n", applied) +
		"Review the roles and assign remaining objects manually. Fixed stock/sights may be Body; movable parts must stay separate.",
		"Review role suggestions", wxOK | wxICON_INFORMATION, this);
}

bool cGlbImportDialog::save()
{
	try
	{
		readMetadata();
		const wxFileName initial(projectPath.empty() ? project.sourcePath : projectPath);
		const auto filename = projectPath.empty() ? initial.GetName() + ".vwtimport.xml" : initial.GetFullName();
		wxFileDialog output(this, "Save GLB import project (not game assets)", initial.GetPath(), filename,
			"GLB import project (*.vwtimport.xml)|*.vwtimport.xml", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
		if (output.ShowModal() != wxID_OK) return false;
		saveGlbImportProject(output.GetPath(), model, project);
		projectPath = output.GetPath(); dirty = false;
		SetTitle("GLB preparation - " + wxFileName(projectPath).GetFullName());
		return true;
	}
	catch (const std::exception& error)
	{
		wxMessageBox(wxString::FromUTF8(error.what()), "Project save failed", wxOK | wxICON_ERROR, this);
		return false;
	}
}

void cGlbImportDialog::close(wxCloseEvent& evt)
{
	if (dirty)
	{
		const int answer = wxMessageBox("Save the changed part assignments before closing?",
			"Unsaved GLB import project", wxYES_NO | wxCANCEL | wxICON_QUESTION, this);
		if (answer == wxCANCEL || (answer == wxYES && !save()))
		{
			if (evt.CanVeto()) evt.Veto();
			return;
		}
	}
	EndModal(wxID_CLOSE);
}
