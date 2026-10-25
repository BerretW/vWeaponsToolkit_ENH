#pragma once
#include "cGlbImportProject.h"
#include <wx/listctrl.h>

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

	void setStage(int value);
	void selectPart(long row);
	void refreshRoles();
	void readMetadata();
	void next();
	void suggest();
	bool save();
	void close(wxCloseEvent& evt);
};
