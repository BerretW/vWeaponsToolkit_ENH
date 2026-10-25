#include "cApp.h"
#include <wx/stdpaths.h>
#include <wx/filename.h>

wxIMPLEMENT_APP(cApp);

bool cApp::OnInit()
{
	wxInitAllImageHandlers();

	// Templates and icon are loaded via relative paths, so run from the exe folder.
	wxSetWorkingDirectory(wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath());

	if (!wxDirExists("templates"))
	{
		wxMessageBox("The 'templates' folder was not found next to the executable.", "vWeaponsToolkit", wxOK | wxICON_ERROR);
		return false;
	}

	m_frame1 = new cWeaponsToolkit();
	m_frame1->Show();

	return true;
}
