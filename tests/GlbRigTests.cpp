#include <wx/wx.h>
#include <wx/filename.h>
#include <wx/file.h>
#include "cGlbRig.h"
#include "cGlbAssetBuilder.h"
#include "cRscWriter.h"
#include <cmath>
#include <functional>
#include <iostream>

namespace
{
	void require(bool ok,const char* text) { if (!ok) throw std::runtime_error(text); }
	void nearValue(float a,float b) { require(std::fabs(a-b)<0.00001f,"Rig transform mismatch."); }
	void fails(const std::function<void()>& action,const std::string& reason)
	{
		try { action(); } catch (const std::exception& error) {
			if (std::string(error.what()).find(reason)==std::string::npos) throw;
			return;
		}
		throw std::runtime_error("Expected rig failure: "+reason);
	}
}

void testGlbRig(const wxString& repo,const wxString& output)
{
	const auto path=[&](const char* generation) { return repo+"\\test\\"+generation+"\\w_ar_carbinerifle_m31.ydr"; };
	const auto legacy=loadReferenceSkeleton(path("gen8")), enhanced=loadReferenceSkeleton(path("gen9"));
	require(legacy.bones.size()==17 && enhanced.bones.size()==17,"Actual reference skeleton count.");
	for (size_t i=0; i<legacy.bones.size(); ++i)
	{
		const auto& a=legacy.bones[i]; const auto& b=enhanced.bones[i];
		require(a.name==b.name && a.parent==b.parent && a.tag==b.tag,"Cross-generation bone identities.");
		for (unsigned k=0;k<3;++k) nearValue(a.translation[k],b.translation[k]);
		for (unsigned k=0;k<4;++k) nearValue(a.rotation[k],b.rotation[k]);
	}
	require(legacy.bones[1].name=="Gun_GripR" && legacy.bones[1].tag==18308 && legacy.bones[2].parent==1,
		"Binary name/tag/parent offsets must match supplied XML.");
	nearValue(legacy.bones[1].translation[0],-.115186f);
	const auto world=skeletonWorldMatrices(legacy);
	for (const auto& m:world)
	{
		const std::array<float,3> local={.1f,.2f,-.3f};
		const auto restored=inverseBonePoint(m,bonePoint(m,local));
		for (unsigned k=0;k<3;++k) nearValue(restored[k],local[k]);
	}
	auto model=loadGlb(wxFileName(output,"static_import.glb").GetFullPath());
	auto project=createGlbImportProject(model,wxFileName(output,"static_import.glb").GetFullPath(),"WEAPON_CARBINERIFLE");
	assignGlbPartRole(model,project,0,GlbPartRole::Body); assignGlbPartRole(model,project,1,GlbPartRole::Magazine);
	project.alignment.confirmed=true;
	auto pivot=project.pivots[1]; pivot.confirmed=true; assignGlbPivot(model,project,1,pivot);
	setGlbReferenceSkeleton(model,project,legacy);
	const auto suggestions=suggestGlbBones(model,project);
	require(suggestions==std::vector<int>({2,14}),"Only verified semantic bone names are suggested.");
	require(project.boneBindings[0]==-1,"Suggestions must not bind silently.");
	assignGlbBone(model,project,0,2); assignGlbBone(model,project,1,14);
	require(glbRigErrors(model,project).empty(),"Complete rigid recipe.");
	const auto bound=buildGlbRigidBindings(model,project);
	const auto rest=makePreparedGlbPreview(model,project);
	for (size_t m=0; m<bound.size(); ++m)
		for (size_t v=0; v<bound[m].localPositions.size(); ++v)
		{
			const auto point=bonePoint(world[bound[m].bone],bound[m].localPositions[v]);
			for (unsigned k=0;k<3;++k) nearValue(point[k],rest.meshes[m].pos[v*3+k]);
		}
	const auto moved=makeGlbRigPreview(model,project,14,30,2);
	require(moved.meshes[0].pos==rest.meshes[0].pos && moved.meshes[1].pos!=rest.meshes[1].pos,"Selected bone moves only its bound parts.");
	const auto parent=makeGlbRigPreview(model,project,2,10,0);
	require(parent.meshes[0].pos!=rest.meshes[0].pos && parent.meshes[1].pos!=rest.meshes[1].pos,"Descendant bone motion must propagate.");
	require(makeGlbRigPreview(model,project).meshes[0].pos==rest.meshes[0].pos,"Rest pose must remain unchanged.");
	fitGlbBoneToPivot(model,project,1);
	const auto fitted=skeletonWorldMatrices(project.skeleton);
	const auto p=bonePoint(fitted[14],{0,0,0});
	for (unsigned k=0;k<3;++k) nearValue(p[k],project.pivots[1].position[k]);
	require(makeGlbRigPreview(model,project).meshes[1].pos==rest.meshes[1].pos,"Moving a bind origin must preserve authored geometry.");
	project.skeleton=legacy;
	auto mainPivot=project.pivots[0]; mainPivot.position={1,2,3}; mainPivot.confirmed=true;
	assignGlbPivot(model,project,0,mainPivot);
	fitGlbBoneToPivot(model,project,0);
	const auto descendants=skeletonWorldMatrices(project.skeleton);
	for (size_t b=0;b<world.size();++b)
		if (b!=2)
			for (unsigned k=0;k<3;++k) nearValue(float(descendants[b][12+k]),float(world[b][12+k]));
	project.skeleton=legacy;
	const auto saved=wxFileName(output,"reference_rig.vwtimport.xml").GetFullPath();
	saveGlbImportProject(saved,model,project);
	GlbModel reopened;
	const auto loaded=loadGlbImportProject(saved,reopened);
	require(loaded.skeleton.bones.size()==17 && loaded.boneBindings==project.boneBindings,"Embedded skeleton snapshot round trip.");
	const auto assets=buildGlbAssets(model,project,64);
	const auto partitioned=partitionGlbComponents(assets,project);
	require(partitioned.weapon.meshes.size()==1 && partitioned.components.size()==1,"Magazine must be a separate component, not duplicated in the base.");
	require(partitioned.components[0].role==GlbPartRole::Magazine && partitioned.components[0].attachBone=="WAPClip" &&
		partitioned.components[0].drawable.skeleton.bones[0].name==partitioned.components[0].localAttachBone,
		"Component must actually contain its declared local attachment anchor.");
	const auto& componentMesh=partitioned.components[0].drawable.meshes[0];
	for (size_t v=0;v<componentMesh.vertices.size();++v)
	{
		const auto global=bonePoint(world[14],componentMesh.vertices[v].position);
		for (unsigned k=0;k<3;++k) nearValue(global[k],assets.drawable.meshes[1].vertices[v].position[k]);
	}
	auto collapsed=model;
	collapsed.parts[0].hasTangents=false;
	for (auto& v:collapsed.parts[0].geometry.vertices) v.uv={0,0};
	const auto collapsedAssets=buildGlbAssets(collapsed,project,64);
	bool diagnosed=false;
	for (const auto& diagnostic:collapsedAssets.diagnostics)
		if (diagnostic.find("1 triangles have collapsed UVs")!=std::string::npos) diagnosed=true;
	require(diagnosed,"Collapsed UV export must explicitly diagnose the geometric basis policy.");
	for (const auto generation:{GameGeneration::Legacy,GameGeneration::Enhanced})
	{
		const auto prefix=generation==GameGeneration::Legacy ? "gen8":"gen9";
		const auto out=wxFileName(output,wxString(prefix)+"_rigged.ydr").GetFullPath();
		cRscWriter::save(out,buildYdr(assets.drawable,generation));
		const auto writtenSkeleton=loadReferenceSkeleton(out);
		require(writtenSkeleton.bones.size()==legacy.bones.size() && writtenSkeleton.signatures==legacy.signatures &&
			writtenSkeleton.childPairs==legacy.childPairs,"Written reference tables must survive independent native readback.");
		for (size_t i=0;i<legacy.bones.size();++i)
		{
			require(writtenSkeleton.bones[i].name==legacy.bones[i].name && writtenSkeleton.bones[i].parent==legacy.bones[i].parent,
				"Written rig identity round trip.");
			for (unsigned k=0;k<3;++k) nearValue(writtenSkeleton.bones[i].translation[k],legacy.bones[i].translation[k]);
		}
		PreviewModel reloaded; std::string error;
		require(loadPreviewModel(out.ToStdString(),{},reloaded,error),error.c_str());
		require(reloaded.meshes.size()==2,"Skinned geometry reader must load generated declaration.");
	}
	auto invalid=project; invalid.weaponTemplate="WEAPON_PISTOL";
	require(!glbRigErrors(model,invalid).empty(),"Changing template invalidates reference declaration.");
	invalid=project; invalid.boneBindings[0]=999;
	fails([&] { validateGlbRigData(model,invalid); },"out of range");
	auto bad=legacy; bad.bones[2].parent=2;
	fails([&] { validateReferenceSkeleton(bad); },"Parents");
	bad=legacy; bad.bones[0].rotation={0,0,0,0};
	fails([&] { validateReferenceSkeleton(bad); },"quaternion");
	bad=legacy; bad.bones[1].tag=bad.bones[0].tag;
	fails([&] { validateReferenceSkeleton(bad); },"unique");
	assignGlbPartRole(model,project,1,GlbPartRole::Excluded);
	require(project.boneBindings[1]==-1,"Role changes/exclusion must clear bone mapping.");
	fails([&] { assignGlbBone(model,project,1,14); },"included");
	std::cout<<"Binary reference skeleton / rigid binding / descendant motion tests passed.\n";
	const auto realPath=repo+"\\test\\m4a1.glb";
	if (wxFileExists(realPath))
	{
		GlbModel real;
		auto realProject=loadGlbImportProject(wxFileName(output,"m4a1_preparation.vwtimport.xml").GetFullPath(),real);
		setGlbReferenceSkeleton(real,realProject,legacy);
		const auto proposed=suggestGlbBones(real,realProject);
		for (size_t i=0;i<real.parts.size();++i)
		{
			const auto role=realProject.roles[i];
			const int bone=proposed[i]>=0 ? proposed[i] : role==GlbPartRole::Bolt ? 4 :
				role==GlbPartRole::ChargingHandle ? 3:2;
			assignGlbBone(real,realProject,i,bone);
			auto pivot=realProject.pivots[i]; pivot.confirmed=true;
			assignGlbPivot(real,realProject,i,pivot);
		}
		// Synthetic identity placement is for structural testing, not the user's authored calibration.
		realProject.alignment.confirmed=true;
		saveGlbImportProject(wxFileName(output,"m4a1_structural_test.vwtimport.xml").GetFullPath(),real,realProject);
		auto realAssets=buildGlbAssets(real,realProject,64);
		for (const auto generation:{GameGeneration::Legacy,GameGeneration::Enhanced})
		{
			const auto prefix=generation==GameGeneration::Legacy ? "gen8":"gen9";
			cRscWriter::save(wxFileName(output,wxString(prefix)+"_m4a1_converted.ydr").GetFullPath(),buildYdr(realAssets.drawable,generation));
			cRscWriter::save(wxFileName(output,wxString(prefix)+"_m4a1_converted.ytd").GetFullPath(),buildYtd(realAssets.images,generation));
		}
		std::cout<<"Real M4A1 opaque material / tangent / skinned YDR + YTD conversion passed (synthetic test placement only).\n";
	}
}
