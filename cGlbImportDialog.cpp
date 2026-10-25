#include "cGlbImportDialog.h"
#include "cGlbPlacement.h"
#include "cGlbRig.h"
#include "cGlbResourceExporter.h"
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/dirdlg.h>
#include <wx/stdpaths.h>
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
	progress=new wxStaticText(this,wxID_ANY,wxEmptyString);
	progress->SetName("glb-progress");
	layout->Add(progress,0,wxLEFT|wxRIGHT|wxTOP|wxEXPAND,10);
	heading = new wxStaticText(this, wxID_ANY, wxEmptyString);
	layout->Add(heading, 0, wxLEFT|wxRIGHT|wxTOP | wxEXPAND, 10);
	stepInstructions=new wxStaticText(this,wxID_ANY,wxEmptyString,wxDefaultPosition,wxDefaultSize,wxST_NO_AUTORESIZE);
	stepInstructions->SetName("glb-step-instructions");
	stepInstructions->SetMinSize(wxSize(-1,36));
	layout->Add(stepInstructions,0,wxLEFT|wxRIGHT|wxTOP|wxEXPAND,10);
	readiness=new wxStaticText(this,wxID_ANY,wxEmptyString);
	readiness->SetName("glb-readiness");
	auto* statusRow=new wxBoxSizer(wxHORIZONTAL);
	statusRow->Add(readiness,1,wxALIGN_CENTER_VERTICAL|wxRIGHT,6);
	nextIncomplete=new wxButton(this,wxID_ANY,"Show next unfinished object");
	nextIncomplete->SetName("glb-next-incomplete");
	statusRow->Add(nextIncomplete,0);
	layout->Add(statusRow,0,wxLEFT|wxRIGHT|wxBOTTOM|wxEXPAND,10);
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
	templateChoice->SetToolTip("Choose the closest base-game weapon. Its metadata and reference skeleton must describe the same weapon family.");
	if (!templateChoice->SetStringSelection(wxString::FromUTF8(project.weaponTemplate)))
		throw std::invalid_argument("The import project references an unsupported weapon template.");
	fields->Add(templateChoice, 1, wxEXPAND);
	fields->Add(new wxStaticText(assignmentPanel, wxID_ANY, "Role of selected source object"), 0, wxALIGN_CENTER_VERTICAL);
	roleChoice = new wxChoice(assignmentPanel, wxID_ANY);
	roleChoice->SetName("glb-part-role");
	roleChoice->SetToolTip("What this source object does. This semantic role is separate from the real game bone assigned in step 4.");
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
	buildRigPanel(layout);
	exportPanel=new wxPanel(this);
	auto* outputFields=new wxFlexGridSizer(4,6,8);
	outputFields->AddGrowableCol(1); outputFields->AddGrowableCol(3);
	outputFields->Add(new wxStaticText(exportPanel,wxID_ANY,"Resource name"),0,wxALIGN_CENTER_VERTICAL);
	resourceName=new wxTextCtrl(exportPanel,wxID_ANY,wxString::FromUTF8(project.modelName));
	resourceName->SetName("glb-resource-name"); outputFields->Add(resourceName,1,wxEXPAND);
	outputFields->Add(new wxStaticText(exportPanel,wxID_ANY,"Weapon ID"),0,wxALIGN_CENTER_VERTICAL);
	weaponId=new wxTextCtrl(exportPanel,wxID_ANY,"WEAPON_"+wxString::FromUTF8(project.modelName).Upper());
	weaponId->SetName("glb-weapon-id"); outputFields->Add(weaponId,1,wxEXPAND);
	exportPanel->SetSizer(outputFields); layout->Add(exportPanel,0,wxEXPAND|wxALL,10);
	report = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 65), wxTE_MULTILINE | wxTE_READONLY);
	report->SetName("glb-report");
	layout->Add(report, 0, wxEXPAND | wxALL, 10);
	auto* buttons = new wxBoxSizer(wxHORIZONTAL);
	backButton = new wxButton(this, wxID_BACKWARD, "< Back");
	nextButton = new wxButton(this, wxID_FORWARD, "Next: assign parts >");
	auto* saveButton = new wxButton(this, wxID_SAVE, "Save project...");
	saveButton->SetToolTip("Save editable preparation (.vwtimport.xml), including unfinished work. This does not create game resources.");
	auto* closeButton = new wxButton(this, wxID_CLOSE, "Close");
	auto* helpButton=new wxButton(this,wxID_HELP,"Explain this step...");
	buttons->Add(helpButton,0,wxRIGHT,6);
	exportButton=new wxButton(this,wxID_ANY,"Export Legacy + Enhanced resources...");
	exportButton->SetName("glb-export-resources");
	exportButton->SetToolTip("Create a new folder with Legacy and Enhanced game resources. Existing output is never overwritten. Use one generation on your server.");
	SetEscapeId(wxID_CLOSE);
	buttons->Add(backButton, 0, wxRIGHT, 6); buttons->Add(nextButton, 0, wxRIGHT, 6);
	buttons->AddStretchSpacer(); buttons->Add(exportButton,0,wxRIGHT,6); buttons->Add(saveButton, 0, wxRIGHT, 6); buttons->Add(closeButton);
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
	helpButton->Bind(wxEVT_BUTTON,[this](wxCommandEvent&) { showStepHelp(); });
	nextIncomplete->Bind(wxEVT_BUTTON,[this](wxCommandEvent&) {
		try { focusNextIncomplete(); }
		catch (const std::exception& error) {
			wxMessageBox(wxString::FromUTF8(error.what()),"Cannot select unfinished object",wxOK|wxICON_WARNING,this);
		}
	});
	exportButton->Bind(wxEVT_BUTTON,[this](wxCommandEvent&) { exportResources(); });
	Bind(wxEVT_CLOSE_WINDOW, &cGlbImportDialog::close, this);
	Bind(wxEVT_SIZE,[this](wxSizeEvent& event) {
		if (stepInstructions) refreshGuidance();
		event.Skip();
	});
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
	auto changed = [this](wxCommandEvent&) {
		dirty = true;
		placementStatus->SetLabel("Pending edits: Apply or confirm before continuing.");
		readiness->SetLabel("Pending placement edits. Apply updates the view; Confirm accepts the alignment / selected pivot.");
	};
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
	refreshGuidance();
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

void cGlbImportDialog::buildRigPanel(wxBoxSizer* layout)
{
	rigPanel=new wxPanel(this);
	auto* vertical=new wxBoxSizer(wxVERTICAL);
	auto* actions=new wxBoxSizer(wxHORIZONTAL);
	auto* load=new wxButton(rigPanel,wxID_ANY,"Load reference YDR (clears bone mappings)...");
	load->SetName("glb-load-reference");
	auto* suggest=new wxButton(rigPanel,wxID_ANY,"Suggest unbound bones");
	actions->Add(load,0,wxRIGHT,6); actions->Add(suggest);
	vertical->Add(actions,0,wxBOTTOM,5);
	boneChoice=new wxChoice(rigPanel,wxID_ANY);
	boneChoice->SetName("glb-bone");
	vertical->Add(boneChoice,0,wxEXPAND|wxBOTTOM,5);
	auto* motion=new wxBoxSizer(wxHORIZONTAL);
	rigAxis=new wxChoice(rigPanel,wxID_ANY);
	for (const auto* name:{"Local X","Local Y","Local Z"}) rigAxis->Append(name);
	rigAxis->SetSelection(0); rigAxis->SetName("glb-bone-axis");
	rigMotion=new wxSlider(rigPanel,wxID_ANY,0,-45,45);
	rigMotion->SetName("glb-bone-motion");
	auto* reset=new wxButton(rigPanel,wxID_ANY,"Reset bone test");
	auto* fit=new wxButton(rigPanel,wxID_ANY,"Move bone origin to confirmed pivot");
	fit->SetName("glb-fit-bone");
	motion->Add(rigAxis,0,wxRIGHT,6); motion->Add(rigMotion,1,wxRIGHT|wxALIGN_CENTER_VERTICAL,6); motion->Add(reset);
	vertical->Add(motion,0,wxEXPAND|wxBOTTOM,5); vertical->Add(fit,0,wxBOTTOM,5);
	auto* pickBone=new wxButton(rigPanel,wxID_ANY,"Pick reference bone origin (muzzle / eject / attachments)...");
	pickBone->SetName("glb-pick-bone-origin");
	vertical->Add(pickBone,0,wxBOTTOM,5);
	rigStatus=new wxStaticText(rigPanel,wxID_ANY,wxEmptyString);
	rigStatus->SetName("glb-rig-status");
	vertical->Add(rigStatus,0,wxEXPAND);
	rigPanel->SetSizer(vertical);
	layout->Add(rigPanel,0,wxEXPAND|wxALL,10);
	auto run=[this](const std::function<void()>& action) {
		try { action(); }
		catch (const std::exception& error) {
			wxMessageBox(wxString::FromUTF8(error.what()),"Reference bone operation failed",wxOK|wxICON_ERROR,this);
		}
	};
	load->Bind(wxEVT_BUTTON,[this,run](wxCommandEvent&) {
		wxFileDialog input(this,"Reference YDR for "+wxString::FromUTF8(project.weaponTemplate),
			wxEmptyString,wxEmptyString,"Weapon drawable (*.ydr)|*.ydr",wxFD_OPEN|wxFD_FILE_MUST_EXIST);
		if (input.ShowModal()!=wxID_OK) return;
		run([&] {
			wxBusyCursor busy;
			setGlbReferenceSkeleton(model,project,loadReferenceSkeleton(input.GetPath()));
			selectedReferenceBone=-1;
			dirty=true; rigMotion->SetValue(0); refreshRig(); refreshRigPreview();
		});
	});
	suggest->Bind(wxEVT_BUTTON,[this,run](wxCommandEvent&) {
		run([&] {
			if (project.skeleton.bones.empty()) throw std::runtime_error("Load a reference weapon YDR first.");
			const auto suggestions=suggestGlbBones(model,project);
			for (size_t i=0; i<suggestions.size(); ++i)
				if (project.boneBindings[i]<0 && suggestions[i]>=0) { assignGlbBone(model,project,i,suggestions[i]); dirty=true; }
			refreshRig(); refreshRigPreview();
		});
	});
	boneChoice->Bind(wxEVT_CHOICE,[this,run](wxCommandEvent&) {
		run([&] {
			selectedReferenceBone=boneChoice->GetSelection()-1;
			if (selectedPart>=0)
			{
				assignGlbBone(model,project,size_t(selectedPart),selectedReferenceBone);
				dirty=true;
			}
			rigMotion->SetValue(0); refreshRig(); refreshRigPreview();
		});
	});
	fit->Bind(wxEVT_BUTTON,[this,run](wxCommandEvent&) {
		run([&] {
			if (selectedPart<0) throw std::runtime_error("Select an object with a confirmed pivot.");
			fitGlbBoneToPivot(model,project,size_t(selectedPart));
			dirty=true; rigMotion->SetValue(0); refreshRig(); refreshRigPreview();
		});
	});
	pickBone->Bind(wxEVT_BUTTON,[this,run](wxCommandEvent&) {
		run([&] {
			if (selectedReferenceBone<0 || rigMotion->GetValue()!=0)
				throw std::runtime_error("Select a reference bone and reset test motion before picking its rest origin.");
			const auto bone=selectedReferenceBone;
			rigStatus->SetLabel("Armed: right-click a visible surface to move the selected reference bone origin. Child rest origins will be preserved.");
			preview->setPointPickedHandler([this,bone](const std::array<float,3>& point) {
				if (stage!=3 || selectedReferenceBone!=bone || rigMotion->GetValue()!=0)
					throw std::runtime_error("Bone selection/test pose changed. Arm the bone-origin picker again.");
				moveGlbBoneOrigin(model,project,size_t(bone),point);
				preview->setPointPickedHandler({}); dirty=true; refreshRigPreview();
			});
		});
	});
	rigMotion->Bind(wxEVT_SLIDER,[this,run](wxCommandEvent&) { run([&] { refreshRigPreview(); }); });
	rigAxis->Bind(wxEVT_CHOICE,[this,run](wxCommandEvent&) { run([&] { refreshRigPreview(); }); });
	reset->Bind(wxEVT_BUTTON,[this,run](wxCommandEvent&) { run([&] { rigMotion->SetValue(0); refreshRigPreview(); }); });
}

void cGlbImportDialog::refreshRig()
{
	boneChoice->Clear(); boneChoice->Append("Unbound");
	for (size_t i=0; i<project.skeleton.bones.size(); ++i)
	{
		const auto& bone=project.skeleton.bones[i];
		const auto parent=bone.parent<0 ? std::string("<root>") : project.skeleton.bones[size_t(bone.parent)].name;
		boneChoice->Append(wxString::Format("%zu: ",i)+wxString::FromUTF8(bone.name)+" <- "+
			wxString::FromUTF8(parent)+wxString::Format(" (tag %u)",unsigned(bone.tag)));
	}
	const bool selected=selectedPart>=0 && project.roles[size_t(selectedPart)]!=GlbPartRole::Excluded;
	boneChoice->Enable((selected || selectedPart<0) && !project.skeleton.bones.empty());
	const int bone=selected ? project.boneBindings.at(size_t(selectedPart)) : selectedPart<0 ? selectedReferenceBone:-1;
	selectedReferenceBone=bone;
	boneChoice->SetSelection(bone+1);
	rigMotion->Enable(bone>=0); rigAxis->Enable(bone>=0);
	rigStatus->SetLabel(project.skeleton.bones.empty() ? "No reference skeleton loaded." :
		wxString::Format("%zu reference bones | ",project.skeleton.bones.size())+
		(bone<0 ? "Select an included object and assign a bone." : "Rest pose is preserved; test rotation uses the real bone hierarchy."));
	refreshGuidance();
}

void cGlbImportDialog::refreshRigPreview()
{
	preview->setPointPickedHandler({});
	if (project.skeleton.bones.empty()) { preview->showModel(makePreparedGlbPreview(model,project),true); return; }
	const int bone=selectedPart>=0 ? project.boneBindings.at(size_t(selectedPart)) : selectedReferenceBone;
	preview->showModel(makeGlbRigPreview(model,project,bone,float(rigMotion->GetValue()),unsigned(rigAxis->GetSelection())),true);
	if (selectedPart>=0 && bone>=0)
	{
		const auto matrices=skeletonWorldMatrices(project.skeleton);
		const auto origin=bonePoint(matrices[size_t(bone)],{0,0,0});
		const auto pivot=glbAlignedPoint(project.alignment,project.pivots[size_t(selectedPart)].position);
		double distance=0;
		for (unsigned k=0; k<3; ++k) distance+=std::pow(double(origin[k])-pivot[k],2);
		rigStatus->SetLabel(wxString::FromUTF8(project.skeleton.bones[size_t(bone)].name)+
			wxString::Format(" | pivot to bone rest-origin distance: %.2f mm | test: %d degrees",
				std::sqrt(distance)*1000,rigMotion->GetValue()));
	}
	else if (bone>=0)
	{
		const auto matrices=skeletonWorldMatrices(project.skeleton);
		const auto origin=bonePoint(matrices[size_t(bone)],{0,0,0});
		rigStatus->SetLabel(wxString::FromUTF8(project.skeleton.bones[size_t(bone)].name)+
			wxString::Format(" | rest origin (output m): %.3f, %.3f, %.3f | test: %d degrees",
				double(origin[0]),double(origin[1]),double(origin[2]),rigMotion->GetValue()));
	}
}

void cGlbImportDialog::selectPart(long row)
{
	if (stage == 2) applyPlacement();
	selectedPart = static_cast<int>(row)-1;
	if (stage==3)
	{
		rigMotion->SetValue(0);
		refreshRig(); refreshRigPreview();
		return;
	}
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
	if (readiness) refreshGuidance();
}

void cGlbImportDialog::refreshGuidance()
{
	static const char* names[]={"Inspect","Assign parts","Align / pivots","Bind bones","Export"};
	wxString trail;
	for (int i=0;i<5;++i)
	{
		if (i) trail+="  >  ";
		trail+=i==stage ? "["+wxString::FromUTF8(names[i])+"]":wxString::FromUTF8(names[i]);
	}
	progress->SetLabel(trail);
	static const char* instructions[]={
		"Check that the whole assembled weapon is visible and sized in metres. Drag to rotate, wheel to zoom; double-click resets the view.\nNext you will tell the tool which objects are fixed, moving or removable. No source files are changed.",
		"Select an object on the LEFT, then choose its role below. Start with name suggestions, but review each result.\nBody = fixed geometry; Magazine = detachable component; Exclude = intentionally omitted. A role is not a bone.",
		"First align the complete weapon and Confirm alignment. Then select each moving object and open Object pivot / test motion.\nA pivot is the hinge/slide origin; pick a surface with RIGHT-click or enter coordinates. Confirm each required pivot.",
		"Load a weapon YDR matching your template; it supplies actual game bones. Select each object on the LEFT and choose its bone.\nSuggestions fill known names only. Test motion, reset it, and check muzzle/attachment positions before continuing.",
		"Choose a unique resource name and WEAPON_ID, then Export into a parent folder. A NEW <resource>_converted folder is created.\nInstall either the Legacy OR Enhanced child resource and add ensure <resource> to server.cfg. Game testing is still required."
	};
	stepInstructions->SetLabel(wxString::FromUTF8(instructions[stage]));
	stepInstructions->Wrap(std::max(780,GetClientSize().x-24));
	std::set<size_t> seen;
	size_t unassigned=0,pivots=0,unbound=0;
	for (size_t i=0;i<model.parts.size();++i)
	{
		if (!seen.insert(model.parts[i].nodeIndex).second) continue;
		const auto role=project.roles[i];
		if (role==GlbPartRole::Unassigned) ++unassigned;
		if ((role==GlbPartRole::Magazine || role==GlbPartRole::Bolt || role==GlbPartRole::Trigger ||
			role==GlbPartRole::ChargingHandle || role==GlbPartRole::MovingPart) && !project.pivots[i].confirmed) ++pivots;
		if (role!=GlbPartRole::Excluded && project.boneBindings[i]<0) ++unbound;
	}
	wxString state;
	if (stage==0) state=wxString::Format("%zu source objects loaded. Next: classify their purpose.",seen.size());
	else if (stage==1) state=wxString::Format("Still to assign: %zu objects. Body required; exclude unused objects explicitly.",unassigned);
	else if (stage==2) state=wxString::Format("Alignment: %s | Moving-object pivots still to confirm: %zu.",
		project.alignment.confirmed ? "confirmed":"not confirmed",pivots);
	else if (stage==3) state=wxString::Format("Reference bones: %zu | Objects still unbound: %zu. Reload clears bone mappings.",
		project.skeleton.bones.size(),unbound);
	else state="Export creates game files; Save project stores editable preparation only. These are different outputs.";
	readiness->SetLabel(state);
}

void cGlbImportDialog::showStepHelp()
{
	static const char* help[]={
		"GOAL: Check your import before assigning anything.\n\n"
		"The GLB should be assembled, not exploded. All parts shows the entire source; a left-list row isolates one source object, including its material primitives. "
		"Use drag / wheel / double-click for the camera. The preview is diffuse-only, not a game renderer.\n\n"
		"Scale and orientation are corrected in step 3, so do not re-export just to move the weapon. Missing meshes or unsupported features must be resolved first.\n\n"
		"Next: Assign parts. Save project can store a draft at any point; your GLB remains unchanged.",
		"GOAL: Describe what each object does, not which game bone it uses.\n\n"
		"Select a row on the left, then choose a role. Body includes receiver and genuinely fixed details. "
		"Magazine is exported separately and attached to WAPClip. Bolt, trigger and charging handle remain moving parts of the weapon. "
		"Scope / Grip become separate optional components. Excluded parts are intentionally left out.\n\n"
		"Apply name suggestions fills unassigned objects only. Unknown parts still need a manual decision. "
		"All material primitives of one source object share its role. Changing a role clears its old bone binding.\n\n"
		"To continue: assign or exclude every object, and keep at least one Body. Suppressor/flashlight helper rigs are not supported by the current export.",
		"GOAL: Establish size/orientation and the origin of each moving part.\n\n"
		"MODEL ALIGNMENT: Scale is uniform and positive. Rotation is XYZ degrees. Source anchor is an original Z-up point that maps to Output offset. "
		"Right-click a surface to choose the anchor; Apply previews the change, Confirm alignment accepts it. "
		"The supplied carbine barrel points along +X; the preset rotates your declared axis toward +X, it does not detect the barrel.\n\n"
		"OBJECT PIVOT: Select an object on the left, open Object pivot / test motion. A pivot is its hinge or slide origin, not necessarily its centre. "
		"Set source-space position and a unit axis, then confirm. Source object origin is only a starting point: your M4A1 has identity transforms, so these may all be zero.\n\n"
		"Use the slider to test rotation or output-mm sliding. Reset before picking points. The test pose is never saved. "
		"To continue: confirm alignment and every required moving-object pivot.",
		"GOAL: Bind your prepared objects to actual bones from a compatible game weapon.\n\n"
		"Load a reference YDR for the selected weapon template. For the supplied carbine example use w_ar_carbinerifle_m31.ydr. "
		"The GLB source and this reference are DIFFERENT files. Loading a new reference clears mappings.\n\n"
		"Select a source object on the left, then choose its actual bone. Suggestions fill known roles only; other moving parts need an explicit decision. "
		"Every object has one bone at 100% weight. The slider rotates a bone and all its descendants, so inspect which parts move together.\n\n"
		"Move bone origin to confirmed pivot adjusts a rest origin while preserving child origins and the assembled mesh. "
		"With All parts selected you can choose muzzle/eject/attachment bones too; arm Pick reference bone origin and right-click a surface. "
		"This is authoring, not proof that game animations will match.\n\nTo continue: use a current compatible reference and bind every included object.",
		"GOAL: Produce the FiveM resource, then test it in the correct game generation.\n\n"
		"Resource name is the folder/ensure name; Weapon ID is the unique game identifier used by scripts. "
		"Export creates <resource>_converted containing separate Legacy and Enhanced resources. Existing destination folders are never overwritten. "
		"Save project instead stores only editable preparation, not game files.\n\n"
		"Copy ONE generation's child folder into the server resources directory and add ensure <folder> to server.cfg. "
		"Use the configured Weapon ID in your inventory/spawn tooling. Do not enable both generations together.\n\n"
		"conversion-report.txt lists limitations. The resource's nonintrusive client validator logs [vwt-validation] asset checks, "
		"but reloads, attachment alignment, normal-map/tint appearance and animations still need in-game inspection. "
		"Export is opaque/high-LOD-only; unsupported materials or specialized helper rigs produce an explicit error."
	};
	wxMessageBox(wxString::FromUTF8(help[stage]),"GLB to FiveM - step "+wxString::Format("%d / 5",stage+1),
		wxOK|wxICON_INFORMATION,this);
}

void cGlbImportDialog::focusNextIncomplete()
{
	if (stage==2)
	{
		applyPlacement();
		if (!project.alignment.confirmed)
		{
			placementTabs->SetSelection(0);
			wxWindow::FindWindowByName("glb-confirm-alignment",this)->SetFocus();
			readiness->SetLabel("First check the whole weapon's size/orientation, then Confirm alignment. Pivots are next.");
			return;
		}
	}
	if (stage==3 && project.skeleton.bones.empty())
	{
		wxWindow::FindWindowByName("glb-load-reference",this)->SetFocus();
		readiness->SetLabel("First load a reference weapon YDR matching your template. Then assign objects to its real bones.");
		return;
	}
	for (size_t offset=1;offset<=model.parts.size();++offset)
	{
		const auto i=(size_t(selectedPart+1)+offset-1)%model.parts.size();
		const auto role=project.roles[i];
		const bool moving=role==GlbPartRole::Magazine || role==GlbPartRole::Bolt || role==GlbPartRole::Trigger ||
			role==GlbPartRole::ChargingHandle || role==GlbPartRole::MovingPart;
		const bool incomplete=stage==1 ? role==GlbPartRole::Unassigned :
			stage==2 ? moving && !project.pivots[i].confirmed :
			stage==3 ? role!=GlbPartRole::Excluded && project.boneBindings[i]<0:false;
		if (!incomplete) continue;
		parts->SetItemState(long(i+1),wxLIST_STATE_SELECTED|wxLIST_STATE_FOCUSED,wxLIST_STATE_SELECTED|wxLIST_STATE_FOCUSED);
		parts->EnsureVisible(long(i+1));
		if (stage==2) placementTabs->SetSelection(1);
		if (stage==1) roleChoice->SetFocus();
		if (stage==3) boneChoice->SetFocus();
		return;
	}
	refreshGuidance();
	readiness->SetLabel(readiness->GetLabel()+" All objects in this step are resolved.");
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
	if (value < 0 || value > 4) throw std::invalid_argument("Invalid GLB preparation step.");
	stage = value;
	assignmentPanel->Show(stage == 1);
	placementPanel->Show(stage == 2);
	rigPanel->Show(stage==3);
	nextIncomplete->Show(stage>0 && stage<4);
	exportPanel->Show(stage==4); exportButton->Show(stage==4);
	parts->Enable(stage != 4);
	backButton->Enable(stage > 0);
	nextButton->Enable(stage < 4);
	nextButton->SetLabel(stage == 0 ? "Next: assign parts >" : stage == 1 ? "Next: align model >" :
		stage == 2 ? "Next: bind bones >" : stage==3 ? "Next: review >" : "Preparation complete");
	preview->setPointPickedHandler({});
	wxString text;
	if (stage == 4)
	{
		heading->SetLabel("Step 5 / 5 - Export weapon + magazine/scope/grip components. Suppressor/flashlight helper rigs are not supported. In-game testing is still required.");
		const auto bindings=buildGlbRigidBindings(model,project);
		auto view = makeGlbRigPreview(model, project);
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
		text += wxString::Format("Reference: %zu bones, %zu rigidly bound primitives (one bone / 100%% weight per object).\n",
			project.skeleton.bones.size(),bindings.size());
		text += "Export uses opaque weapon shader / approximate PBR conversion, high LOD only, rigid weights. No game animation validation.\n"
			"Save project to resume, or export into a NEW directory. Both generations are produced; use only one resource per server.\n\n";
		preview->showModel(std::move(view));
	}
	else if (stage==3)
	{
		heading->SetLabel("Step 4 / 5 - Load a reference weapon YDR and assign real bones. Test motion affects all objects bound to that bone and descendants.");
		rigMotion->SetValue(0); refreshRig(); refreshRigPreview();
		text="Reference bones are read from your YDR, not guessed from source part names. Loading a reference declares it belongs to the selected weapon template.\n"
			"Name suggestions fill only unbound objects; bolt/charging-handle/other moving parts need explicit decisions. Missing bones are never invented.\n"
			"Magazine/scope/grip export as separate attached models. Suppressor/flashlight need additional component helper rigs and cannot export yet.\n\n";
	}
	else if (stage == 2)
	{
		heading->SetLabel("Step 3 / 5 - Align the model and confirm moving-part pivots. Right-click a surface to pick an anchor/pivot; X red, Y green, Z blue.");
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
			"Step 1 / 5 - Inspect the source GLB. Select an object on the left; drag to rotate, wheel to zoom. Units: metres, Z-up." :
			"Step 2 / 5 - Assign every object a role, or explicitly exclude it. At least one Body object is required.");
		selectPart(selectedPart+1);
	}
	for (const auto& item : model.warnings) text += "- " + wxString::FromUTF8(item) + "\n";
	report->ChangeValue(text);
	refreshGuidance();
	Layout();
}

void cGlbImportDialog::exportResources()
{
	try
	{
		readMetadata();
		WeaponResourceSettings settings;
		settings.modelName=project.modelName; settings.weaponTemplate=project.weaponTemplate;
		settings.resourceName=normalizeAssetName(resourceName->GetValue().ToStdString(),"Resource");
		settings.weaponId=weaponId->GetValue().ToStdString(); settings.displayName=project.modelName;
		wxDirDialog output(this,"Select output parent (a NEW <resource>_converted directory will be created)",
			wxEmptyString,wxDD_DEFAULT_STYLE|wxDD_DIR_MUST_EXIST);
		if (output.ShowModal()!=wxID_OK) return;
		const auto destination=output.GetPath()+"\\"+wxString::FromUTF8(settings.resourceName)+"_converted";
		const auto templates=wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath()+"\\templates";
		std::vector<std::string> diagnostics;
		{
			wxBusyCursor busy;
			diagnostics=exportGlbResources(model,project,destination,templates,settings);
		}
		wxString message="Published:\n"+destination+"\n\nUse the Legacy OR Enhanced resource, not both.\nWeapon ID: "+
			wxString::FromUTF8(settings.weaponId)+"\n\n";
		for (const auto& item:diagnostics) message+="- "+wxString::FromUTF8(item)+"\n";
		wxMessageBox(message,"Resources generated - in-game test required",wxOK|wxICON_INFORMATION,this);
	}
	catch (const std::exception& error)
	{
		wxMessageBox(wxString::FromUTF8(error.what()),"Resource export failed",wxOK|wxICON_ERROR,this);
	}
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
	if (stage==3)
	{
		const auto errors=glbRigErrors(model,project);
		if (!errors.empty())
		{
			wxString text;
			for (const auto& item:errors) text+="- "+wxString::FromUTF8(item)+"\n";
			wxMessageBox(text,"Bone bindings incomplete",wxOK|wxICON_WARNING,this);
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
