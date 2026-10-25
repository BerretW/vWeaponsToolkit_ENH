#include <wx/wx.h>
#include "cGlbRig.h"
#include "cGlbResourceExporter.h"
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <iostream>
#include <sstream>
#include <locale>
#include <cmath>
#include <stdexcept>
#include <iomanip>
#include <limits>

namespace
{
	std::string text(const wxString& value) { return value.ToStdString(wxConvUTF8); }
	void quoted(const std::string& value)
	{
		std::cout << '"';
		const char* hex="0123456789abcdef";
		for (unsigned char c:value)
		{
			if (c=='"' || c=='\\') std::cout << '\\' << char(c);
			else if (c<32) std::cout << "\\u00" << hex[c>>4] << hex[c&15];
			else std::cout << char(c);
		}
		std::cout << '"';
	}
	float scalar(const wxString& value)
	{
		std::istringstream input(text(value));
		input.imbue(std::locale::classic());
		float out;
		if (!(input>>out) || !std::isfinite(out)) throw std::invalid_argument("Expected finite numeric argument: "+text(value));
		input>>std::ws;
		if (!input.eof()) throw std::invalid_argument("Unexpected numeric suffix: "+text(value));
		return out;
	}
	size_t index(const wxString& value)
	{
		const auto s=text(value);
		if (s.empty() || s.size()>6 || s.find_first_not_of("0123456789")!=std::string::npos)
			throw std::invalid_argument("Expected nonnegative index: "+s);
		size_t out=0; for (char c:s) out=out*10+size_t(c-'0');
		return out;
	}
	size_t sourceObject(const GlbModel& model,size_t node)
	{
		for (size_t i=0;i<model.parts.size();++i) if (model.parts[i].nodeIndex==node) return i;
		throw std::invalid_argument("No mesh instance with source node index "+std::to_string(node));
	}
	std::array<float,3> vector(wchar_t** argv,int at)
	{
		return {scalar(argv[at]),scalar(argv[at+1]),scalar(argv[at+2])};
	}
	template<size_t N> void array(const std::array<float,N>& values)
	{
		std::cout<<'[';
		for (size_t i=0;i<N;++i) { if (i) std::cout<<','; std::cout<<values[i]; }
		std::cout<<']';
	}
	void summary(const GlbModel& model,const GlbImportProject* project=nullptr)
	{
		std::cout << "{\"source_sha256\":"; quoted(model.sourceSha256);
		if (project)
		{
			std::cout << ",\"model\":"; quoted(project->modelName);
			std::cout << ",\"template\":"; quoted(project->weaponTemplate);
			std::cout<<",\"alignment\":{\"scale\":"<<project->alignment.scale<<",\"confirmed\":"<<(project->alignment.confirmed ? "true":"false")
				<<",\"rotation_degrees\":"; array(project->alignment.rotationDegrees);
			std::cout<<",\"source_origin\":"; array(project->alignment.origin);
			std::cout<<",\"output_offset\":"; array(project->alignment.offset); std::cout<<'}';
		}
		std::cout << ",\"parts\":[";
		for (size_t i=0;i<model.parts.size();++i)
		{
			const auto& part=model.parts[i];
			if (i) std::cout << ',';
			std::cout << "{\"part\":" << i << ",\"node\":" << part.nodeIndex << ",\"name\":"; quoted(part.name);
			std::cout << ",\"vertices\":" << part.geometry.vertices.size() << ",\"triangles\":" << part.geometry.indices.size()/3;
			if (project)
			{
				std::cout << ",\"role\":"; quoted(glbPartRoleInfo(project->roles[i]).id);
				std::cout << ",\"pivot_confirmed\":" << (project->pivots[i].confirmed ? "true":"false")
					<< ",\"bone\":" << project->boneBindings[i];
				std::cout<<",\"pivot\":"; array(project->pivots[i].position);
				std::cout<<",\"axis\":"; array(project->pivots[i].axis);
			}
			std::cout << '}';
		}
		std::cout << "],\"bones\":[";
		const auto world=project && !project->skeleton.bones.empty() ? skeletonWorldMatrices(project->skeleton):std::vector<BoneMatrix>{};
		if (project)
			for (size_t i=0;i<project->skeleton.bones.size();++i)
			{
				const auto& bone=project->skeleton.bones[i]; if (i) std::cout << ',';
				std::cout << "{\"index\":" << i << ",\"name\":"; quoted(bone.name);
				std::cout << ",\"parent\":" << bone.parent << ",\"tag\":" << bone.tag << ",\"translation\":";
				array(bone.translation); std::cout<<",\"rotation\":"; array(bone.rotation);
				std::cout<<",\"world_origin\":"; array(bonePoint(world[i],{0,0,0})); std::cout<<'}';
			}
		std::cout << "],\"warnings\":[";
		for (size_t i=0;i<model.warnings.size();++i) { if (i) std::cout << ','; quoted(model.warnings[i]); }
		std::cout << "]}\n";
	}
	void usage()
	{
		std::cout <<
			"vwt-convert (all commands noninteractive; JSON stdout; diagnostics stderr)\n"
			"  inspect-glb source.glb\n"
			"  inspect-reference reference.ydr\n"
			"  create source.glb output.vwtimport.xml WEAPON_TEMPLATE [model_name]\n"
			"  inspect-project project.vwtimport.xml\n"
			"  roles project --suggest | NODE role-id\n"
			"  align project SCALE RX RY RZ ORIGIN_X ORIGIN_Y ORIGIN_Z OFFSET_X OFFSET_Y OFFSET_Z --confirm\n"
			"  pivot project NODE PX PY PZ AX AY AZ --confirm\n"
			"  reference project reference.ydr\n"
			"  bind project --suggest | NODE BONE_INDEX\n"
			"  fit-bone project NODE\n"
			"  bone-origin project BONE_INDEX OUTPUT_X OUTPUT_Y OUTPUT_Z\n"
			"  validate project assignments|placement|rig\n"
			"  export project NEW_OUTPUT_DIRECTORY resource_name WEAPON_ID [texture_size=1024]\n"
			"Nodes are source-node indices from inspect, not row numbers. --suggest never overwrites manual choices.\n"
			"Exit 0=success, 1=error, 2=incomplete validation. No game export is claimed by preparation commands.\n";
	}
}

int wmain(int argc,wchar_t** argv)
{
	try
	{
		wxInitializer wx;
		if (!wx.IsOk()) throw std::runtime_error("wxWidgets initialization failed.");
		wxInitAllImageHandlers();
		std::cout<<std::setprecision(std::numeric_limits<float>::max_digits10);
		if (argc==1 || (argc==2 && wxString(argv[1])=="--help")) { usage(); return 0; }
		if (argc<3) throw std::invalid_argument("Missing command arguments. Use --help.");
		const wxString command(argv[1]), path(argv[2]);
		auto expect=[&](int count) { if (argc!=count) throw std::invalid_argument("Unexpected command arguments; use --help."); };
		if (command=="inspect-glb") { expect(3); summary(loadGlb(path)); return 0; }
		if (command=="inspect-reference")
		{
			expect(3);
			GlbImportProject reference; reference.skeleton=loadReferenceSkeleton(path);
			summary({},&reference); return 0;
		}
		if (command=="create")
		{
			if (argc!=5 && argc!=6) throw std::invalid_argument("create requires source, output, template and optional model name.");
			const auto model=loadGlb(path);
			auto project=createGlbImportProject(model,path,text(argv[4]));
			if (argc==6) project.modelName=normalizeAssetName(text(argv[5]),"Model");
			saveGlbImportProject(argv[3],model,project); summary(model,&project); return 0;
		}
		GlbModel model;
		auto project=loadGlbImportProject(path,model);
		if (command=="inspect-project") { expect(3); summary(model,&project); return 0; }
		if (command=="export")
		{
			if (argc!=6 && argc!=7) throw std::invalid_argument("export requires project, NEW output folder, resource name, WEAPON_ID and optional texture size.");
			WeaponResourceSettings settings;
			settings.weaponTemplate=project.weaponTemplate; settings.modelName=project.modelName;
			settings.resourceName=normalizeAssetName(text(argv[4]),"Resource"); settings.weaponId=text(argv[5]);
			settings.displayName=project.modelName;
			const auto templates=wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath()+"\\templates";
			const auto diagnostics=exportGlbResources(model,project,argv[3],templates,settings,argc==7 ? unsigned(index(argv[6])):1024);
			std::cout<<"{\"published\":"; quoted(text(argv[3])); std::cout<<",\"in_game_validated\":false,\"diagnostics\":[";
			for (size_t i=0;i<diagnostics.size();++i) { if (i) std::cout<<','; quoted(diagnostics[i]); }
			std::cout<<"]}\n"; return 0;
		}
		if (command=="validate")
		{
			expect(4);
			const wxString stage(argv[3]);
			const auto errors=stage=="assignments" ? glbAssignmentErrors(model,project) :
				stage=="placement" ? glbPlacementErrors(model,project) :
				stage=="rig" ? glbRigErrors(model,project) : throw std::invalid_argument("Unknown validation stage.");
			std::cout << "{\"valid\":" << (errors.empty() ? "true":"false") << ",\"errors\":[";
			for (size_t i=0;i<errors.size();++i) { if (i) std::cout << ','; quoted(errors[i]); }
			std::cout << "]}\n"; return errors.empty() ? 0:2;
		}
		if (command=="roles" || command=="bind")
		{
			if (argc==4 && wxString(argv[3])=="--suggest")
			{
				if (command=="roles")
				{
					const auto proposed=suggestGlbPartRoles(model);
					for (size_t i=0;i<proposed.size();++i)
						if (project.roles[i]==GlbPartRole::Unassigned) assignGlbPartRole(model,project,i,proposed[i]);
				}
				else
				{
					if (project.skeleton.bones.empty()) throw std::invalid_argument("Load reference skeleton before bone suggestions.");
					const auto proposed=suggestGlbBones(model,project);
					for (size_t i=0;i<proposed.size();++i)
						if (project.boneBindings[i]<0 && proposed[i]>=0) assignGlbBone(model,project,i,proposed[i]);
				}
			}
			else
			{
				expect(5);
				const auto part=sourceObject(model,index(argv[3]));
				if (command=="roles")
				{
					bool found=false;
					for (const auto& role:glbPartRoles())
						if (text(argv[4])==role.id) { assignGlbPartRole(model,project,part,role.role); found=true; break; }
					if (!found) throw std::invalid_argument("Unknown semantic role ID.");
				}
				else assignGlbBone(model,project,part,wxString(argv[4])=="-1" ? -1:int(index(argv[4])));
			}
		}
		else if (command=="align")
		{
			expect(14);
			if (wxString(argv[13])!="--confirm") throw std::invalid_argument("Explicit --confirm required.");
			project.alignment.scale=scalar(argv[3]); project.alignment.rotationDegrees=vector(argv,4);
			project.alignment.origin=vector(argv,7); project.alignment.offset=vector(argv,10); project.alignment.confirmed=true;
		}
		else if (command=="pivot")
		{
			expect(11);
			if (wxString(argv[10])!="--confirm") throw std::invalid_argument("Explicit --confirm required.");
			GlbPivot pivot;
			pivot.position=vector(argv,4); pivot.axis=vector(argv,7); pivot.confirmed=true;
			assignGlbPivot(model,project,sourceObject(model,index(argv[3])),pivot);
		}
		else if (command=="reference") { expect(4); setGlbReferenceSkeleton(model,project,loadReferenceSkeleton(argv[3])); }
		else if (command=="fit-bone") { expect(4); fitGlbBoneToPivot(model,project,sourceObject(model,index(argv[3]))); }
		else if (command=="bone-origin") { expect(7); moveGlbBoneOrigin(model,project,index(argv[3]),vector(argv,4)); }
		else throw std::invalid_argument("Unknown command; use --help.");
		saveGlbImportProject(path,model,project);
		summary(model,&project);
		return 0;
	}
	catch (const std::exception& error) { std::cerr << "ERROR: " << error.what() << '\n'; return 1; }
}
