#include "cGlbImportDialog.h"
#include "cGlbPlacement.h"
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <map>
#include <set>
#include <cmath>
#include <limits>

namespace
{
	std::array<wxTextCtrl*, 3> vectorFields(wxWindow* parent, wxFlexGridSizer* grid, const char* label, const char* name)
	{
		grid->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
		std::array<wxTextCtrl*, 3> fields;
		for (unsigned k = 0; k < 3; ++k)
		{
			fields[k] = new wxTextCtrl(parent, wxID_ANY, "0");
			fields[k]->SetName(wxString::FromUTF8(name) + "-" + wxString("xyz"[k]));
			grid->Add(fields[k], 1, wxEXPAND);
		}
		return fields;
	}
	float value(wxTextCtrl* field)
	{
		double number;
		if (!field->GetValue().ToDouble(&number) || !std::isfinite(number) || std::fabs(number) > 1000000)
			throw std::invalid_argument("Enter a finite numeric value for " + field->GetName().ToStdString());
		return static_cast<float>(number);
	}
	std::array<float, 3> values(const std::array<wxTextCtrl*, 3>& fields)
	{
		return {value(fields[0]), value(fields[1]), value(fields[2])};
	}
	void fill(const std::array<wxTextCtrl*, 3>& fields, const std::array<float, 3>& vector)
	{
		for (unsigned k = 0; k < 3; ++k) fields[k]->ChangeValue(wxString::Format("%.9g", double(vector[k])));
	}
}

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
	preview->SetName("glb-preview");
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
	buildPlacementPanel(layout);
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
		if (changingSelection) return;
		try { selectPart(evt.GetIndex()); }
		catch (const std::exception& error) {
			changingSelection = true;
			parts->SetItemState(selectedPart+1, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
			changingSelection = false;
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
		try { if (stage == 2) applyPlacement(); setStage(stage-1); }
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

void cGlbImportDialog::buildPlacementPanel(wxBoxSizer* layout)
{
	placementPanel = new wxPanel(this);
	placementTabs = new wxNotebook(placementPanel, wxID_ANY);
	placementTabs->SetName("glb-placement-tabs");
	auto* globalPage = new wxPanel(placementTabs);
	pivotPage = new wxPanel(placementTabs);
	placementTabs->AddPage(globalPage, "Model alignment");
	placementTabs->AddPage(pivotPage, "Object pivot / test motion");
	auto* global = new wxBoxSizer(wxVERTICAL);
	auto* top = new wxBoxSizer(wxHORIZONTAL);
	top->Add(new wxStaticText(globalPage, wxID_ANY, "Uniform scale"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
	scaleField = new wxTextCtrl(globalPage, wxID_ANY, "1", wxDefaultPosition, wxSize(90, -1));
	scaleField->SetName("glb-scale"); top->Add(scaleField, 0, wxRIGHT, 12);
	top->Add(new wxStaticText(globalPage, wxID_ANY, "Current barrel axis (Z up)"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
	auto* preset = new wxChoice(globalPage, wxID_ANY);
	for (const auto* text : {"+X", "-X", "+Y", "-Y"}) preset->Append(text);
	preset->SetSelection(0); top->Add(preset, 0, wxRIGHT, 6);
	auto* yaw = new wxButton(globalPage, wxID_ANY, "Yaw preset -> +X"); top->Add(yaw);
	global->Add(top, 0, wxEXPAND | wxBOTTOM, 4);
	auto* grid = new wxFlexGridSizer(4, 4, 6);
	for (unsigned k = 1; k < 4; ++k) grid->AddGrowableCol(k);
	rotationFields = vectorFields(globalPage, grid, "Rotation XYZ (degrees)", "glb-rotation");
	originFields = vectorFields(globalPage, grid, "Source anchor XYZ (m)", "glb-origin");
	offsetFields = vectorFields(globalPage, grid, "Output offset XYZ (m)", "glb-offset");
	global->Add(grid, 0, wxEXPAND);
	auto* actions = new wxBoxSizer(wxHORIZONTAL);
	auto* apply = new wxButton(globalPage, wxID_ANY, "Apply placement");
	apply->SetName("glb-apply-placement");
	auto* confirm = new wxButton(globalPage, wxID_ANY, "Confirm alignment");
	confirm->SetName("glb-confirm-alignment");
	actions->Add(apply, 0, wxRIGHT, 6); actions->Add(confirm);
	global->Add(actions, 0, wxTOP, 4);
	global->Add(new wxStaticText(globalPage, wxID_ANY, "Right-click a visible surface to set the source anchor. No automatic centering or resizing."), 0, wxTOP, 4);
	auto* globalBorder = new wxBoxSizer(wxVERTICAL); globalBorder->Add(global, 1, wxEXPAND | wxALL, 6);
	globalPage->SetSizer(globalBorder);

	auto* local = new wxBoxSizer(wxVERTICAL);
	auto* pivotGrid = new wxFlexGridSizer(4, 4, 6);
	for (unsigned k = 1; k < 4; ++k) pivotGrid->AddGrowableCol(k);
	pivotFields = vectorFields(pivotPage, pivotGrid, "Pivot XYZ (source metres)", "glb-pivot");
	axisFields = vectorFields(pivotPage, pivotGrid, "Hinge / slide axis (unit)", "glb-axis");
	local->Add(pivotGrid, 0, wxEXPAND);
	auto* pivotActions = new wxBoxSizer(wxHORIZONTAL);
	auto* source = new wxButton(pivotPage, wxID_ANY, "Use source object origin");
	auto* confirmPivot = new wxButton(pivotPage, wxID_ANY, "Apply / confirm pivot");
	confirmPivot->SetName("glb-confirm-pivot");
	axisChoice = new wxChoice(pivotPage, wxID_ANY);
	for (const auto* text : {"Custom axis", "X", "Y", "Z"}) axisChoice->Append(text);
	axisChoice->SetSelection(0);
	pivotActions->Add(source, 0, wxRIGHT, 6); pivotActions->Add(axisChoice, 0, wxRIGHT, 6); pivotActions->Add(confirmPivot);
	local->Add(pivotActions, 0, wxTOP, 4);
	auto* test = new wxBoxSizer(wxHORIZONTAL);
	motionType = new wxChoice(pivotPage, wxID_ANY);
	motionType->Append("Rotate (degrees)"); motionType->Append("Slide (output mm)"); motionType->SetSelection(0);
	motionType->SetName("glb-motion-type"); test->Add(motionType, 0, wxRIGHT, 6);
	motionSlider = new wxSlider(pivotPage, wxID_ANY, 0, -45, 45);
	motionSlider->SetName("glb-motion"); test->Add(motionSlider, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
	auto* reset = new wxButton(pivotPage, wxID_ANY, "Reset test"); test->Add(reset);
	local->Add(test, 0, wxEXPAND | wxTOP, 4);
	local->Add(new wxStaticText(pivotPage, wxID_ANY, "Select an included object on the left. Right-click a surface to pick its pivot. Reset test motion before picking."), 0, wxTOP, 4);
	auto* localBorder = new wxBoxSizer(wxVERTICAL); localBorder->Add(local, 1, wxEXPAND | wxALL, 6);
	pivotPage->SetSizer(localBorder);
	auto* container = new wxBoxSizer(wxVERTICAL);
	container->Add(placementTabs, 0, wxEXPAND);
	placementStatus = new wxStaticText(placementPanel, wxID_ANY, wxEmptyString);
	placementStatus->SetName("glb-placement-status");
	container->Add(placementStatus, 0, wxTOP, 4);
	placementPanel->SetSizer(container);
	layout->Add(placementPanel, 0, wxEXPAND | wxALL, 10);
	auto changed = [this](wxCommandEvent&) { dirty = true; placementStatus->SetLabel("Pending edits: Apply or confirm before continuing."); };
	scaleField->Bind(wxEVT_TEXT, changed);
	for (const auto* fields : {&rotationFields, &originFields, &offsetFields, &pivotFields, &axisFields})
		for (auto* field : *fields) field->Bind(wxEVT_TEXT, changed);
	apply->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { applyPlacementFromUi(); });
	confirm->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { applyPlacementFromUi(true); });
	confirmPivot->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { applyPlacementFromUi(false, true); });
	yaw->Bind(wxEVT_BUTTON, [this, preset](wxCommandEvent&) {
		const float angles[4] = {0, 180, -90, 90};
		fill(rotationFields, {0, 0, angles[preset->GetSelection()]}); applyPlacementFromUi();
	});
	source->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		if (selectedPart < 0 || project.roles[size_t(selectedPart)] == GlbPartRole::Excluded)
		{
			wxMessageBox("Select an included source object first.", "Pivot selection", wxOK | wxICON_WARNING, this);
			return;
		}
		const auto& m = model.parts.at(size_t(selectedPart)).sourceWorldTransform;
		fill(pivotFields, {m[12], -m[14], m[13]}); applyPlacementFromUi();
	});
	axisChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
		if (axisChoice->GetSelection() <= 0) return;
		std::array<float, 3> axis = {};
		axis[size_t(axisChoice->GetSelection()-1)] = 1;
		fill(axisFields, axis); applyPlacementFromUi();
	});
	reset->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { motionSlider->SetValue(0); applyPlacementFromUi(); });
	motionSlider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) { applyPlacementFromUi(); });
	motionType->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { motionSlider->SetValue(0); applyPlacementFromUi(); });
	placementTabs->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent& evt) {
		if (stage == 2) { motionSlider->SetValue(0); applyPlacementFromUi(); }
		evt.Skip();
	});
}

void cGlbImportDialog::loadPlacementFields()
{
	scaleField->ChangeValue(wxString::Format("%.9g", double(project.alignment.scale)));
	fill(rotationFields, project.alignment.rotationDegrees); fill(originFields, project.alignment.origin);
	fill(offsetFields, project.alignment.offset);
	const bool enabled = selectedPart >= 0 && project.roles[size_t(selectedPart)] != GlbPartRole::Excluded;
	pivotPage->Enable(enabled);
	const GlbPivot pivot = enabled ? project.pivots.at(size_t(selectedPart)) : GlbPivot();
	fill(pivotFields, pivot.position); fill(axisFields, pivot.axis);
	axisChoice->SetSelection(0);
	for (unsigned k = 0; k < 3; ++k)
	{
		std::array<float, 3> axis = {}; axis[k] = 1;
		if (pivot.axis == axis) axisChoice->SetSelection(int(k+1));
	}
	placementStatus->SetLabel(wxString::Format("Alignment confirmed: %s | Selected pivot: %s | Temporary test: %d %s",
		project.alignment.confirmed ? "yes" : "no", enabled ? (pivot.confirmed ? "yes" : "no") : "select an included object",
		motionSlider->GetValue(), motionType->GetSelection() == 0 ? "degrees" : "mm"));
}

void cGlbImportDialog::applyPlacement(bool confirmAlignment, bool confirmPivot)
{
	if (confirmPivot && (selectedPart < 0 || project.roles[size_t(selectedPart)] == GlbPartRole::Excluded))
		throw std::runtime_error("Select an included object before confirming a pivot.");
	auto candidate = project;
	candidate.alignment.scale = value(scaleField);
	candidate.alignment.rotationDegrees = values(rotationFields);
	candidate.alignment.origin = values(originFields); candidate.alignment.offset = values(offsetFields);
	auto expected = candidate.alignment; expected.confirmed = project.alignment.confirmed;
	if (!(expected == project.alignment)) candidate.alignment.confirmed = false;
	if (confirmAlignment) candidate.alignment.confirmed = true;
	if (selectedPart >= 0 && project.roles[size_t(selectedPart)] != GlbPartRole::Excluded)
	{
		auto pivot = candidate.pivots.at(size_t(selectedPart));
		pivot.position = values(pivotFields); pivot.axis = values(axisFields);
		if (!(pivot == project.pivots[size_t(selectedPart)])) pivot.confirmed = false;
		if (confirmPivot) pivot.confirmed = true;
		assignGlbPivot(model, candidate, size_t(selectedPart), pivot);
	}
	validateGlbPlacement(candidate);
	GlbMotionPreview motion;
	if (motionType->GetSelection() == 0) motion.rotationDegrees = float(motionSlider->GetValue());
	else motion.slideMetres = motionSlider->GetValue()*0.001f;
	auto view = makePreparedGlbPreview(model, candidate, selectedPart, motion);
	if (!(candidate.alignment == project.alignment) || candidate.pivots != project.pivots) dirty = true;
	project = std::move(candidate);
	motionRendered = motion.rotationDegrees != 0 || motion.slideMetres != 0;
	loadPlacementFields();
	preview->showModel(std::move(view), true);
}

void cGlbImportDialog::applyPlacementFromUi(bool confirmAlignment, bool confirmPivot)
{
	try { applyPlacement(confirmAlignment, confirmPivot); }
	catch (const std::exception& error)
	{
		wxMessageBox(wxString::FromUTF8(error.what()), "Placement could not be applied", wxOK | wxICON_ERROR, this);
	}
}

void cGlbImportDialog::pickedPoint(const std::array<float, 3>& point)
{
	if (motionRendered || motionSlider->GetValue() != 0) throw std::runtime_error("Reset test motion before selecting a surface point.");
	const auto source = glbSourcePoint(project.alignment, point);
	if (placementTabs->GetSelection() == 0) fill(originFields, source);
	else
	{
		if (selectedPart < 0 || project.roles[size_t(selectedPart)] == GlbPartRole::Excluded)
			throw std::runtime_error("Select an included object before setting its pivot.");
		fill(pivotFields, source);
	}
	applyPlacement();
}

void cGlbImportDialog::selectPart(long row)
{
	if (stage == 2) applyPlacement();
	selectedPart = static_cast<int>(row)-1;
	if (stage == 2)
	{
		motionSlider->SetValue(0);
		loadPlacementFields();
		motionRendered = false;
		preview->showModel(makePreparedGlbPreview(model, project, selectedPart), true);
		return;
	}
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
	if (value < 0 || value > 3) throw std::invalid_argument("Invalid GLB preparation step.");
	stage = value;
	assignmentPanel->Show(stage == 1);
	placementPanel->Show(stage == 2);
	parts->Enable(stage != 3);
	backButton->Enable(stage > 0);
	nextButton->Enable(stage < 3);
	nextButton->SetLabel(stage == 0 ? "Next: assign parts >" : stage == 1 ? "Next: align model >" :
		stage == 2 ? "Next: review >" : "Export not yet available");
	preview->setPointPickedHandler({});
	wxString text;
	if (stage == 3)
	{
		heading->SetLabel("Step 4 / 4 - Review the aligned assembly. Confirmed pivots are authoring data, NOT a GTA skeleton or FiveM resource.");
		auto view = makePreparedGlbPreview(model, project);
		std::map<GlbPartRole, std::set<size_t>> objects;
		for (size_t i = 0; i < model.parts.size(); ++i) objects[project.roles[i]].insert(model.parts[i].nodeIndex);
		text = "Model: " + wxString::FromUTF8(project.modelName) + "\nTemplate: " + wxString::FromUTF8(project.weaponTemplate) + "\n";
		for (const auto& info : glbPartRoles())
			if (!objects[info.role].empty())
				text += wxString::FromUTF8(info.label) + wxString::Format(": %zu source objects\n", objects[info.role].size());
		text += wxString::Format("Included size (metres): %.4f x %.4f x %.4f\n",
			view.bmax[0]-view.bmin[0], view.bmax[1]-view.bmin[1], view.bmax[2]-view.bmin[2]);
		text += wxString::Format("Alignment scale: %.6g; rotation XYZ: %.3f, %.3f, %.3f degrees\n",
			double(project.alignment.scale), double(project.alignment.rotationDegrees[0]),
			double(project.alignment.rotationDegrees[1]), double(project.alignment.rotationDegrees[2]));
		text += "Alignment/pivots are confirmed by the author, not checked in-game. Bone binding, material conversion and LODs remain pending.\n"
			"Save the project to resume later. No .ydr/.ytd or resource is generated by this step.\n\n";
		preview->showModel(std::move(view));
	}
	else if (stage == 2)
	{
		heading->SetLabel("Step 3 / 4 - Align the model and confirm moving-part pivots. Right-click a surface to pick an anchor/pivot; X red, Y green, Z blue.");
		motionSlider->SetValue(0);
		motionRendered = false;
		loadPlacementFields();
		preview->showModel(makePreparedGlbPreview(model, project, selectedPart));
		preview->setPointPickedHandler([this](const std::array<float, 3>& point) { pickedPoint(point); });
		text = "Placement uses source Z-up metres. Apply edits or confirm before continuing. The muzzle direction in the supplied carbine reference is +X.\n"
			"Root = offset + Rz*Ry*Rx*(scale*(source-origin)). Picked points are converted back to source coordinates.\n"
			"Motion testing is temporary; it is not saved as a pose or animation. Pivots are not guessed from bounding-box centres.\n\n";
	}
	else
	{
		heading->SetLabel(stage == 0 ?
			"Step 1 / 4 - Inspect the source GLB. Select an object on the left; drag to rotate, wheel to zoom. Units: metres, Z-up." :
			"Step 2 / 4 - Assign every object a role, or explicitly exclude it. At least one Body object is required.");
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
	if (stage == 2)
	{
		applyPlacement();
		const auto errors = glbPlacementErrors(model, project);
		if (!errors.empty())
		{
			wxString message;
			for (const auto& error : errors) message += "- " + wxString::FromUTF8(error) + "\n";
			wxMessageBox(message, "Placement needs confirmation", wxOK | wxICON_WARNING, this);
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
		if (stage == 2) applyPlacement();
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
