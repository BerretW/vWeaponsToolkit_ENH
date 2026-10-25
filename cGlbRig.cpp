#include "cGlbRig.h"
#include <cmath>
#include <map>
#include <set>
#include <limits>

namespace
{
	[[noreturn]] void fail(const char* text) { throw std::invalid_argument(std::string("GLB rig: ")+text); }
	std::array<float,3> turn(std::array<float,3> p, unsigned axis, float degrees)
	{
		const auto a=(axis+1)%3, b=(axis+2)%3;
		const double angle=degrees*std::acos(-1.0)/180, c=std::cos(angle), s=std::sin(angle);
		const float x=p[a], y=p[b];
		p[a]=float(c*x-s*y); p[b]=float(s*x+c*y);
		return p;
	}
}

void validateGlbRigData(const GlbModel& model, const GlbImportProject& project)
{
	if (project.boneBindings.size()!=model.parts.size()) fail("Bone mapping count differs from source.");
	if (project.skeleton.bones.empty())
	{
		if (!project.skeletonTemplate.empty()) fail("Skeleton template without a skeleton.");
		for (int b : project.boneBindings) if (b!=-1) fail("Bone mapping without a reference skeleton.");
		return;
	}
	validateReferenceSkeleton(project.skeleton);
	if (project.skeletonTemplate.empty()) fail("Reference skeleton template is missing.");
	std::map<size_t,int> objects;
	for (size_t i=0; i<project.boneBindings.size(); ++i)
	{
		const auto bone=project.boneBindings[i];
		if (bone < -1 || bone >= int(project.skeleton.bones.size())) fail("Bone binding is out of range.");
		if (project.roles.at(i)==GlbPartRole::Excluded && bone!=-1) fail("Excluded objects must not have a bone binding.");
		const auto node=model.parts[i].nodeIndex;
		const auto found=objects.find(node);
		if (found!=objects.end() && found->second!=bone) fail("Material primitives of one object must share a bone.");
		objects[node]=bone;
	}
}

void setGlbReferenceSkeleton(const GlbModel& model, GlbImportProject& project, const ReferenceSkeleton& skeleton)
{
	validateReferenceSkeleton(skeleton);
	auto candidate=project;
	candidate.skeleton=skeleton;
	candidate.skeletonTemplate=project.weaponTemplate;
	candidate.boneBindings.assign(model.parts.size(),-1);
	validateGlbRigData(model,candidate);
	project=std::move(candidate);
}

void assignGlbBone(const GlbModel& model, GlbImportProject& project, size_t part, int bone)
{
	validateGlbRigData(model,project);
	if (part>=model.parts.size() || project.roles[part]==GlbPartRole::Excluded) fail("Select an included source object.");
	if (bone < -1 || bone>=int(project.skeleton.bones.size())) fail("Select a bone from the reference skeleton.");
	auto candidate=project;
	for (size_t i=0; i<model.parts.size(); ++i)
		if (model.parts[i].nodeIndex==model.parts[part].nodeIndex) candidate.boneBindings[i]=bone;
	validateGlbRigData(model,candidate);
	project.boneBindings=std::move(candidate.boneBindings);
}

void fitGlbBoneToPivot(const GlbModel& model, GlbImportProject& project, size_t part)
{
	validateGlbRigData(model,project);
	if (part>=model.parts.size() || project.boneBindings[part]<0 || !project.pivots[part].confirmed)
		fail("Select an object with a confirmed pivot and assigned bone.");
	const auto index=size_t(project.boneBindings[part]);
	moveGlbBoneOrigin(model,project,index,glbAlignedPoint(project.alignment,project.pivots[part].position));
}

void moveGlbBoneOrigin(const GlbModel& model,GlbImportProject& project,size_t index,const std::array<float,3>& pivot)
{
	validateGlbRigData(model,project);
	if (index>=project.skeleton.bones.size()) fail("Reference bone index is out of range.");
	for (float value:pivot) if (!std::isfinite(value) || std::fabs(value)>1000) fail("Invalid reference bone position.");
	if (project.skeleton.bones[index].parent<0) fail("Do not move the skeleton root; use model alignment.");
	const auto world=skeletonWorldMatrices(project.skeleton);
	auto candidate=project.skeleton;
	const auto parent=size_t(candidate.bones[index].parent);
	candidate.bones[index].translation=inverseBonePoint(world[parent],pivot);
	const auto moved=skeletonWorldMatrices(candidate);
	for (size_t i=0; i<candidate.bones.size(); ++i)
		if (candidate.bones[i].parent==int(index))
			candidate.bones[i].translation=inverseBonePoint(moved[index],bonePoint(world[i],{0,0,0}));
	validateReferenceSkeleton(candidate);
	project.skeleton=std::move(candidate);
}

std::vector<int> suggestGlbBones(const GlbModel& model, const GlbImportProject& project)
{
	validateGlbRigData(model,project);
	std::vector<int> result(model.parts.size(),-1);
	for (size_t i=0; i<result.size(); ++i)
	{
		const char* name=nullptr;
		switch (project.roles[i])
		{
		case GlbPartRole::Body: name="Gun_Main_Bone"; break;
		case GlbPartRole::Magazine: name="WAPClip"; break;
		case GlbPartRole::Trigger: name="Gun_Trigger"; break;
		case GlbPartRole::Scope: name="WAPScop"; break;
		case GlbPartRole::Suppressor: name="WAPSupp"; break;
		case GlbPartRole::Flashlight: name="WAPFlshLasr"; break;
		case GlbPartRole::Grip: name="WAPGrip"; break;
		default: break;
		}
		if (!name) continue;
		for (size_t b=0; b<project.skeleton.bones.size(); ++b)
			if (project.skeleton.bones[b].name==name) result[i]=int(b);
	}
	return result;
}

std::vector<std::string> glbRigErrors(const GlbModel& model, const GlbImportProject& project)
{
	auto errors=glbPlacementErrors(model,project);
	validateGlbRigData(model,project);
	if (project.skeleton.bones.empty())
	{
		errors.push_back("Load a reference weapon YDR with a skeleton.");
		return errors;
	}
	if (project.skeletonTemplate!=project.weaponTemplate)
		errors.push_back("Weapon template changed: load/confirm a reference skeleton for the new template.");
	std::set<size_t> checked;
	for (size_t i=0; i<model.parts.size(); ++i)
		if (project.roles[i]!=GlbPartRole::Excluded && project.boneBindings[i]<0 && checked.insert(model.parts[i].nodeIndex).second)
			errors.push_back("Assign a real reference bone: "+model.parts[i].name);
	return errors;
}

std::vector<GlbRigidMesh> buildGlbRigidBindings(const GlbModel& model, const GlbImportProject& project)
{
	const auto errors=glbRigErrors(model,project);
	if (!errors.empty()) throw std::runtime_error(errors.front());
	const auto world=skeletonWorldMatrices(project.skeleton);
	const auto parts=includedGlbParts(model,project);
	const auto aligned=makePreparedGlbPreview(model,project);
	std::vector<GlbRigidMesh> result;
	for (size_t i=0; i<parts.size(); ++i)
	{
		GlbRigidMesh mesh;
		mesh.sourcePart=parts[i]; mesh.bone=uint16_t(project.boneBindings[parts[i]]);
		const auto& vertices=aligned.meshes[i].pos;
		for (size_t v=0; v<vertices.size()/3; ++v)
			mesh.localPositions.push_back(inverseBonePoint(world[mesh.bone],{vertices[v*3],vertices[v*3+1],vertices[v*3+2]}));
		result.push_back(std::move(mesh));
	}
	return result;
}

PreviewModel makeGlbRigPreview(const GlbModel& model, const GlbImportProject& project, int selectedBone,
	float degrees, unsigned axis, float slideMetres)
{
	validateGlbRigData(model,project);
	if (selectedBone < -1 || selectedBone>=int(project.skeleton.bones.size()) || axis>2 ||
		!std::isfinite(degrees) || std::fabs(degrees)>180 || !std::isfinite(slideMetres) || std::fabs(slideMetres)>1)
		fail("Invalid bone-motion preview.");
	if ((degrees!=0 || slideMetres!=0) && selectedBone<0) fail("Select a bone before testing motion.");
	auto view=makePreparedGlbPreview(model,project);
	const auto world=skeletonWorldMatrices(project.skeleton);
	const auto parts=includedGlbParts(model,project);
	auto descendant=[&](int bone) {
		for (int b=bone; b>=0; b=project.skeleton.bones[size_t(b)].parent) if (b==selectedBone) return true;
		return false;
	};
	auto posed=[&](const std::array<float,3>& p) {
		auto local=inverseBonePoint(world[size_t(selectedBone)],p);
		local=turn(local,axis,degrees); local[axis]+=slideMetres;
		return bonePoint(world[size_t(selectedBone)],local);
	};
	if (selectedBone>=0 && (degrees!=0 || slideMetres!=0))
	{
		for (unsigned k=0; k<3; ++k) { view.bmin[k]=std::numeric_limits<float>::max(); view.bmax[k]=-view.bmin[k]; }
		for (size_t i=0; i<parts.size(); ++i)
		{
			auto& mesh=view.meshes[i];
			const bool moving=descendant(project.boneBindings[parts[i]]);
			for (size_t v=0; v<mesh.pos.size()/3; ++v)
			{
				std::array<float,3> p={mesh.pos[v*3],mesh.pos[v*3+1],mesh.pos[v*3+2]};
				if (moving)
				{
					p=posed(p);
					auto tip=inverseBoneDirection(world[size_t(selectedBone)],{mesh.nrm[v*3],mesh.nrm[v*3+1],mesh.nrm[v*3+2]});
					tip=turn(tip,axis,degrees);
					auto end=bonePoint(world[size_t(selectedBone)],tip);
					for (unsigned k=0; k<3; ++k) mesh.nrm[v*3+k]=float(end[k]-world[size_t(selectedBone)][12+k]);
				}
				for (unsigned k=0; k<3; ++k)
				{
					mesh.pos[v*3+k]=p[k]; view.bmin[k]=std::min(view.bmin[k],p[k]); view.bmax[k]=std::max(view.bmax[k],p[k]);
				}
			}
		}
	}
	for (size_t b=0; b<world.size(); ++b)
	{
		auto p=bonePoint(world[b],{0,0,0});
		if (selectedBone>=0 && descendant(int(b))) p=posed(p);
		const bool selected=int(b)==selectedBone;
		const std::array<float,3> direction={float(world[b][axis*4]),float(world[b][axis*4+1]),float(world[b][axis*4+2])};
		view.markers.push_back({p,direction,selected ? project.skeleton.bones[b].name : "",false});
	}
	view.info+=" | reference skeleton / rigid binding preview";
	return view;
}
