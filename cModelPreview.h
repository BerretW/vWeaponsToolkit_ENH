#pragma once
#include <wx/wx.h>
#include <cstdint>
#include <string>
#include <vector>

struct PreviewTexture
{
	int width = 0;
	int height = 0;
	std::vector<uint8_t> rgba;
};

struct PreviewMesh
{
	std::vector<float> pos;
	std::vector<float> nrm;
	std::vector<float> uv;
	std::vector<uint32_t> idx;
	int texture = -1;
};

struct PreviewModel
{
	std::vector<PreviewMesh> meshes;
	std::vector<PreviewTexture> textures;
	float bmin[3] = { 0, 0, 0 };
	float bmax[3] = { 0, 0, 0 };
	std::string info;
};

// Loads a legacy (gen8) or Enhanced (gen9) .ydr together with textures from the given .ytd files.
bool loadPreviewModel(const std::string& ydrPath, const std::vector<std::string>& ytdPaths, PreviewModel& out, std::string& error);

class cModelPreviewPanel : public wxPanel
{
public:
	cModelPreviewPanel(wxWindow* parent, const wxPoint& pos, const wxSize& size);

	void showModel(const std::string& ydrPath, const std::vector<std::string>& ytdPaths);
	void clearModel(const wxString& message);

private:
	void onPaint(wxPaintEvent& evt);
	void onSize(wxSizeEvent& evt);
	void onMouse(wxMouseEvent& evt);
	void render();

	PreviewModel model;
	bool hasModel = false;
	wxString message;
	wxBitmap bitmap;
	float yaw = 0.0f;
	float pitch = 0.2f;
	float zoom = 1.0f;
	float baseYaw = 0.0f;
	wxPoint lastMouse;
};
