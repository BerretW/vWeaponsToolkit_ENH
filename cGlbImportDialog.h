#pragma once
#include "cGlbImportProject.h"
#include <wx/listctrl.h>
#include <wx/notebook.h>

class cGlbImportDialog : public wxDialog
{
public:
	cGlbImportDialog(wxWindow* parent, GlbModel model, GlbImportProject project,
		const wxArrayString& templates, const wxString& projectPath = wxEmptyString);

private:
	GlbModel model;
	GlbImportProject project;
	wxString projectPath;
	int stage = 0;
	int selectedPart = -1;
	bool dirty = false;
	bool changingSelection = false;
	bool motionRendered = false;
	wxStaticText* heading;
	wxListCtrl* parts;
	cModelPreviewPanel* preview;
	wxPanel* assignmentPanel;
	wxChoice* roleChoice;
	wxChoice* templateChoice;
	wxTextCtrl* modelName;
	wxTextCtrl* report;
	wxButton* backButton;
	wxButton* nextButton;
	wxPanel* placementPanel;
	wxNotebook* placementTabs;
	wxTextCtrl* scaleField;
	std::array<wxTextCtrl*, 3> rotationFields, originFields, offsetFields, pivotFields, axisFields;
	wxPanel* pivotPage;
	wxChoice* motionType;
	wxChoice* axisChoice;
	wxSlider* motionSlider;
	wxStaticText* placementStatus;

	void setStage(int value);
	void selectPart(long row);
	void refreshRoles();
	void readMetadata();
	void next();
	void suggest();
	bool save();
	void close(wxCloseEvent& evt);
	void buildPlacementPanel(wxBoxSizer* layout);
	void loadPlacementFields();
	void applyPlacement(bool confirmAlignment = false, bool confirmPivot = false);
	void applyPlacementFromUi(bool confirmAlignment = false, bool confirmPivot = false);
	void pickedPoint(const std::array<float, 3>& point);
};
