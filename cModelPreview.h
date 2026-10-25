#pragma once
#include <wx/wx.h>
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <functional>
#include <wx/timer.h>

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
	std::vector<float> colors;
	std::vector<uint32_t> idx;
	int texture = -1;
};

struct PreviewMarker
{
	std::array<float, 3> position;
	std::array<float, 3> axis;
	std::string label;
	bool axes = false;
};

struct PreviewModel
{
	std::vector<PreviewMesh> meshes;
	std::vector<PreviewTexture> textures;
	float bmin[3] = { 0, 0, 0 };
	float bmax[3] = { 0, 0, 0 };
	std::string info;
	std::vector<PreviewMarker> markers;
};

struct PreviewRenderStats
{
	uint64_t frames = 0;
	int width = 0;
	int height = 0;
};

bool pickPreviewSurface(const PreviewModel& model, int width, int height, float yaw, float pitch,
	float zoom, const wxPoint& pixel, std::array<float, 3>& point);
wxImage renderPreviewImage(const PreviewModel& model, int width, int height,
	float yaw, float pitch, float zoom);
// Loads a legacy (gen8) or Enhanced (gen9) .ydr together with textures from the given .ytd files.
bool loadPreviewModel(const std::string& ydrPath, const std::vector<std::string>& ytdPaths, PreviewModel& out, std::string& error);

class cModelPreviewPanel : public wxPanel
{
public:
	cModelPreviewPanel(wxWindow* parent, const wxPoint& pos, const wxSize& size);

	void showModel(const std::string& ydrPath, const std::vector<std::string>& ytdPaths);
	void showModel(PreviewModel loaded, bool preserveView = false);
	void setPointPickedHandler(std::function<void(const std::array<float, 3>&)> handler);
	void clearModel(const wxString& message);
	PreviewRenderStats getRenderStats() const { return renderStats; }

private:
	void onPaint(wxPaintEvent& evt);
	void onSize(wxSizeEvent& evt);
	void onMouse(wxMouseEvent& evt);
	void onPick(wxMouseEvent& evt);
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
	std::function<void(const std::array<float, 3>&)> pointPicked;
	wxTimer frameTimer;
	wxTimer settleTimer;
	bool framePending = false;
	bool interactive = false;
	PreviewRenderStats renderStats;
	void requestFrame();
};
