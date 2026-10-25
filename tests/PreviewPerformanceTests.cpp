#include <wx/wx.h>
#include "cGlbImporter.h"
#include <chrono>
#include <iostream>
#include <algorithm>
#include <cmath>

void testPreviewPerformance(const wxString& repo,const wxString& output)
{
	PreviewModel colorModel;
	PreviewTexture colorTexture;
	colorTexture.width=1; colorTexture.height=1; colorTexture.rgba={32,128,250,255};
	colorModel.textures.push_back(colorTexture);
	PreviewMesh triangle;
	triangle.pos={-1,0,-1,1,0,-1,0,0,1}; triangle.idx={0,1,2};
	triangle.nrm={0,0,1,0,0,1,0,0,1}; triangle.uv={0,0,0,0,0,0}; triangle.texture=0;
	colorModel.bmin[0]=-1; colorModel.bmin[2]=-1; colorModel.bmax[0]=1; colorModel.bmax[2]=1;
	for (float factor:{.01f,.25f,.5f,1.f})
	{
		triangle.colors.assign(9,factor); colorModel.meshes={triangle};
		const auto image=renderPreviewImage(colorModel,200,200,0,0,1);
		for (unsigned k=0;k<3;++k)
		{
			const float srgb=colorTexture.rgba[k]/255.f;
			const float linear=(srgb<=.04045f ? srgb/12.92f:std::pow((srgb+.055f)/1.055f,2.4f))*factor;
			const float encoded=255*(linear<=.0031308f ? 12.92f*linear:1.055f*std::pow(linear,1/2.4f)-.055f);
			const int expected=int(encoded*(.35f+.65f*.72f));
			const int actual=image.GetData()[(100*200+100)*3+k];
			if (std::abs(actual-expected)>1) throw std::runtime_error("Preview color LUT changed linear-light output by more than one byte.");
		}
	}
	const auto source=repo+"\\test\\m4a1.glb";
	if (!wxFileExists(source)) { std::cout<<"Optional real-model preview benchmark skipped.\n"; return; }
	const auto begin=std::chrono::steady_clock::now();
	const auto imported=loadGlb(source);
	const auto view=makeGlbPreview(imported);
	for (const auto& texture:view.textures)
		if (texture.width>512 || texture.height>512) throw std::runtime_error("Preview must not copy full-size export textures.");
	std::vector<double> times;
	for (unsigned i=0;i<7;++i)
	{
		const auto start=std::chrono::steady_clock::now();
		auto image=renderPreviewImage(view,640,400,.35f+float(i)*.07f,.2f,2);
		if (!image.IsOk()) throw std::runtime_error("Preview render failed.");
		const auto end=std::chrono::steady_clock::now();
		if (i) times.push_back(std::chrono::duration<double,std::milli>(end-start).count());
		if (i==0 && !image.SaveFile(output+"\\preview_quality.png",wxBITMAP_TYPE_PNG))
			throw std::runtime_error("Could not save benchmark preview.");
	}
	std::sort(times.begin(),times.end());
	std::cout<<"M4A1 preview benchmark: median="<<times[times.size()/2]<<" ms at 640x400 zoom2; "
		<<"import+setup+7frames="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()<<" ms\n";
	std::vector<double> interactive;
	for (unsigned i=0;i<7;++i)
	{
		const auto start=std::chrono::steady_clock::now();
		renderPreviewImage(view,360,225,.35f+float(i)*.07f,.2f,2);
		if (i) interactive.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
	}
	std::sort(interactive.begin(),interactive.end());
	std::cout<<"M4A1 interactive preview: median="<<interactive[interactive.size()/2]<<" ms at 360x225\n";
	if (interactive[interactive.size()/2]>50) throw std::runtime_error("Interactive preview exceeds the 50ms rendering budget.");
	wxDialog dialog(nullptr,wxID_ANY,"Preview interaction test",wxDefaultPosition,wxSize(660,450));
	auto* panel=new cModelPreviewPanel(&dialog,{0,0},{640,400});
	wxTimer checkMoving(&dialog),checkSettled(&dialog);
	PreviewRenderStats before,moving;
	std::exception_ptr failure;
	dialog.Bind(wxEVT_TIMER,[&](wxTimerEvent&) {
		try
		{
			moving=panel->getRenderStats();
			if (moving.frames!=before.frames+1 || moving.width>360)
				throw std::runtime_error("Interactive frame must be bounded and coalesced.");
		}
		catch (...) { failure=std::current_exception(); dialog.EndModal(wxID_CANCEL); }
	},checkMoving.GetId());
	dialog.Bind(wxEVT_TIMER,[&](wxTimerEvent&) {
		try
		{
			const auto settled=panel->getRenderStats();
			if (settled.frames!=moving.frames+1 || settled.width!=panel->GetClientSize().x)
				throw std::runtime_error("Idle preview must return to full viewport resolution.");
		}
		catch (...) { failure=std::current_exception(); }
		dialog.EndModal(wxID_OK);
	},checkSettled.GetId());
	wxTheApp->CallAfter([&] {
		panel->showModel(view); before=panel->getRenderStats();
		for (unsigned i=0;i<40;++i)
		{
			wxMouseEvent wheel(wxEVT_MOUSEWHEEL);
			wheel.m_wheelRotation=i%2 ? -120:120;
			panel->GetEventHandler()->ProcessEvent(wheel);
		}
		if (panel->getRenderStats().frames!=before.frames)
		{
			failure=std::make_exception_ptr(std::runtime_error("Mouse bursts must be coalesced, not rendered synchronously."));
			dialog.EndModal(wxID_CANCEL); return;
		}
		checkMoving.StartOnce(60); checkSettled.StartOnce(200);
	});
	dialog.ShowModal();
	checkMoving.Stop();checkSettled.Stop();
	if (failure) std::rethrow_exception(failure);
	std::cout<<"Mouse burst: 40 wheel events -> 1 interactive frame + 1 full-quality settled frame.\n";
}
