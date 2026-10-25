#include <wx/wx.h>
#include <wx/filename.h>
#include <Windows.h>
#include "cGlbResourceExporter.h"
#include "cRscWriter.h"
#include <algorithm>

std::vector<std::string> exportGlbResources(const GlbModel& model,const GlbImportProject& project,
	const wxString& destination,const wxString& templatesRoot,const WeaponResourceSettings& settings,unsigned textureSize)
{
	if (settings.modelName!=project.modelName || settings.weaponTemplate!=project.weaponTemplate)
		throw std::invalid_argument("Resource settings must match the prepared model/template.");
	if (!settings.components.empty()) throw std::invalid_argument("Component metadata is derived from project roles; do not provide a second manual component list.");
	for (auto role:project.roles)
		if (role==GlbPartRole::Suppressor || role==GlbPartRole::Flashlight)
			throw std::invalid_argument("Suppressor/flashlight export requires a compatible component-specific muzzle/bulb rig; the weapon reference alone does not provide those helper bones. Do not relabel these parts to bypass this check.");
	wxFileName target(destination,"");
	if (!target.MakeAbsolute()) throw std::runtime_error("Could not resolve output directory.");
	const auto finalPath=target.GetPath();
	if (wxDirExists(finalPath) || wxFileExists(finalPath)) throw std::runtime_error("Output destination already exists; choose a new directory.");
	wxFileName parent(finalPath); // The final component is the new directory name.
	if (!wxDirExists(parent.GetPath())) throw std::runtime_error("Output parent directory does not exist.");
	auto assets=buildGlbAssets(model,project,textureSize);
	const auto partitioned=partitionGlbComponents(assets,project);
	auto resourceSettings=settings;
	for (const auto& source:partitioned.components)
	{
		WeaponResourceComponent component;
		component.modelName=source.drawable.name; component.attachBone=source.attachBone;
		component.localAttachBone=source.localAttachBone;
		auto suffix=std::string(glbPartRoleInfo(source.role).id);
		for (char& c:suffix) if (c>='a' && c<='z') c=char(c-'a'+'A');
		auto id=project.modelName;
		for (char& c:id) if (c>='a' && c<='z') c=char(c-'a'+'A');
		component.componentId="COMPONENT_"+id+"_"+suffix;
		switch (source.role)
		{
		case GlbPartRole::Magazine: component.kind=WeaponResourceComponentKind::Clip; break;
		case GlbPartRole::Scope: component.kind=WeaponResourceComponentKind::Scope; break;
		case GlbPartRole::Suppressor: component.kind=WeaponResourceComponentKind::Suppressor; break;
		case GlbPartRole::Flashlight: component.kind=WeaponResourceComponentKind::Flashlight; break;
		case GlbPartRole::Grip: component.kind=WeaponResourceComponentKind::Grip; break;
		default: throw std::runtime_error("Invalid component partition role.");
		}
		resourceSettings.components.push_back(std::move(component));
	}
	const auto metadata=buildWeaponResourceFiles(resourceSettings,templatesRoot);
	std::string runtimeCheck="local weapon = '"+settings.weaponId+"'\nlocal components = {\n";
	for (const auto& component:resourceSettings.components)
		runtimeCheck+="    { id = '"+component.componentId+"', model = '"+component.modelName+"' },\n";
	runtimeCheck+="}\n"
		"AddEventHandler('onClientResourceStart', function(resource)\n"
		"    if resource ~= GetCurrentResourceName() then return end\n"
		"    CreateThread(function()\n"
		"        Wait(1000)\n"
		"        local hash = GetHashKey(weapon)\n"
		"        local result = { resource = resource, weapon = weapon, weapon_valid = IsWeaponValid(hash), component_count = #components, components = {} }\n"
		"        RequestWeaponAsset(hash, 31, 0)\n"
		"        local deadline = GetGameTimer() + 15000\n"
		"        while not HasWeaponAssetLoaded(hash) and GetGameTimer() < deadline do Wait(50) end\n"
		"        result.asset_loaded = HasWeaponAssetLoaded(hash)\n"
		"        for _, component in ipairs(components) do\n"
		"            local id = GetHashKey(component.id)\n"
		"            local model = GetHashKey(component.model)\n"
		"            RequestModel(model)\n"
		"            local modelDeadline = GetGameTimer() + 10000\n"
		"            while not HasModelLoaded(model) and GetGameTimer() < modelDeadline do Wait(50) end\n"
		"            result.components[#result.components + 1] = {\n"
		"                id = component.id,\n"
		"                compatible = DoesWeaponTakeWeaponComponent(hash, id),\n"
		"                model_matches = GetWeaponComponentTypeModel(id) == model,\n"
		"                model_loaded = HasModelLoaded(model)\n"
		"            }\n"
		"            SetModelAsNoLongerNeeded(model)\n"
		"        end\n"
		"        RemoveWeaponAsset(hash)\n"
		"        print('[vwt-validation] ' .. json.encode(result))\n"
		"    end)\n"
		"end)\n";
	// All expensive binary construction completes before publishing anything.
	const auto ydr8=buildYdr(partitioned.weapon,GameGeneration::Legacy), ydr9=buildYdr(partitioned.weapon,GameGeneration::Enhanced);
	const auto ytd8=buildYtd(assets.images,GameGeneration::Legacy), ytd9=buildYtd(assets.images,GameGeneration::Enhanced);
	std::vector<std::vector<uint8_t>> component8,component9;
	for (const auto& component:partitioned.components)
	{
		component8.push_back(buildYdr(component.drawable,GameGeneration::Legacy));
		component9.push_back(buildYdr(component.drawable,GameGeneration::Enhanced));
	}
	const auto stage=wxFileName::CreateTempFileName(finalPath+".stage-");
	if (stage.empty()) throw std::runtime_error("Could not allocate staging path.");
	if (!wxRemoveFile(stage) || !wxMkdir(stage)) throw std::runtime_error("Could not create staging directory.");
	std::vector<wxString> files,dirs={stage};
	auto mkdir=[&](const wxString& path) {
		if (!wxMkdir(path)) throw std::runtime_error("Could not create output subdirectory: "+path.ToStdString());
		dirs.push_back(path);
	};
	auto save=[&](const wxString& path,const std::vector<uint8_t>& data) {
		cRscWriter::save(path,data); files.push_back(path);
	};
	try
	{
		for (unsigned enhanced=0;enhanced<2;++enhanced)
		{
			const auto resource=stage+"\\"+wxString::FromUTF8(settings.resourceName)+(enhanced ? "_enhanced":"");
			mkdir(resource); mkdir(resource+"\\meta"); mkdir(resource+"\\stream");
			for (const auto& file:metadata)
			{
				if (file.relativePath.empty() || file.relativePath.find("..")!=std::string::npos ||
					file.relativePath.find(':')!=std::string::npos || file.relativePath.front()=='/' || file.relativePath.front()=='\\')
					throw std::runtime_error("Invalid metadata output path.");
				auto relative=wxString::FromUTF8(file.relativePath); relative.Replace("/","\\");
				if (file.relativePath=="fxmanifest.lua")
				{
					auto bytes=file.bytes;
					const std::string declaration="\nclient_script 'vwt_validate.lua'\n";
					bytes.insert(bytes.end(),declaration.begin(),declaration.end());
					save(resource+"\\"+relative,bytes);
				}
				else save(resource+"\\"+relative,file.bytes);
			}
			save(resource+"\\vwt_validate.lua",std::vector<uint8_t>(runtimeCheck.begin(),runtimeCheck.end()));
			const auto base=resource+"\\stream\\"+wxString::FromUTF8(project.modelName);
			save(base+".ydr",enhanced ? ydr9:ydr8); save(base+".ytd",enhanced ? ytd9:ytd8);
			for (size_t i=0;i<partitioned.components.size();++i)
			{
				const auto component=resource+"\\stream\\"+wxString::FromUTF8(partitioned.components[i].drawable.name);
				save(component+".ydr",enhanced ? component9[i]:component8[i]);
				save(component+".ytd",enhanced ? ytd9:ytd8);
			}
		}
		std::string report="Conversion completed structurally, NOT validated in-game.\nSource SHA256: "+project.sourceSha256+
			"\nWeapon: "+settings.weaponId+"\nUse only one generated resource on a server, matching its game generation.\n";
		for (const auto& diagnostic:assets.diagnostics) report+="- "+diagnostic+"\n";
		for (const auto& component:resourceSettings.components)
			report+="- Component "+component.componentId+": "+component.modelName+" -> "+component.attachBone+" (default enabled)\n";
		report+="Enable with ensure "+settings.resourceName+" (Legacy) or ensure "+settings.resourceName+"_enhanced (Enhanced).\n";
		report+="On client resource start, vwt_validate.lua logs [vwt-validation] JSON after checking asset loading and component registrations. It never equips or spawns weapons. This does not test animations or appearance.\n";
		save(stage+"\\conversion-report.txt",std::vector<uint8_t>(report.begin(),report.end()));
		if (!::MoveFileExW(stage.wc_str(),finalPath.wc_str(),MOVEFILE_WRITE_THROUGH))
			throw std::runtime_error("Could not atomically publish the output directory (Windows error "+std::to_string(GetLastError())+").");
	}
	catch (...)
	{
		bool cleaned=true;
		for (auto i=files.rbegin();i!=files.rend();++i) if (wxFileExists(*i) && !wxRemoveFile(*i)) cleaned=false;
		for (auto i=dirs.rbegin();i!=dirs.rend();++i) if (wxDirExists(*i) && !wxRmdir(*i)) cleaned=false;
		if (!cleaned) wxLogError("Failed to clean owned conversion staging directory: %s",stage);
		throw;
	}
	return assets.diagnostics;
}
