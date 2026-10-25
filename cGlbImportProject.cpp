#include <wx/wx.h>
#include <wx/file.h>
#include <wx/filename.h>
#include "cGlbImportProject.h"
#include "cGlbPlacement.h"
#include "cGlbRig.h"
#include "cRscWriter.h"
#include "vendor/rapidxml/rapidxml_ext.h"
#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <iomanip>
#include <limits>
#include <locale>

namespace
{
	[[noreturn]] void fail(const std::string& text) { throw std::runtime_error("GLB project: " + text); }
	void bindings(const GlbModel& model, const GlbImportProject& project)
	{
		if (project.sourceSha256 != model.sourceSha256 || project.sourceSha256.size() != 64 ||
			project.sourceSha256.find_first_not_of("0123456789abcdef") != std::string::npos)
			fail("Source GLB SHA-256 differs; reimport the changed source instead of reusing old assignments.");
		if (project.roles.size() != model.parts.size() || project.roles.empty() || project.roles.size() > 128)
			fail("Part count does not match the source.");
		if (project.pivots.size() != model.parts.size()) fail("Pivot count does not match the source.");
		validateGlbPlacement(project);
		validateGlbRigData(model,project);
		std::map<size_t, GlbPartRole> nodes;
		std::map<size_t, GlbPivot> pivots;
		for (size_t i = 0; i < project.roles.size(); ++i)
		{
			glbPartRoleInfo(project.roles[i]);
			const auto node = model.parts[i].nodeIndex;
			const auto found = nodes.find(node);
			if (found != nodes.end() && found->second != project.roles[i])
				fail("All material primitives of one source object must share a role.");
			nodes[node] = project.roles[i];
			const auto& pivot = project.pivots[i];
			const auto previous = pivots.find(node);
			if (previous != pivots.end() && (previous->second.position != pivot.position ||
				previous->second.axis != pivot.axis || previous->second.confirmed != pivot.confirmed))
				fail("All material primitives of one source object must share a pivot.");
			pivots[node] = pivot;
		}
	}
	void metadata(const GlbImportProject& project)
	{
		if (project.sourcePath.empty()) fail("Source path is missing.");
		normalizeAssetName(project.modelName, "Model");
		const auto& name = project.weaponTemplate;
		if (name.size() <= 7 || name.size() > 80 || name.compare(0, 7, "WEAPON_") != 0 ||
			name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos)
			fail("Select a valid WEAPON_ template.");
	}
	bool word(const std::string& name, const std::string& value)
	{
		return (" " + name + " ").find(" " + value + " ") != std::string::npos;
	}
	GlbPartRole suggestion(const GlbPart& part)
	{
		for (auto it = part.nodeNames.rbegin(); it != part.nodeNames.rend(); ++it)
		{
			std::string name;
			for (unsigned char c : *it)
			{
				if (c >= 'A' && c <= 'Z') c += 'a'-'A';
				name += (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ? char(c) : ' ';
			}
			if (word(name, "magazine")) return GlbPartRole::Magazine;
			if (word(name, "charging handle")) return GlbPartRole::ChargingHandle;
			if (word(name, "bolt catch") || word(name, "bolt handle") || word(name, "chamber door") ||
				word(name, "magrelease") || word(name, "selector")) return GlbPartRole::MovingPart;
			if (word(name, "bolt") || word(name, "slide")) return GlbPartRole::Bolt;
			if (word(name, "trigger")) return GlbPartRole::Trigger;
			if (word(name, "suppressor")) return GlbPartRole::Suppressor;
			if (word(name, "flashlight")) return GlbPartRole::Flashlight;
			if (word(name, "scope")) return GlbPartRole::Scope;
			if (it == part.nodeNames.rbegin() && (word(name, "body") || word(name, "receiver") ||
				name == "base" || name.compare(0, 5, "base ") == 0)) return GlbPartRole::Body;
		}
		return GlbPartRole::Unassigned;
	}
	using Node = rapidxml::xml_node<>;
	using Document = rapidxml::xml_document<>;
	std::string attr(Node* node, const char* key)
	{
		auto* value = node->first_attribute(key);
		if (!value || value->next_attribute(key)) fail(std::string("Missing/duplicate XML attribute: ") + key);
		return std::string(value->value(), value->value_size());
	}
	void attributes(Node* node, const std::set<std::string>& allowed)
	{
		for (auto* a = node->first_attribute(); a; a = a->next_attribute())
			if (!allowed.count(std::string(a->name(), a->name_size()))) fail("Unknown project XML attribute.");
	}
	void add(Document& doc, Node* node, const char* key, const std::string& value)
	{
		node->append_attribute(doc.allocate_attribute(key, doc.allocate_string(value.c_str())));
	}
	size_t number(const std::string& text)
	{
		if (text.empty() || text.size() > 6 || text.find_first_not_of("0123456789") != std::string::npos)
			fail("Invalid project part index.");
		size_t result = 0;
		for (char c : text) result = result*10 + size_t(c-'0');
		return result;
	}
	std::string decimal(float value)
	{
		std::ostringstream out;
		out.imbue(std::locale::classic());
		out << std::setprecision(std::numeric_limits<float>::max_digits10) << value;
		return out.str();
	}
	uint32_t wordNumber(Node* node,const char* key)
	{
		const auto text=attr(node,key);
		if (text.empty() || text.size()>10 || text.find_first_not_of("0123456789")!=std::string::npos)
			fail("Invalid unsigned 32-bit field.");
		uint64_t value=0;
		for (char c:text) value=value*10+uint64_t(c-'0');
		if (value>std::numeric_limits<uint32_t>::max()) fail("Unsigned field exceeds 32 bits.");
		return uint32_t(value);
	}
	float scalar(Node* node, const char* key)
	{
		std::istringstream in(attr(node, key));
		in.imbue(std::locale::classic());
		float value;
		if (!(in >> value)) fail(std::string("Invalid numeric attribute: ") + key);
		in >> std::ws;
		if (!in.eof()) fail(std::string("Invalid numeric attribute: ") + key);
		return value;
	}
	bool flag(Node* node, const char* key)
	{
		const auto value = attr(node, key);
		if (value != "0" && value != "1") fail(std::string("Expected 0 or 1 for: ") + key);
		return value == "1";
	}
	void vector(Document& doc, Node* node, const char* prefix, const std::array<float, 3>& value)
	{
		for (unsigned k = 0; k < 3; ++k)
		{
			const std::string key = std::string(prefix) + "xyz"[k];
			add(doc, node, doc.allocate_string(key.c_str()), decimal(value[k]));
		}
	}
	std::array<float, 3> vector(Node* node, const char* prefix)
	{
		std::array<float, 3> value;
		for (unsigned k = 0; k < 3; ++k)
		{
			const std::string key = std::string(prefix) + "xyz"[k];
			value[k] = scalar(node, key.c_str());
		}
		return value;
	}
	GlbPivot sourcePivot(const GlbPart& part)
	{
		GlbPivot pivot;
		pivot.position = {part.sourceWorldTransform[12], -part.sourceWorldTransform[14], part.sourceWorldTransform[13]};
		return pivot;
	}
}

const std::vector<GlbRoleInfo>& glbPartRoles()
{
	static const std::vector<GlbRoleInfo> roles = {
		{GlbPartRole::Unassigned, "unassigned", "Unassigned"},
		{GlbPartRole::Body, "body", "Body / fixed detail"},
		{GlbPartRole::Magazine, "magazine", "Magazine"},
		{GlbPartRole::Bolt, "bolt", "Bolt / slide"},
		{GlbPartRole::Trigger, "trigger", "Trigger"},
		{GlbPartRole::ChargingHandle, "charging-handle", "Charging handle"},
		{GlbPartRole::MovingPart, "moving-part", "Other moving part"},
		{GlbPartRole::Scope, "scope", "Scope"},
		{GlbPartRole::Suppressor, "suppressor", "Suppressor"},
		{GlbPartRole::Flashlight, "flashlight", "Flashlight"},
		{GlbPartRole::Grip, "grip", "Optional grip"},
		{GlbPartRole::Excluded, "excluded", "Exclude from weapon"}
	};
	return roles;
}

const GlbRoleInfo& glbPartRoleInfo(GlbPartRole role)
{
	for (const auto& info : glbPartRoles()) if (info.role == role) return info;
	fail("Unknown part role.");
}

GlbImportProject createGlbImportProject(const GlbModel& model, const wxString& path, const std::string& weaponTemplate)
{
	GlbImportProject project;
	wxFileName source(path);
	if (!source.MakeAbsolute()) fail("Could not resolve source path.");
	project.sourcePath = source.GetFullPath();
	project.sourceSha256 = model.sourceSha256;
	project.modelName = "w_ar_" + source.GetName().ToStdString();
	project.weaponTemplate = weaponTemplate;
	project.roles.assign(model.parts.size(), GlbPartRole::Unassigned);
	project.boneBindings.assign(model.parts.size(),-1);
	for (const auto& part : model.parts) project.pivots.push_back(sourcePivot(part));
	bindings(model, project);
	return project;
}

std::vector<GlbPartRole> suggestGlbPartRoles(const GlbModel& model)
{
	std::vector<GlbPartRole> roles;
	for (const auto& part : model.parts) roles.push_back(suggestion(part));
	return roles;
}

void assignGlbPartRole(const GlbModel& model, GlbImportProject& project, size_t part, GlbPartRole role)
{
	bindings(model, project);
	glbPartRoleInfo(role);
	if (part >= model.parts.size()) fail("Selected part is out of range.");
	for (size_t i = 0; i < model.parts.size(); ++i)
		if (model.parts[i].nodeIndex == model.parts[part].nodeIndex)
		{
			if (project.roles[i] != role)
			{
				project.pivots[i].confirmed = false;
				project.boneBindings[i] = -1;
			}
			project.roles[i] = role;
		}
}

void assignGlbPivot(const GlbModel& model, GlbImportProject& project, size_t part, const GlbPivot& pivot)
{
	bindings(model, project);
	if (part >= model.parts.size()) fail("Selected pivot part is out of range.");
	auto candidate = project;
	for (size_t i = 0; i < model.parts.size(); ++i)
		if (model.parts[i].nodeIndex == model.parts[part].nodeIndex) candidate.pivots[i] = pivot;
	bindings(model, candidate);
	project.pivots = std::move(candidate.pivots);
}

std::vector<std::string> glbAssignmentErrors(const GlbModel& model, const GlbImportProject& project)
{
	bindings(model, project); metadata(project);
	std::vector<std::string> errors;
	std::set<size_t> unassigned;
	bool body = false;
	for (size_t i = 0; i < model.parts.size(); ++i)
	{
		if (project.roles[i] == GlbPartRole::Unassigned && unassigned.insert(model.parts[i].nodeIndex).second)
			errors.push_back("Assign or explicitly exclude: " + model.parts[i].name);
		if (project.roles[i] == GlbPartRole::Body) body = true;
	}
	if (!body) errors.push_back("At least one source object must be Body / fixed detail.");
	return errors;
}

std::vector<size_t> includedGlbParts(const GlbModel& model, const GlbImportProject& project)
{
	const auto errors = glbAssignmentErrors(model, project);
	if (!errors.empty()) fail(errors.front());
	std::vector<size_t> result;
	for (size_t i = 0; i < project.roles.size(); ++i)
		if (project.roles[i] != GlbPartRole::Excluded) result.push_back(i);
	return result;
}

void saveGlbImportProject(const wxString& path, const GlbModel& model, const GlbImportProject& project)
{
	bindings(model, project); metadata(project);
	if (wxFileName(path).SameAs(wxFileName(project.sourcePath))) fail("The project cannot overwrite its source GLB.");
	if (!path.Lower().EndsWith(".vwtimport.xml")) fail("Project filenames must end in .vwtimport.xml.");
	Document doc;
	auto* root = doc.allocate_node(rapidxml::node_element, "glb-import");
	doc.append_node(root);
	add(doc, root, "version", "3");
	wxFileName source(project.sourcePath);
	wxFileName destination(path);
	if (!destination.MakeAbsolute()) fail("Could not resolve project path.");
	if (!source.MakeRelativeTo(destination.GetPath())) source = wxFileName(project.sourcePath);
	add(doc, root, "source", source.GetFullPath().ToStdString(wxConvUTF8));
	add(doc, root, "sha256", project.sourceSha256);
	add(doc, root, "model", normalizeAssetName(project.modelName, "Model"));
	add(doc, root, "template", project.weaponTemplate);
	auto* alignment = doc.allocate_node(rapidxml::node_element, "alignment");
	root->append_node(alignment);
	add(doc, alignment, "scale", decimal(project.alignment.scale));
	vector(doc, alignment, "rotation-", project.alignment.rotationDegrees);
	vector(doc, alignment, "origin-", project.alignment.origin);
	vector(doc, alignment, "offset-", project.alignment.offset);
	add(doc, alignment, "confirmed", project.alignment.confirmed ? "1" : "0");
	if (!project.skeleton.bones.empty())
	{
		auto* skeleton=doc.allocate_node(rapidxml::node_element,"skeleton");
		root->append_node(skeleton);
		add(doc,skeleton,"template",project.skeletonTemplate);
		add(doc,skeleton,"binary-metadata",project.skeleton.binaryMetadata ? "1":"0");
		add(doc,skeleton,"unknown-1c",std::to_string(project.skeleton.unknown1C));
		add(doc,skeleton,"unknown-5c",std::to_string(project.skeleton.unknown5C));
		add(doc,skeleton,"tag-buckets",std::to_string(project.skeleton.tagBucketCount));
		for (unsigned k=0;k<3;++k)
		{
			const auto key="signature-"+std::to_string(k);
			add(doc,skeleton,doc.allocate_string(key.c_str()),std::to_string(project.skeleton.signatures[k]));
		}
		for (size_t i=0; i<project.skeleton.bones.size(); ++i)
		{
			const auto& sourceBone=project.skeleton.bones[i];
			auto* bone=doc.allocate_node(rapidxml::node_element,"bone");
			skeleton->append_node(bone);
			add(doc,bone,"index",std::to_string(i)); add(doc,bone,"name",sourceBone.name);
			add(doc,bone,"tag",std::to_string(sourceBone.tag));
			add(doc,bone,"parent",std::to_string(sourceBone.parent));
			add(doc,bone,"sibling",std::to_string(sourceBone.sibling));
			add(doc,bone,"flags",std::to_string(sourceBone.flags));
			vector(doc,bone,"translation-",sourceBone.translation);
			for (unsigned k=0; k<4; ++k)
			{
				const auto key=std::string("rotation-")+"xyzw"[k];
				add(doc,bone,doc.allocate_string(key.c_str()),decimal(sourceBone.rotation[k]));
				const auto extraKey=std::string("extra-")+"xyzw"[k];
				add(doc,bone,doc.allocate_string(extraKey.c_str()),decimal(sourceBone.transformExtra[k]));
			}
		}
		for (const auto& pair:project.skeleton.childPairs)
		{
			auto* link=doc.allocate_node(rapidxml::node_element,"link");
			skeleton->append_node(link); add(doc,link,"child",std::to_string(pair[0])); add(doc,link,"parent",std::to_string(pair[1]));
		}
	}
	for (size_t i = 0; i < model.parts.size(); ++i)
	{
		auto* part = doc.allocate_node(rapidxml::node_element, "part");
		root->append_node(part);
		add(doc, part, "index", std::to_string(i));
		add(doc, part, "node", std::to_string(model.parts[i].nodeIndex));
		add(doc, part, "primitive", std::to_string(model.parts[i].primitiveIndex));
		add(doc, part, "role", glbPartRoleInfo(project.roles[i]).id);
		vector(doc, part, "pivot-", project.pivots[i].position);
		vector(doc, part, "axis-", project.pivots[i].axis);
		add(doc, part, "confirmed", project.pivots[i].confirmed ? "1" : "0");
		add(doc,part,"bone",std::to_string(project.boneBindings[i]));
	}
	std::string xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
	rapidxml::print(std::back_inserter(xml), doc);
	cRscWriter::save(path, std::vector<uint8_t>(xml.begin(), xml.end()));
}

GlbImportProject loadGlbImportProject(const wxString& path, GlbModel& model)
{
	wxFile file(path, wxFile::read);
	if (!file.IsOpened()) fail("Could not open project.");
	const auto length = file.Length();
	if (length <= 0 || length > 1024*1024) fail("Project size must be 1 byte..1 MiB.");
	std::vector<char> xml(static_cast<size_t>(length)+1, 0);
	if (file.Read(xml.data(), static_cast<size_t>(length)) != length) fail("Could not read complete project.");
	if (std::find(xml.begin(), xml.end()-1, '\0') != xml.end()-1) fail("Unexpected NUL in project XML.");
	Document doc;
	try { doc.parse<rapidxml::parse_full>(xml.data()); }
	catch (const rapidxml::parse_error& error) { fail(std::string("Invalid XML: ") + error.what()); }
	Node* root = nullptr;
	for (auto* node = doc.first_node(); node; node = node->next_sibling())
	{
		if (node->type() == rapidxml::node_declaration || node->type() == rapidxml::node_comment) continue;
		if (root || node->type() != rapidxml::node_element || std::string(node->name()) != "glb-import")
			fail("Expected one glb-import root, without DTD/processing instructions.");
		root = node;
	}
	if (!root) fail("Missing glb-import root.");
	attributes(root, {"version", "source", "sha256", "model", "template"});
	const auto version = attr(root, "version");
	if (version != "1" && version != "2" && version != "3") fail("Unsupported import project version.");
	GlbImportProject project;
	const auto sourceText = attr(root, "source");
	wxFileName source(wxString::FromUTF8(sourceText));
	wxFileName projectFile(path);
	if (!projectFile.MakeAbsolute() || sourceText.empty() || !source.MakeAbsolute(projectFile.GetPath()))
		fail("Could not resolve source path.");
	project.sourcePath = source.GetFullPath();
	project.sourceSha256 = attr(root, "sha256");
	project.modelName = attr(root, "model");
	project.weaponTemplate = attr(root, "template");
	metadata(project);
	GlbModel loaded = loadGlb(project.sourcePath);
	bool hasAlignment = false;
	bool hasSkeleton = false;
	for (auto* node = root->first_node(); node; node = node->next_sibling())
	{
		if (node->type() == rapidxml::node_comment) continue;
		if (version != "1" && node->type() == rapidxml::node_element && std::string(node->name()) == "alignment")
		{
			if (hasAlignment || node->first_node()) fail("Expected exactly one empty alignment element.");
			attributes(node, {"scale", "rotation-x", "rotation-y", "rotation-z", "origin-x", "origin-y", "origin-z",
				"offset-x", "offset-y", "offset-z", "confirmed"});
			project.alignment.scale = scalar(node, "scale");
			project.alignment.rotationDegrees = vector(node, "rotation-");
			project.alignment.origin = vector(node, "origin-");
			project.alignment.offset = vector(node, "offset-");
			project.alignment.confirmed = flag(node, "confirmed");
			hasAlignment = true;
			continue;
		}
		if (version=="3" && node->type()==rapidxml::node_element && std::string(node->name())=="skeleton")
		{
			if (hasSkeleton) fail("Duplicate skeleton element.");
			hasSkeleton=true;
			attributes(node,{"template","binary-metadata","unknown-1c","unknown-5c","tag-buckets",
				"signature-0","signature-1","signature-2"});
			project.skeletonTemplate=attr(node,"template");
			project.skeleton.binaryMetadata=flag(node,"binary-metadata");
			project.skeleton.unknown1C=wordNumber(node,"unknown-1c");
			const auto unknown5C=wordNumber(node,"unknown-5c"), buckets=wordNumber(node,"tag-buckets");
			if (unknown5C>65535 || buckets>1024) fail("Invalid skeleton runtime field.");
			project.skeleton.unknown5C=uint16_t(unknown5C); project.skeleton.tagBucketCount=uint16_t(buckets);
			for (unsigned k=0;k<3;++k)
			{
				const auto key="signature-"+std::to_string(k);
				project.skeleton.signatures[k]=wordNumber(node,key.c_str());
			}
			for (auto* bone=node->first_node(); bone; bone=bone->next_sibling())
			{
				if (bone->type()==rapidxml::node_comment) continue;
				if (bone->type()==rapidxml::node_element && std::string(bone->name())=="link" && !bone->first_node())
				{
					attributes(bone,{"child","parent"});
					const auto child=number(attr(bone,"child")),parent=number(attr(bone,"parent"));
					if (child>255 || parent>255 || project.skeleton.childPairs.size()>=4096) fail("Invalid skeleton child pair.");
					project.skeleton.childPairs.push_back({uint16_t(child),uint16_t(parent)});
					continue;
				}
				if (bone->type()!=rapidxml::node_element || std::string(bone->name())!="bone" || bone->first_node())
					fail("Expected empty bone elements.");
				attributes(bone,{"index","name","tag","parent","sibling","flags","translation-x","translation-y","translation-z",
					"rotation-x","rotation-y","rotation-z","rotation-w","extra-x","extra-y","extra-z","extra-w"});
				if (project.skeleton.bones.size()>=256 || number(attr(bone,"index"))!=project.skeleton.bones.size())
					fail("Bone index/order is inconsistent.");
				SkeletonBone entry;
				entry.name=attr(bone,"name");
				const auto tag=number(attr(bone,"tag"));
				if (tag>65535) fail("Bone tag out of range.");
				entry.tag=uint16_t(tag);
				const auto parent=attr(bone,"parent");
				entry.parent=parent=="-1" ? -1 : int(number(parent));
				const auto sibling=attr(bone,"sibling");
				entry.sibling=sibling=="-1" ? -1 : int(number(sibling));
				const auto flags=wordNumber(bone,"flags");
				if (flags>65535) fail("Invalid bone flags.");
				entry.flags=uint16_t(flags);
				entry.translation=vector(bone,"translation-");
				for (unsigned k=0; k<4; ++k)
				{
					const auto key=std::string("rotation-")+"xyzw"[k];
					entry.rotation[k]=scalar(bone,key.c_str());
					const auto extraKey=std::string("extra-")+"xyzw"[k];
					entry.transformExtra[k]=scalar(bone,extraKey.c_str());
				}
				project.skeleton.bones.push_back(std::move(entry));
			}
			validateReferenceSkeleton(project.skeleton);
			continue;
		}
		if (node->type() != rapidxml::node_element || std::string(node->name()) != "part" || node->first_node())
			fail("Expected empty part elements.");
		if (version == "1") attributes(node, {"index", "node", "primitive", "role"});
		else if (version=="2") attributes(node, {"index", "node", "primitive", "role", "pivot-x", "pivot-y", "pivot-z",
			"axis-x", "axis-y", "axis-z", "confirmed"});
		else attributes(node, {"index", "node", "primitive", "role", "pivot-x", "pivot-y", "pivot-z",
			"axis-x", "axis-y", "axis-z", "confirmed","bone"});
		const auto i = number(attr(node, "index"));
		if (i != project.roles.size() || i >= loaded.parts.size() ||
			number(attr(node, "node")) != loaded.parts[i].nodeIndex ||
			number(attr(node, "primitive")) != loaded.parts[i].primitiveIndex)
			fail("Part identity/order does not match the source.");
		const auto role = attr(node, "role");
		auto found = std::find_if(glbPartRoles().begin(), glbPartRoles().end(),
			[&](const GlbRoleInfo& info) { return role == info.id; });
		if (found == glbPartRoles().end()) fail("Unknown saved part role.");
		project.roles.push_back(found->role);
		auto pivot = sourcePivot(loaded.parts[i]);
		if (version != "1")
		{
			pivot.position = vector(node, "pivot-");
			pivot.axis = vector(node, "axis-");
			pivot.confirmed = flag(node, "confirmed");
		}
		project.pivots.push_back(pivot);
		const auto bone=version=="3" ? attr(node,"bone") : "-1";
		project.boneBindings.push_back(bone=="-1" ? -1 : int(number(bone)));
	}
	if (version != "1" && !hasAlignment) fail("Missing alignment element.");
	bindings(loaded, project);
	model = std::move(loaded);
	return project;
}
