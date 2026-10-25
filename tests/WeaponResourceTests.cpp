#include "cWeaponResource.h"
#include "vendor/rapidxml/rapidxml_ext.h"
#include <wx/dir.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/utils.h>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>

namespace
{
	using Node = rapidxml::xml_node<>;

	void require(bool condition, const std::string& message)
	{
		if (!condition) throw std::runtime_error("Weapon resource test: " + message);
	}

	std::string read(const wxString& path)
	{
		wxFile file(path, wxFile::read);
		require(file.IsOpened(), "open fixture source");
		const auto length = file.Length();
		require(length > 0, "fixture source length");
		std::string result(static_cast<size_t>(length), '\0');
		require(file.Read(&result[0], result.size()) == length, "read fixture source");
		return result;
	}

	struct Xml
	{
		std::vector<char> bytes;
		std::unique_ptr<rapidxml::xml_document<>> document;
		rapidxml::xml_document<>& doc;
		explicit Xml(const std::string& source)
			: bytes(source.begin(), source.end()), document(new rapidxml::xml_document<>), doc(*document)
		{
			bytes.push_back('\0');
			doc.parse<rapidxml::parse_full | rapidxml::parse_no_data_nodes>(bytes.data());
		}
	};
	static_assert(sizeof(Xml) < 1024, "Test XML document pools must remain heap allocated.");

	Node* child(Node* node, const char* name)
	{
		require(node && node->first_node(name), std::string("missing XML field ") + name);
		return node->first_node(name);
	}

	std::string text(const Node* node)
	{
		return std::string(node->value(), node->value_size());
	}

	Node* info(Xml& xml)
	{
		return child(child(child(child(child(&xml.doc, "CWeaponInfoBlob"), "Infos"), "Item"), "Infos"), "Item");
	}

	std::string printed(const Node* node)
	{
		std::string result;
		rapidxml::print(std::back_inserter(result), *node);
		return result;
	}

	void fails(const std::function<void()>& action, const std::string& reason)
	{
		try { action(); }
		catch (const std::exception& error)
		{
			require(std::string(error.what()).find(reason) != std::string::npos,
				"unexpected exception: " + std::string(error.what()) + "; expected " + reason);
			return;
		}
		throw std::runtime_error("Expected weapon resource failure: " + reason);
	}

	std::map<std::string, std::string> filesByPath(const std::vector<WeaponResourceFile>& files)
	{
		std::map<std::string, std::string> result;
		for (const auto& file : files)
		{
			require(file.relativePath.find("..") == std::string::npos && file.relativePath[0] != '/',
				"relative output paths");
			require(result.emplace(file.relativePath, std::string(file.bytes.begin(), file.bytes.end())).second,
				"unique output paths");
		}
		return result;
	}

	size_t checkPersonality(Node* node, const std::string& expected, const std::string& normalized)
	{
		size_t count = 0;
		if (std::string(node->name(), node->name_size()) == "Weapons")
		{
			for (auto* item = node->first_node("Item"); item; item = item->next_sibling("Item"))
			{
				require(text(item) == expected, "personality weapon reference");
				item->value(normalized.c_str(), normalized.size());
				++count;
			}
		}
		else
			for (auto* item = node->first_node(); item; item = item->next_sibling())
				count += checkPersonality(item, expected, normalized);
		return count;
	}

	void checkTemplate(const wxString& repo, WeaponResourceSettings settings)
	{
		const auto root = repo + "\\templates";
		const auto sourceDirectory = root + "\\weapons\\" + wxString::FromUTF8(settings.weaponTemplate.c_str());
		const auto files = filesByPath(buildWeaponResourceFiles(settings, root));
		require(files.size() == 6, "six metadata/script files and no separate components");
		require(files == filesByPath(buildWeaponResourceFiles(settings, root)), "deterministic metadata output");
		const auto& manifest = files.at("fxmanifest.lua");
		require(manifest.find("fx_version 'cerulean'") != std::string::npos, "cerulean manifest");
		require(manifest.find("game 'gta5'") != std::string::npos, "GTA5 manifest");
		require(manifest.find("client_script 'cl_weaponNames.lua'") != std::string::npos, "name script registration");
		require(manifest.find("WEAPONCOMPONENTSINFO_FILE") == std::string::npos, "no missing component metadata registration");
		const std::pair<const char*, const char*> registrations[] = {
			{"weaponarchetypes.meta", "WEAPON_METADATA_FILE"}, {"weaponanimations.meta", "WEAPON_ANIMATIONS_FILE"},
			{"pedpersonality.meta", "PED_PERSONALITY_FILE"}, {"weapons.meta", "WEAPONINFO_FILE"}
		};
		for (const auto& registration : registrations)
		{
			const auto path = "meta/" + std::string(registration.first);
			require(files.count(path) == 1, "manifest target exists");
			require(manifest.find("data_file '" + std::string(registration.second) + "' '" + path + "'") != std::string::npos,
				"correct data_file registration");
			Xml parsed(files.at(path));
			require(parsed.doc.first_node()->type() == rapidxml::node_declaration, "valid XML declaration");
			require(files.at(path).find("encoding=\"UTF-8\"") != std::string::npos, "UTF-8 XML declaration");
		}
		require(files.at("cl_weaponNames.lua").find("AddTextEntry(\"" + settings.weaponId + "\", ") == 0,
			"HumanNameHash text entry key");

		Xml original(read(sourceDirectory + "\\weapons.meta")), generated(files.at("meta/weapons.meta"));
		auto* before = info(original);
		auto* after = info(generated);
		require(text(child(after, "Name")) == settings.weaponId, "weapon identifier");
		require(text(child(after, "Model")) == settings.modelName, "streamed weapon model");
		require(text(child(after, "HumanNameHash")) == settings.weaponId, "display name hash");
		const std::string slot = "SLOT_" + settings.weaponId;
		require(text(child(after, "Slot")) == slot, "unique add-on slot");
		auto* navigation = child(child(child(child(child(&generated.doc, "CWeaponInfoBlob"), "SlotNavigateOrder"), "Item"), "WeaponSlots"), "Item");
		require(text(child(navigation, "Entry")) == slot, "slot navigation reference");
		auto* oldNavigation = child(child(child(child(child(&original.doc, "CWeaponInfoBlob"), "SlotNavigateOrder"), "Item"), "WeaponSlots"), "Item");
		require(std::string(child(navigation, "OrderNumber")->first_attribute("value")->value()) ==
			std::string(child(oldNavigation, "OrderNumber")->first_attribute("value")->value()),
			"preserve navigation priority");
		if (auto* camos = before->first_node("CamoDiffuseTexIdxs"))
			for (auto* entry = camos->first_node("Item"); entry; entry = entry->next_sibling("Item"))
			{
				auto* key = entry->first_attribute("key");
				require(key != nullptr, "camo model key");
				key->value(settings.modelName.c_str(), settings.modelName.size());
			}
		const std::set<std::string> identity = {"Name", "Model", "Slot", "HumanNameHash", "AttachPoints"};
		for (auto* field = before->first_node(); field; field = field->next_sibling())
			if (field->type() == rapidxml::node_element && !identity.count(std::string(field->name(), field->name_size())))
				require(printed(field) == printed(child(after, field->name())), "preserve template gameplay field " + std::string(field->name()));
		if (auto* attachments = after->first_node("AttachPoints"))
			require(!attachments->first_node("Item"), "integrated geometry must not request stock attachment components");
		require(files.at("meta/weapons.meta").find("COMPONENT_") == std::string::npos, "no stock component dependency");
		// PickupHash, Audio and animation clip names intentionally remain stock:
		// they refer to GTA gameplay assets, not this resource's streamed model.
		require(text(child(after, "PickupHash")) == text(child(before, "PickupHash")), "preserve native pickup reference");

		Xml originalAnimations(read(sourceDirectory + "\\weaponanimations.meta"));
		Xml generatedAnimations(files.at("meta/weaponanimations.meta"));
		auto* animationRoot = child(&generatedAnimations.doc, "CWeaponAnimationsSets");
		size_t animationCount = 0;
		for (auto* set = child(animationRoot, "WeaponAnimationsSets")->first_node("Item"); set; set = set->next_sibling("Item"))
		{
			auto* animation = child(child(set, "WeaponAnimations"), "Item");
			auto* key = animation->first_attribute("key");
			require(key && std::string(key->value(), key->value_size()) == settings.weaponId, "all animation keys remapped");
			key->value(settings.weaponTemplate.c_str(), settings.weaponTemplate.size());
			++animationCount;
		}
		require(animationCount > 0, "animation mappings present");
		for (auto* set = child(child(&originalAnimations.doc, "CWeaponAnimationsSets"), "WeaponAnimationsSets")->first_node("Item");
			set; set = set->next_sibling("Item"))
			child(child(set, "WeaponAnimations"), "Item")->first_attribute("key")->value(
				settings.weaponTemplate.c_str(), settings.weaponTemplate.size());
		require(printed(animationRoot) == printed(child(&originalAnimations.doc, "CWeaponAnimationsSets")),
			"preserve all animation clips, fallbacks and rates");
		Xml originalPersonality(read(sourceDirectory + "\\pedpersonality.meta"));
		Xml generatedPersonality(files.at("meta/pedpersonality.meta"));
		auto* personalityRoot = child(&generatedPersonality.doc, "CPedModelInfo__PersonalityDataList");
		checkPersonality(personalityRoot, settings.weaponId, settings.weaponTemplate);
		const std::function<void(Node*)> normalizeOriginal = [&](Node* node)
		{
			if (std::string(node->name(), node->name_size()) == "Weapons")
			{
				auto* item = child(node, "Item");
				while (auto* extra = item->next_sibling("Item")) node->remove_node(extra);
				item->value(settings.weaponTemplate.c_str(), settings.weaponTemplate.size());
			}
			else
				for (auto* item = node->first_node(); item; item = item->next_sibling()) normalizeOriginal(item);
		};
		normalizeOriginal(child(&originalPersonality.doc, "CPedModelInfo__PersonalityDataList"));
		require(printed(personalityRoot) == printed(child(&originalPersonality.doc, "CPedModelInfo__PersonalityDataList")),
			"preserve personality movement and unholster settings");

		Xml archetypes(files.at("meta/weaponarchetypes.meta"));
		Xml originalArchetypes(read(sourceDirectory + "\\weaponarchetypes.meta"));
		Node* originalBase = nullptr;
		for (auto* entry = child(child(&originalArchetypes.doc, "CWeaponModelInfo__InitDataList"), "InitDatas")->first_node("Item");
			entry; entry = entry->next_sibling("Item"))
			if (wxString::FromUTF8(text(child(entry, "modelName")).c_str()).CmpNoCase(
				wxString::FromUTF8(text(child(before, "Model")).c_str())) == 0) originalBase = entry;
		auto* archetype = child(child(child(&archetypes.doc, "CWeaponModelInfo__InitDataList"), "InitDatas"), "Item");
		require(!archetype->next_sibling("Item"), "exactly one streamed archetype");
		require(text(child(archetype, "modelName")) == settings.modelName, "archetype model");
		require(text(child(archetype, "txdName")) == settings.modelName, "matching texture dictionary");
		require(text(child(archetype, "ptfxAssetName")) == (originalBase ? text(child(originalBase, "ptfxAssetName")) : "NULL"),
			"preserve model particle setting");
		require(std::string(child(archetype, "lodDist")->first_attribute("value")->value()) ==
			(originalBase ? std::string(child(originalBase, "lodDist")->first_attribute("value")->value()) : "500"),
			"preserve model LOD distance or use explicit generated default");
	}

	void checkComponents(const wxString& repo, const WeaponResourceSettings& settings)
	{
		const auto files = filesByPath(buildWeaponResourceFiles(settings, repo + "\\templates"));
		require(files.size() == 7 && files.count("meta/weaponcomponents.meta") == 1, "component metadata emitted once");
		const auto& manifest = files.at("fxmanifest.lua");
		require(manifest.find("data_file 'WEAPONCOMPONENTSINFO_FILE' 'meta/weaponcomponents.meta'") != std::string::npos,
			"component data_file registration");
		require(manifest.find("'meta/weaponcomponents.meta',") != std::string::npos, "component metadata declared in files");
		std::map<std::string, const WeaponResourceComponent*> expected;
		for (const auto& component : settings.components) expected.emplace(component.componentId, &component);
		Xml weapons(files.at("meta/weapons.meta"));
		size_t attachments = 0;
		for (auto* point = child(info(weapons), "AttachPoints")->first_node("Item"); point; point = point->next_sibling("Item"))
		{
			auto* entry = child(child(point, "Components"), "Item");
			const auto& component = *expected.at(text(child(entry, "Name")));
			require(!entry->next_sibling("Item"), "one requested component per attachment");
			require(text(child(point, "AttachBone")) == component.attachBone, "attachment references real weapon bone");
			auto* enabled = child(entry, "Default")->first_attribute("value");
			require(enabled && std::string(enabled->value(), enabled->value_size()) == (component.defaultEnabled ? "true" : "false"),
				"component defaults match request");
			++attachments;
		}
		require(attachments == settings.components.size(), "no stock attachment references remain");
		Xml components(files.at("meta/weaponcomponents.meta"));
		const char* types[] = {"CWeaponComponentClipInfo", "CWeaponComponentScopeInfo", "CWeaponComponentSuppressorInfo",
			"CWeaponComponentFlashLightInfo", "CWeaponComponentInfo"};
		const char* localBones[] = {"AAPClip", "AAPScop", "AAPSupp", "AAPFlsh", "AAPGrip"};
		const char* donorIds[] = {"COMPONENT_CARBINERIFLE_CLIP_01", "COMPONENT_AT_SCOPE_MEDIUM", "COMPONENT_AT_AR_SUPP",
			"COMPONENT_AT_AR_FLSH", "COMPONENT_AT_AR_AFGRIP"};
		size_t definitions = 0;
		std::set<std::string> declaredIds;
		for (auto* item = child(child(&components.doc, "CWeaponComponentInfoBlob"), "Infos")->first_node("Item");
			item; item = item->next_sibling("Item"))
		{
			const auto id = text(child(item, "Name"));
			require(declaredIds.insert(id).second, "unique declared components");
			const auto& component = *expected.at(id);
			const auto kind = static_cast<size_t>(component.kind);
			require(std::string(item->first_attribute("type")->value()) == types[kind], "correct native component type");
			require(text(child(item, "Model")) == component.modelName, "component references generated model");
			require(text(child(item, "WeaponAttachBone")) == component.attachBone, "component weapon-bone reference");
			require(text(child(item, "AttachBone")) == (component.localAttachBone.empty() ? localBones[kind] : component.localAttachBone),
				"native or explicitly supplied component-local anchor");
			require(std::string(child(item, "CreateObject")->first_attribute("value")->value()) == "true", "component object created");
			Xml donor(read(repo + "\\templates\\components\\" + wxString::FromUTF8(donorIds[kind]) + "\\weaponcomponents.meta"));
			auto* original = child(child(child(&donor.doc, "CWeaponComponentInfoBlob"), "Infos"), "Item");
			for (auto* field = original->first_node(); field; field = field->next_sibling())
				if (field->type() == rapidxml::node_element && std::string(field->name()) != "Name" &&
					std::string(field->name()) != "Model" && std::string(field->name()) != "WeaponAttachBone")
				{
					if (std::string(field->name()) == "AttachBone" && !component.localAttachBone.empty()) continue;
					require(printed(field) == printed(child(item, field->name())), "preserved component gameplay/effect field");
				}
			if (component.kind == WeaponResourceComponentKind::Clip)
			{
				require(std::string(child(item, "ClipSize")->first_attribute("value")->value()) == "30", "matching carbine clip capacity");
				require(std::string(child(item, "ReloadData")->first_attribute("ref")->value()) == "RELOAD_DEFAULT",
					"matching clip reload data");
			}
			++definitions;
		}
		require(definitions == expected.size(), "all requested component definitions emitted");
		Xml archetypes(files.at("meta/weaponarchetypes.meta"));
		std::set<std::string> models;
		for (auto* item = child(child(&archetypes.doc, "CWeaponModelInfo__InitDataList"), "InitDatas")->first_node("Item");
			item; item = item->next_sibling("Item"))
		{
			const auto model = text(child(item, "modelName"));
			require(models.insert(model).second, "unique base/component archetypes");
			require(text(child(item, "txdName")) == model, "component owns matching texture dictionary");
		}
		require(models.size() == settings.components.size() + 1 && models.count(settings.modelName), "base and component model declarations");
		for (const auto& component : settings.components)
			require(models.count(component.modelName) == 1, "all component model references resolve to declared archetypes");
	}

	// Bad-template fixtures belong to the test itself. Never edit shipped templates.
	struct Fixture
	{
		wxString root;
		wxString directory;
		std::map<std::string, std::string> originals;
		std::map<std::string, std::string> componentOriginals;
		explicit Fixture(const wxString& repo)
		{
			for (const char* name : {"weapons.meta", "weaponanimations.meta", "pedpersonality.meta", "weaponarchetypes.meta"})
				originals.emplace(name, read(repo + "\\templates\\weapons\\WEAPON_CARBINERIFLE\\" + name));
			for (const char* name : {"COMPONENT_CARBINERIFLE_CLIP_01", "COMPONENT_AT_SCOPE_MEDIUM", "COMPONENT_AT_AR_SUPP",
				"COMPONENT_AT_AR_FLSH", "COMPONENT_AT_AR_AFGRIP"})
				componentOriginals.emplace(name, read(repo + "\\templates\\components\\" + name + "\\weaponcomponents.meta"));
			root = repo + "\\tests\\weapon_resource_fixture_" + wxString::Format("%lu", static_cast<unsigned long>(wxGetProcessId()));
			require(!wxFileName::DirExists(root) && !wxFileName::FileExists(root), "fixture path must not already exist");
			directory = root + "\\weapons\\WEAPON_CARBINERIFLE";
			require(wxFileName::Mkdir(directory, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL), "create owned test fixture");
		}
		~Fixture()
		{
			wxFileName::Rmdir(root, wxPATH_RMDIR_RECURSIVE);
		}
		void write(const std::string& name, const std::string& contents)
		{
			wxFile file(wxFileName(directory, wxString::FromUTF8(name.c_str())).GetFullPath(), wxFile::write);
			require(file.IsOpened() && file.Write(contents.data(), contents.size()) == contents.size(), "write owned fixture");
		}
		void reset()
		{
			for (const auto& entry : originals) write(entry.first, entry.second);
			for (const auto& entry : componentOriginals) writeComponent(entry.first, entry.second);
		}
		void writeComponent(const std::string& name, const std::string& contents)
		{
			const auto path = root + "\\components\\" + wxString::FromUTF8(name.c_str());
			if (!wxFileName::DirExists(path))
				require(wxFileName::Mkdir(path, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL), "create owned component fixture");
			wxFile file(path + "\\weaponcomponents.meta", wxFile::write);
			require(file.IsOpened() && file.Write(contents.data(), contents.size()) == contents.size(), "write owned component fixture");
		}
	};

	std::string replaceOnce(std::string source, const std::string& from, const std::string& to)
	{
		const auto position = source.find(from);
		require(position != std::string::npos, "fixture mutation target exists");
		source.replace(position, from.size(), to);
		return source;
	}
}

void testWeaponResource(const wxString& repo)
{
	WeaponResourceSettings settings = {"WEAPON_CARBINERIFLE", "WEAPON_TEST_ADDON", "w_ar_test_addon", "Test carbine", "test-addon"};
	wxDir templates(repo + "\\templates\\weapons");
	require(templates.IsOpened(), "weapon templates directory");
	wxString templateName;
	unsigned checked = 0;
	for (bool more = templates.GetFirst(&templateName, wxEmptyString, wxDIR_DIRS); more; more = templates.GetNext(&templateName))
	{
		auto candidate = settings;
		candidate.weaponTemplate = std::string(templateName.utf8_str());
		// This shipped file has mismatched closing tags; never silently repair it.
		if (candidate.weaponTemplate == "WEAPON_MINISMG")
		{
			fails([&] { buildWeaponResourceFiles(candidate, repo + "\\templates"); }, "malformed XML");
			continue;
		}
		checkTemplate(repo, candidate);
		++checked;
	}
	require(checked >= 33, "all valid shipped templates checked");
	auto assembled = settings;
	assembled.components = {
		{"COMPONENT_TEST_CLIP_01", "w_ar_test_addon_mag1", "WAPClip", WeaponResourceComponentKind::Clip},
		{"COMPONENT_TEST_SCOPE", "w_ar_test_addon_scope", "WAPScop", WeaponResourceComponentKind::Scope},
		{"COMPONENT_TEST_SUPP", "w_ar_test_addon_supp", "WAPSupp", WeaponResourceComponentKind::Suppressor},
		{"COMPONENT_TEST_FLASH", "w_ar_test_addon_flash", "WAPFlshLasr", WeaponResourceComponentKind::Flashlight},
		{"COMPONENT_TEST_GRIP", "w_ar_test_addon_grip", "WAPGrip", WeaponResourceComponentKind::Grip}
	};
	checkComponents(repo, assembled);
	assembled.components[4].defaultEnabled = false;
	checkComponents(repo, assembled);
	assembled.components[4].defaultEnabled = true;
	auto rooted = assembled;
	for (auto& component : rooted.components)
		if (component.kind == WeaponResourceComponentKind::Clip || component.kind == WeaponResourceComponentKind::Scope ||
			component.kind == WeaponResourceComponentKind::Grip) component.localAttachBone = "Gun_Root";
	checkComponents(repo, rooted);
	const auto rootedFiles = filesByPath(buildWeaponResourceFiles(rooted, repo + "\\templates"));
	require(rootedFiles.at("meta/weaponcomponents.meta").find("AAPClip") == std::string::npos,
		"explicit magazine anchor leaves no donor AAPClip reference");
	require(rootedFiles.at("meta/weaponcomponents.meta").find("Gun_SuMuzzle") != std::string::npos &&
		rootedFiles.at("meta/weaponcomponents.meta").find("Gun_FLMuzzle") != std::string::npos &&
		rootedFiles.at("meta/weaponcomponents.meta").find("Bulb_On") != std::string::npos &&
		rootedFiles.at("meta/weaponcomponents.meta").find("Bulb_Off") != std::string::npos,
		"explicit anchors never silently rewrite muzzle or flashlight helpers");
	auto invalidAnchor = rooted;
	invalidAnchor.components[0].localAttachBone = "bad/bone";
	fails([&] { buildWeaponResourceFiles(invalidAnchor, repo + "\\templates"); }, "localAttachBone");
	for (unsigned field = 0; field < 3; ++field)
	{
		auto invalidComponent = assembled;
		auto& component = invalidComponent.components[0];
		if (field == 0) component.componentId = "../INVALID";
		else if (field == 1) component.modelName = "bad.model";
		else component.attachBone = "bad-bone";
		fails([&] { buildWeaponResourceFiles(invalidComponent, repo + "\\templates"); }, "component");
	}
	auto duplicate = assembled;
	duplicate.components[1].componentId = duplicate.components[0].componentId;
	fails([&] { buildWeaponResourceFiles(duplicate, repo + "\\templates"); }, "Duplicate componentId");
	duplicate = assembled; duplicate.components[0].modelName = settings.modelName;
	fails([&] { buildWeaponResourceFiles(duplicate, repo + "\\templates"); }, "collision with base");
	duplicate = assembled; duplicate.components.push_back(duplicate.components[0]);
	duplicate.components.back().componentId = "COMPONENT_SECOND_CLIP";
	duplicate.components.back().modelName = "second_clip";
	fails([&] { buildWeaponResourceFiles(duplicate, repo + "\\templates"); }, "one component per kind");
	duplicate = assembled; duplicate.components[0].attachBone = "WAPScop";
	fails([&] { buildWeaponResourceFiles(duplicate, repo + "\\templates"); }, "does not match");
	duplicate = assembled; duplicate.components[0].kind = static_cast<WeaponResourceComponentKind>(99);
	fails([&] { buildWeaponResourceFiles(duplicate, repo + "\\templates"); }, "Unknown component kind");
	duplicate = assembled; duplicate.components[0].componentId = "INVALID_PREFIX";
	fails([&] { buildWeaponResourceFiles(duplicate, repo + "\\templates"); }, "COMPONENT_");
	duplicate = assembled; duplicate.components[0].componentId = "COMPONENT_CARBINERIFLE_CLIP_01";
	fails([&] { buildWeaponResourceFiles(duplicate, repo + "\\templates"); }, "must differ from the stock");
	duplicate = assembled; duplicate.weaponTemplate = "WEAPON_STUNGUN";
	fails([&] { buildWeaponResourceFiles(duplicate, repo + "\\templates"); }, "COMPONENT_STUNGUN_CLIP_01");
	auto matched = assembled;
	matched.weaponTemplate = "WEAPON_PISTOL";
	matched.components.resize(1);
	const auto pistolFiles = filesByPath(buildWeaponResourceFiles(matched, repo + "\\templates"));
	Xml pistolComponent(pistolFiles.at("meta/weaponcomponents.meta"));
	auto* pistolClip = child(child(child(&pistolComponent.doc, "CWeaponComponentInfoBlob"), "Infos"), "Item");
	require(std::string(child(pistolClip, "ClipSize")->first_attribute("value")->value()) == "12", "weapon-matched pistol clip capacity");
	require(std::string(child(pistolClip, "ReloadData")->first_attribute("ref")->value()) == "RELOAD_DEFAULT_WITH_EMPTIES",
		"weapon-matched pistol reload data");
	matched = assembled; matched.weaponTemplate = "WEAPON_ADVANCEDRIFLE";
	matched.components = {assembled.components[1]};
	const auto advancedFiles = filesByPath(buildWeaponResourceFiles(matched, repo + "\\templates"));
	Xml advancedComponent(advancedFiles.at("meta/weaponcomponents.meta"));
	Xml smallScope(read(repo + "\\templates\\components\\COMPONENT_AT_SCOPE_SMALL\\weaponcomponents.meta"));
	require(text(child(child(child(child(&advancedComponent.doc, "CWeaponComponentInfoBlob"), "Infos"), "Item"), "LocName")) ==
		text(child(child(child(child(&smallScope.doc, "CWeaponComponentInfoBlob"), "Infos"), "Item"), "LocName")),
		"prefer weapon template stock scope over generic scope");
	matched = assembled; matched.components = {assembled.components[1]}; matched.components[0].attachBone = "WAPScop_2";
	const auto fixedFiles = filesByPath(buildWeaponResourceFiles(matched, repo + "\\templates"));
	Xml fixedComponent(fixedFiles.at("meta/weaponcomponents.meta"));
	require(text(child(child(child(child(&fixedComponent.doc, "CWeaponComponentInfoBlob"), "Infos"), "Item"), "WeaponAttachBone")) == "WAPScop_2",
		"supported fixed-zoom scope bone uses matching shipped template");
	auto escaped = settings;
	escaped.displayName = "A \"quoted\" \\ rifle\n2 & <test> \xc5\xbd";
	const auto script = filesByPath(buildWeaponResourceFiles(escaped, repo + "\\templates")).at("cl_weaponNames.lua");
	require(script == "AddTextEntry(\"WEAPON_TEST_ADDON\", \"A \\\"quoted\\\" \\\\ rifle\\0102 & <test> \xc5\xbd\")\n",
		"Lua quotes, slashes, newlines, digit boundaries and UTF-8 escaped safely");
	for (const auto& value : {std::string("../escape"), std::string("bad-name"), std::string("x.y"),
		std::string("bad\xff"), std::string("x\0y", 3), std::string(64, 'A')})
	{
		auto invalid = settings;
		invalid.modelName = value;
		fails([&] { buildWeaponResourceFiles(invalid, repo + "\\templates"); }, "modelName");
	}
	auto invalid = settings;
	invalid.weaponId = "PLAIN_ID";
	fails([&] { buildWeaponResourceFiles(invalid, repo + "\\templates"); }, "WEAPON_");
	invalid = settings; invalid.weaponTemplate = "..\\WEAPON_CARBINERIFLE";
	fails([&] { buildWeaponResourceFiles(invalid, repo + "\\templates"); }, "weaponTemplate");
	invalid = settings; invalid.weaponId = settings.weaponTemplate;
	fails([&] { buildWeaponResourceFiles(invalid, repo + "\\templates"); }, "must differ");
	invalid = settings; invalid.resourceName = "bad' resource";
	fails([&] { buildWeaponResourceFiles(invalid, repo + "\\templates"); }, "resourceName");
	for (const auto& value : {std::string(), std::string("\xc0\x80"), std::string("bad\0name", 8)})
	{
		invalid = settings; invalid.displayName = value;
		fails([&] { buildWeaponResourceFiles(invalid, repo + "\\templates"); }, "displayName");
	}
	invalid = settings; invalid.weaponTemplate = "WEAPON_NONEXISTENT_RESOURCE_TEST";
	fails([&] { buildWeaponResourceFiles(invalid, repo + "\\templates"); }, "weapons.meta");

	Fixture fixture(repo);
	const auto bad = [&](const std::string& filename, const std::string& contents, const std::string& reason)
	{
		fixture.reset();
		fixture.write(filename, contents);
		fails([&] { buildWeaponResourceFiles(settings, fixture.root); }, reason);
	};
	const auto& source = fixture.originals.at("weapons.meta");
	bad("weapons.meta", "", "empty");
	bad("weapons.meta", "<WrongRoot/>", "expected root");
	bad("weapons.meta", "<CWeaponInfoBlob><Infos></CWeaponInfoBlob>", "malformed XML");
	bad("weapons.meta", source + "<OtherRoot/>", "multiple XML root");
	bad("weapons.meta", replaceOnce(source, "<Name>WEAPON_CARBINERIFLE</Name>", ""), "exactly one Name");
	bad("weapons.meta", replaceOnce(source, "<Damage value=\"32.000000\" />", ""), "exactly one Damage");
	bad("weapons.meta", replaceOnce(source, "<Damage value=\"32.000000\" />", "<Damage value=\"not-a-number\" />"), "invalid numeric value");
	bad("weapons.meta", replaceOnce(source, "WEAPON_CARBINERIFLE", "WEAPON_WRONG"), "does not match template");
	bad("weapons.meta", replaceOnce(source, "<Entry>SLOT_CARBINERIFLE</Entry>", "<Entry>SLOT_OTHER</Entry>"), "slot Entry");
	bad("weapons.meta", replaceOnce(source, "<Audio>AUDIO_ITEM_CARBINERIFLE</Audio>", "<Audio>&unknown;</Audio>"), "unknown XML entity");
	bad("weapons.meta", replaceOnce(source, "value=\"300\"", "value=\"300\" value=\"400\""), "duplicate XML attribute");
	bad("weapons.meta", "<!DOCTYPE CWeaponInfoBlob>" + source, "DOCTYPE");
	bad("weapons.meta", source + std::string(1, '\0'), "invalid UTF-8 or XML characters");
	bad("weapons.meta", replaceOnce(source, "<Audio>AUDIO_ITEM_CARBINERIFLE</Audio>", "<Audio>&#0;</Audio>"), "invalid XML character");
	bad("weaponanimations.meta", "<CWeaponAnimationsSets><WeaponAnimationsSets/></CWeaponAnimationsSets>", "missing weapon animation");
	bad("weaponanimations.meta", replaceOnce(fixture.originals.at("weaponanimations.meta"),
		"key=\"WEAPON_CARBINERIFLE\"", "key=\"INVALID KEY\""), "animation weapon key");
	bad("pedpersonality.meta", "<CPedModelInfo__PersonalityDataList/>", "exactly one MovementModeUnholsterData");
	bad("pedpersonality.meta", replaceOnce(fixture.originals.at("pedpersonality.meta"),
		"WEAPON_CARBINERIFLE", "INVALID REFERENCE"), "personality weapon");
	bad("weaponarchetypes.meta", "<CWeaponModelInfo__InitDataList/>", "exactly one InitDatas");
	bad("weaponarchetypes.meta", "<CWeaponModelInfo__InitDataList><InitDatas><Item/></InitDatas></CWeaponModelInfo__InitDataList>",
		"exactly one modelName");
	const auto badComponent = [&](const std::string& contents, const std::string& reason)
	{
		fixture.reset();
		fixture.writeComponent("COMPONENT_CARBINERIFLE_CLIP_01", contents);
		fails([&] { buildWeaponResourceFiles(assembled, fixture.root); }, reason);
	};
	const auto& clipSource = fixture.componentOriginals.at("COMPONENT_CARBINERIFLE_CLIP_01");
	badComponent("", "empty");
	badComponent("<CWeaponComponentInfoBlob><Infos></CWeaponComponentInfoBlob>", "malformed XML");
	badComponent(replaceOnce(clipSource, "CWeaponComponentClipInfo", "CWeaponComponentScopeInfo"), "component type");
	badComponent(replaceOnce(clipSource, "<Name>COMPONENT_CARBINERIFLE_CLIP_01</Name>", ""), "exactly one Name");
	badComponent(replaceOnce(clipSource, "<AttachBone>AAPClip</AttachBone>", "<AttachBone>WrongBone</AttachBone>"), "attachment bones");
	badComponent(replaceOnce(clipSource, "<WeaponAttachBone>WAPClip</WeaponAttachBone>", "<WeaponAttachBone>WAPScop</WeaponAttachBone>"), "attachment bones");
	badComponent(replaceOnce(clipSource, "<CreateObject value=\"true\" />", "<CreateObject value=\"false\" />"), "CreateObject");
	badComponent(replaceOnce(clipSource, "<ClipSize value=\"30\" />", "<ClipSize value=\"not-a-number\" />"), "numeric value");
	badComponent(replaceOnce(clipSource, "<ReloadData ref=\"RELOAD_DEFAULT\" />", "<ReloadData/>"), "ReloadData");

	fixture.reset();
	fixture.write("weapons.meta", replaceOnce(source, "<Audio>AUDIO_ITEM_CARBINERIFLE</Audio>",
		"<Audio>TEST&amp;&lt;&gt;&quot;&apos;</Audio>"));
	const auto escapedXml = filesByPath(buildWeaponResourceFiles(settings, fixture.root)).at("meta/weapons.meta");
	Xml roundTrip(escapedXml);
	require(text(child(info(roundTrip), "Audio")) == "TEST&<>\"'", "XML special characters round-trip without double escaping");
}
