#include "cWeaponResource.h"
#include "vendor/rapidxml/rapidxml_ext.h"
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <cmath>
#include <iterator>
#include <locale>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace
{
	using Node = rapidxml::xml_node<>;
	using Document = rapidxml::xml_document<>;

	std::string text(const Node* node)
	{
		return std::string(node->value(), node->value_size());
	}

	[[noreturn]] void badTemplate(const std::string& context, const std::string& reason)
	{
		throw std::runtime_error("Weapon resource template " + context + ": " + reason);
	}

	bool asciiLetter(unsigned char c)
	{
		return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
	}

	bool weaponReference(const std::string& value)
	{
		if (value.compare(0, 7, "WEAPON_") != 0 || value.size() <= 7) return false;
		for (unsigned char c : value)
			if (!asciiLetter(c) && !(c >= '0' && c <= '9') && c != '_') return false;
		return true;
	}

	void identifier(const std::string& value, const char* field, bool resource = false)
	{
		if (value.empty() || value.size() > 63 ||
			!(asciiLetter(static_cast<unsigned char>(value[0])) || (resource && value[0] >= '0' && value[0] <= '9')))
			throw std::invalid_argument(std::string(field) + " must be a 1-63 character ASCII identifier.");
		for (unsigned char c : value)
			if (!asciiLetter(c) && !(c >= '0' && c <= '9') && c != '_' && !(resource && c == '-'))
				throw std::invalid_argument(std::string(field) + " contains an invalid ASCII identifier character.");
	}

	std::string folded(std::string value)
	{
		for (char& c : value) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
		return value;
	}

	struct ComponentKindInfo
	{
		const char* type;
		const char* localBone;
		const char* weaponBone;
		const char* genericTemplate;
	};

	ComponentKindInfo componentKindInfo(WeaponResourceComponentKind kind)
	{
		switch (kind)
		{
		case WeaponResourceComponentKind::Clip:
			return {"CWeaponComponentClipInfo", "AAPClip", "WAPClip", ""};
		case WeaponResourceComponentKind::Scope:
			return {"CWeaponComponentScopeInfo", "AAPScop", "WAPScop", "COMPONENT_AT_SCOPE_MEDIUM"};
		case WeaponResourceComponentKind::Suppressor:
			return {"CWeaponComponentSuppressorInfo", "AAPSupp", "WAPSupp", "COMPONENT_AT_AR_SUPP"};
		case WeaponResourceComponentKind::Flashlight:
			return {"CWeaponComponentFlashLightInfo", "AAPFlsh", "WAPFlshLasr", "COMPONENT_AT_AR_FLSH"};
		case WeaponResourceComponentKind::Grip:
			return {"CWeaponComponentInfo", "AAPGrip", "WAPGrip", "COMPONENT_AT_AR_AFGRIP"};
		default:
			throw std::invalid_argument("Unknown component kind.");
		}
	}

	bool componentTemplateKind(const std::string& name, WeaponResourceComponentKind kind)
	{
		switch (kind)
		{
		case WeaponResourceComponentKind::Scope: return name.compare(0, 19, "COMPONENT_AT_SCOPE_") == 0;
		case WeaponResourceComponentKind::Suppressor: return name.find("_SUPP") != std::string::npos;
		case WeaponResourceComponentKind::Flashlight: return name.find("_FLSH") != std::string::npos;
		case WeaponResourceComponentKind::Grip: return name == "COMPONENT_AT_AR_AFGRIP";
		default: return false;
		}
	}

	std::string componentTemplate(const WeaponResourceSettings& settings,
		const WeaponResourceComponent& component, Node* stockAttachments, bool pistol)
	{
		if (component.kind == WeaponResourceComponentKind::Clip)
			return "COMPONENT_" + settings.weaponTemplate.substr(7) + "_CLIP_01";
		if (stockAttachments)
			for (auto* point = stockAttachments->first_node("Item"); point; point = point->next_sibling("Item"))
			{
				auto* bone = point->first_node("AttachBone");
				auto* list = point->first_node("Components");
				if (!bone || !list || folded(text(bone)) != folded(component.attachBone)) continue;
				for (auto* item = list->first_node("Item"); item; item = item->next_sibling("Item"))
				{
					auto* name = item->first_node("Name");
					if (name && componentTemplateKind(text(name), component.kind))
					{
						identifier(text(name), "stock component template");
						return text(name);
					}
				}
			}
		if (component.kind == WeaponResourceComponentKind::Scope && folded(component.attachBone) == "WAPSCOP_2")
			return "COMPONENT_AT_SCOPE_LARGE_FIXED_ZOOM";
		if (pistol && component.kind == WeaponResourceComponentKind::Suppressor) return "COMPONENT_AT_PI_SUPP";
		if (pistol && component.kind == WeaponResourceComponentKind::Flashlight) return "COMPONENT_AT_PI_FLSH";
		return componentKindInfo(component.kind).genericTemplate;
	}

	bool xmlScalar(uint32_t c)
	{
		return c == 9 || c == 10 || c == 13 || (c >= 0x20 && c <= 0xd7ff) ||
			(c >= 0xe000 && c <= 0xfffd) || (c >= 0x10000 && c <= 0x10ffff);
	}

	bool validUtf8Xml(const std::string& value)
	{
		for (size_t i = 0; i < value.size();)
		{
			uint32_t c = static_cast<unsigned char>(value[i++]);
			unsigned more = 0;
			uint32_t minimum = 0;
			if (c >= 0xc2 && c <= 0xdf) { c &= 0x1f; more = 1; minimum = 0x80; }
			else if (c >= 0xe0 && c <= 0xef) { c &= 0x0f; more = 2; minimum = 0x800; }
			else if (c >= 0xf0 && c <= 0xf4) { c &= 7; more = 3; minimum = 0x10000; }
			else if (c >= 0x80) return false;
			if (value.size() - i < more) return false;
			for (unsigned j = 0; j < more; ++j)
			{
				const auto next = static_cast<unsigned char>(value[i++]);
				if ((next & 0xc0) != 0x80) return false;
				c = (c << 6) | (next & 0x3f);
			}
			if (c < minimum || !xmlScalar(c)) return false;
		}
		return true;
	}

	// RapidXML intentionally tolerates unknown entities. Reject them before parsing
	// rather than silently escaping and changing a malformed template on output.
	void validateEntities(const std::string& source, const std::string& context)
	{
		for (size_t i = 0; i < source.size(); ++i)
		{
			const bool comment = source.compare(i, 4, "<!--") == 0;
			const bool cdata = source.compare(i, 9, "<![CDATA[") == 0;
			if (comment || cdata)
			{
				const auto end = source.find(comment ? "-->" : "]]>", i + (comment ? 4 : 9));
				if (end == std::string::npos) badTemplate(context, "unterminated comment or CDATA.");
				i = end + 2;
			}
			else if (source[i] == '&')
			{
				const auto end = source.find(';', i + 1);
				if (end == std::string::npos) badTemplate(context, "unterminated XML entity.");
				const auto entity = source.substr(i + 1, end - i - 1);
				if (entity != "amp" && entity != "lt" && entity != "gt" && entity != "quot" && entity != "apos")
				{
					if (entity.size() < 2 || entity[0] != '#') badTemplate(context, "unknown XML entity.");
					const bool hex = entity[1] == 'x';
					const size_t first = hex ? 2 : 1;
					if (first == entity.size()) badTemplate(context, "empty XML character reference.");
					uint32_t value = 0;
					for (size_t k = first; k < entity.size(); ++k)
					{
						const char c = entity[k];
						const int digit = c >= '0' && c <= '9' ? c - '0' :
							(hex && c >= 'a' && c <= 'f' ? c - 'a' + 10 :
							(hex && c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1));
						if (digit < 0 || value > 0x10ffffu / (hex ? 16u : 10u))
							badTemplate(context, "invalid XML character reference.");
						value = value * (hex ? 16 : 10) + digit;
					}
					if (!xmlScalar(value)) badTemplate(context, "invalid XML character reference.");
				}
				i = end;
			}
		}
	}

	void validateTree(Node* node, const std::string& context, unsigned depth = 0)
	{
		if (depth > 128) badTemplate(context, "XML nesting exceeds 128 levels.");
		if (node->type() == rapidxml::node_doctype) badTemplate(context, "DOCTYPE is not supported.");
		std::set<std::string> attributes;
		for (auto* a = node->first_attribute(); a; a = a->next_attribute())
			if (!attributes.emplace(a->name(), a->name_size()).second)
				badTemplate(context, "duplicate XML attribute.");
		for (auto* child = node->first_node(); child; child = child->next_sibling())
			validateTree(child, context, depth + 1);
	}

	struct Xml
	{
		std::string context;
		std::vector<char> buffer;
		// RapidXML embeds a 64 KiB pool; nested builders must not stack these.
		std::unique_ptr<Document> document;
		Document& doc;
		Node* root = nullptr;

		Xml(const wxString& directory, const char* filename, const char* rootName)
			: document(new Document), doc(*document)
		{
			wxLogNull noDialogs;
			const auto path = wxFileName(directory, filename).GetFullPath();
			context = std::string(path.utf8_str());
			wxFile file;
			if (!file.Open(path, wxFile::read)) badTemplate(context, "cannot open file.");
			const auto length = file.Length();
			if (length <= 0 || length > 16 * 1024 * 1024) badTemplate(context, "empty or oversized XML file.");
			buffer.resize(static_cast<size_t>(length));
			if (file.Read(buffer.data(), buffer.size()) != length) badTemplate(context, "cannot read complete file.");
			const std::string source(buffer.begin(), buffer.end());
			if (!validUtf8Xml(source)) badTemplate(context, "invalid UTF-8 or XML characters.");
			validateEntities(source, context);
			buffer.push_back('\0');
			try
			{
				doc.parse<rapidxml::parse_full | rapidxml::parse_no_data_nodes>(buffer.data());
			}
			catch (const rapidxml::parse_error& error)
			{
				badTemplate(context, std::string("malformed XML: ") + error.what());
			}
			validateTree(&doc, context);
			for (auto* node = doc.first_node(); node; node = node->next_sibling())
				if (node->type() == rapidxml::node_element)
				{
					if (root) badTemplate(context, "multiple XML root elements.");
					root = node;
				}
			if (!root || std::string(root->name(), root->name_size()) != rootName)
				badTemplate(context, std::string("expected root ") + rootName + ".");
		}

		Node* required(Node* parent, const char* name)
		{
			auto* child = parent->first_node(name);
			if (!child || child->next_sibling(name)) badTemplate(context, std::string("expected exactly one ") + name + ".");
			return child;
		}

		void set(Node* node, const std::string& value)
		{
			node->remove_all_nodes();
			node->value(doc.allocate_string(value.c_str()), value.size());
		}

		void append(Node* parent, const char* name, const std::string& value)
		{
			parent->append_node(doc.allocate_node(rapidxml::node_element, name, doc.allocate_string(value.c_str())));
		}

		std::string serialize() const
		{
			std::string result = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
			rapidxml::print(std::back_inserter(result), *root);
			return result;
		}
	};
	static_assert(sizeof(Xml) < 1024, "XML document pools must remain heap allocated.");

	void numericValue(Xml& xml, Node* node)
	{
		auto* attribute = node->first_attribute("value");
		if (!attribute) badTemplate(xml.context, "missing numeric value attribute.");
		std::istringstream input(std::string(attribute->value(), attribute->value_size()));
		input.imbue(std::locale::classic());
		double value = 0;
		input >> std::noskipws >> value;
		if (!input || !input.eof() || !std::isfinite(value))
			badTemplate(xml.context, "invalid numeric value attribute.");
	}

	void replaceSlotEntries(Xml& xml, Node* node, const std::string& oldSlot, const std::string& newSlot, size_t& count)
	{
		if (node->type() == rapidxml::node_element && std::string(node->name(), node->name_size()) == "Entry")
		{
			if (text(node) != oldSlot) badTemplate(xml.context, "slot Entry does not match weapon Slot.");
			xml.set(node, newSlot);
			++count;
		}
		for (auto* child = node->first_node(); child; child = child->next_sibling())
			replaceSlotEntries(xml, child, oldSlot, newSlot, count);
	}

	size_t replacePersonalityWeapons(Xml& xml, Node* node, const WeaponResourceSettings& settings)
	{
		size_t count = 0;
		if (node->type() == rapidxml::node_element && std::string(node->name(), node->name_size()) == "Weapons")
		{
			auto* first = node->first_node("Item");
			if (!first) badTemplate(xml.context, "empty personality Weapons list.");
			for (auto* item = first; item; item = item->next_sibling("Item"))
				if (!weaponReference(text(item))) badTemplate(xml.context, "invalid personality weapon reference.");
			// Several shipped templates retain a donor weapon ID in these lists.
			// The typed Weapons/Item field is authoritative, not a string search.
			// A shared stock list becomes one add-on entry, never duplicate keys.
			while (auto* extra = first->next_sibling("Item")) node->remove_node(extra);
			xml.set(first, settings.weaponId);
			return 1;
		}
		for (auto* child = node->first_node(); child; child = child->next_sibling())
			count += replacePersonalityWeapons(xml, child, settings);
		return count;
	}

	std::string luaString(const std::string& value)
	{
		std::string result = "\"";
		for (unsigned char c : value)
		{
			if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
			else if (c < 32 || c == 127)
			{
				result += '\\';
				result += static_cast<char>('0' + c / 100);
				result += static_cast<char>('0' + c / 10 % 10);
				result += static_cast<char>('0' + c % 10);
			}
			else result += static_cast<char>(c);
		}
		return result + '"';
	}
}

std::vector<WeaponResourceFile> buildWeaponResourceFiles(
	const WeaponResourceSettings& settings, const wxString& templatesRoot)
{
	identifier(settings.weaponTemplate, "weaponTemplate");
	identifier(settings.weaponId, "weaponId");
	identifier(settings.modelName, "modelName");
	identifier(settings.resourceName, "resourceName", true);
	if (settings.weaponTemplate.compare(0, 7, "WEAPON_") != 0 || settings.weaponTemplate.size() == 7 ||
		settings.weaponId.compare(0, 7, "WEAPON_") != 0 || settings.weaponId.size() == 7)
		throw std::invalid_argument("weaponTemplate and weaponId must start with WEAPON_ and have a nonempty suffix.");
	if (settings.weaponId == settings.weaponTemplate)
		throw std::invalid_argument("weaponId must differ from weaponTemplate for an add-on resource.");
	if (settings.displayName.empty() || settings.displayName.size() > 1024 || !validUtf8Xml(settings.displayName))
		throw std::invalid_argument("displayName must be nonempty valid UTF-8 without invalid XML characters (maximum 1024 bytes).");
	std::set<std::string> componentIds;
	std::set<std::string> modelNames = {folded(settings.modelName)};
	std::set<WeaponResourceComponentKind> kinds;
	for (const auto& component : settings.components)
	{
		identifier(component.componentId, "componentId");
		identifier(component.modelName, "component modelName");
		identifier(component.attachBone, "component attachBone");
		if (!component.localAttachBone.empty()) identifier(component.localAttachBone, "component localAttachBone");
		if (component.componentId.compare(0, 10, "COMPONENT_") != 0 || component.componentId.size() == 10)
			throw std::invalid_argument("componentId must start with COMPONENT_ and have a nonempty suffix.");
		const auto kind = componentKindInfo(component.kind);
		const auto bone = folded(component.attachBone);
		if (bone != folded(kind.weaponBone) && !(component.kind == WeaponResourceComponentKind::Scope && bone == "WAPSCOP_2"))
			throw std::invalid_argument("component attachBone does not match the component kind.");
		if (!componentIds.insert(folded(component.componentId)).second)
			throw std::invalid_argument("Duplicate componentId.");
		if (!modelNames.insert(folded(component.modelName)).second)
			throw std::invalid_argument("Duplicate component modelName or collision with base model.");
		if (!kinds.insert(component.kind).second)
			throw std::invalid_argument("Only one component per kind is supported.");
	}

	const auto directory = templatesRoot + "\\weapons\\" + wxString::FromUTF8(settings.weaponTemplate.c_str());
	Xml weapons(directory, "weapons.meta", "CWeaponInfoBlob");
	Xml animations(directory, "weaponanimations.meta", "CWeaponAnimationsSets");
	Xml personality(directory, "pedpersonality.meta", "CPedModelInfo__PersonalityDataList");
	Xml archetypes(directory, "weaponarchetypes.meta", "CWeaponModelInfo__InitDataList");

	auto* groups = weapons.required(weapons.root, "Infos");
	Node* info = nullptr;
	for (auto* group = groups->first_node("Item"); group; group = group->next_sibling("Item"))
	{
		auto* entries = weapons.required(group, "Infos");
		for (auto* entry = entries->first_node("Item"); entry; entry = entry->next_sibling("Item"))
		{
			auto* type = entry->first_attribute("type");
			if (!type || std::string(type->value(), type->value_size()) != "CWeaponInfo" || info)
				badTemplate(weapons.context, "expected exactly one CWeaponInfo item.");
			info = entry;
		}
	}
	if (!info) badTemplate(weapons.context, "missing CWeaponInfo item.");
	auto* name = weapons.required(info, "Name");
	if (text(name) != settings.weaponTemplate) badTemplate(weapons.context, "weapon Name does not match template.");
	auto* model = weapons.required(info, "Model");
	const auto originalModel = wxString::FromUTF8(text(model).c_str());
	auto* slot = weapons.required(info, "Slot");
	auto* humanName = weapons.required(info, "HumanNameHash");
	const auto originalSlot = text(slot);
	if (text(model).empty() || originalSlot.empty() || text(humanName).empty())
		badTemplate(weapons.context, "empty weapon identity field.");
	weapons.required(info, "Audio");
	for (const char* field : {"DamageType", "FireType", "WheelSlot", "Group", "WeaponFlags"})
		if (text(weapons.required(info, field)).empty()) badTemplate(weapons.context, std::string("empty ") + field + ".");
	for (const char* field : {"AmmoInfo", "AimingInfo"})
	{
		auto* reference = weapons.required(info, field)->first_attribute("ref");
		if (!reference || !reference->value_size()) badTemplate(weapons.context, std::string("missing ") + field + " ref.");
	}
	for (const char* field : {"ClipSize", "Damage", "WeaponRange"})
		numericValue(weapons, weapons.required(info, field));
	const std::string newSlot = "SLOT_" + settings.weaponId;
	size_t slots = 0;
	replaceSlotEntries(weapons, weapons.required(weapons.root, "SlotNavigateOrder"), originalSlot, newSlot, slots);
	if (!slots) badTemplate(weapons.context, "missing slot navigation Entry.");
	if (auto* bestOrder = weapons.root->first_node("SlotBestOrder"))
		replaceSlotEntries(weapons, bestOrder, originalSlot, newSlot, slots);
	if (auto* camos = info->first_node("CamoDiffuseTexIdxs"))
		for (auto* entry = camos->first_node("Item"); entry; entry = entry->next_sibling("Item"))
		{
			auto* key = entry->first_attribute("key");
			if (!key || originalModel.CmpNoCase(wxString::FromUTF8(key->value(), key->value_size())) != 0)
				badTemplate(weapons.context, "camo model key does not match weapon Model.");
			key->value(weapons.doc.allocate_string(settings.modelName.c_str()), settings.modelName.size());
		}
	std::string componentItems;
	auto* stockAttachments = info->first_node("AttachPoints");
	for (const auto& component : settings.components)
	{
		const auto kind = componentKindInfo(component.kind);
		const auto sourceId = componentTemplate(settings, component, stockAttachments, text(weapons.required(info, "Group")) == "GROUP_PISTOL");
		if (folded(component.componentId) == folded(sourceId))
			throw std::invalid_argument("componentId must differ from the stock component template ID.");
		const auto componentDirectory = templatesRoot + "\\components\\" + wxString::FromUTF8(sourceId.c_str());
		Xml source(componentDirectory, "weaponcomponents.meta", "CWeaponComponentInfoBlob");
		auto* item = source.required(source.required(source.root, "Infos"), "Item");
		auto* type = item->first_attribute("type");
		if (!type || std::string(type->value(), type->value_size()) != kind.type)
			badTemplate(source.context, "component type does not match requested kind.");
		if (text(source.required(item, "Name")) != sourceId)
			badTemplate(source.context, "component Name does not match selected template.");
		if (text(source.required(item, "Model")).empty())
			badTemplate(source.context, "empty component Model.");
		if (text(source.required(item, "AttachBone")) != kind.localBone ||
			folded(text(source.required(item, "WeaponAttachBone"))) != folded(component.attachBone))
			badTemplate(source.context, "component attachment bones do not match requested kind/bone.");
		auto* createObject = source.required(item, "CreateObject")->first_attribute("value");
		if (!createObject || std::string(createObject->value(), createObject->value_size()) != "true")
			badTemplate(source.context, "component CreateObject must be true.");
		source.set(source.required(item, "Name"), component.componentId);
		source.set(source.required(item, "Model"), component.modelName);
		source.set(source.required(item, "WeaponAttachBone"), component.attachBone);
		if (!component.localAttachBone.empty())
			source.set(source.required(item, "AttachBone"), component.localAttachBone);
		if (component.kind == WeaponResourceComponentKind::Clip)
		{
			auto* size = source.required(item, "ClipSize");
			numericValue(source, size);
			const auto* weaponSize = weapons.required(info, "ClipSize")->first_attribute("value");
			size->first_attribute("value")->value(source.doc.allocate_string(weaponSize->value(), weaponSize->value_size()),
				weaponSize->value_size());
			source.required(item, "AmmoInfo");
			auto* reload = source.required(item, "ReloadData")->first_attribute("ref");
			if (!reload || !reload->value_size()) badTemplate(source.context, "missing component ReloadData ref.");
		}
		rapidxml::print(std::back_inserter(componentItems), *item);
	}
	weapons.set(name, settings.weaponId);
	weapons.set(model, settings.modelName);
	weapons.set(slot, newSlot);
	weapons.set(humanName, settings.weaponId);
	auto* attachments = info->first_node("AttachPoints");
	if (attachments)
	{
		weapons.required(info, "AttachPoints");
		// Never retain stock components alongside either integrated geometry
		// or the explicitly requested separate custom component models.
		attachments->remove_all_nodes();
		attachments->value("", 0);
	}
	else if (!settings.components.empty())
	{
		attachments = weapons.doc.allocate_node(rapidxml::node_element, "AttachPoints");
		info->append_node(attachments);
	}
	for (const auto& component : settings.components)
	{
		auto* point = weapons.doc.allocate_node(rapidxml::node_element, "Item");
		attachments->append_node(point);
		weapons.append(point, "AttachBone", component.attachBone);
		auto* list = weapons.doc.allocate_node(rapidxml::node_element, "Components");
		point->append_node(list);
		auto* entry = weapons.doc.allocate_node(rapidxml::node_element, "Item");
		list->append_node(entry);
		weapons.append(entry, "Name", component.componentId);
		auto* enabled = weapons.doc.allocate_node(rapidxml::node_element, "Default");
		enabled->append_attribute(weapons.doc.allocate_attribute("value", component.defaultEnabled ? "true" : "false"));
		entry->append_node(enabled);
	}

	auto* sets = animations.required(animations.root, "WeaponAnimationsSets");
	size_t animationCount = 0;
	for (auto* set = sets->first_node("Item"); set; set = set->next_sibling("Item"))
	{
		auto* entries = animations.required(set, "WeaponAnimations");
		auto* entry = animations.required(entries, "Item");
		auto* key = entry->first_attribute("key");
		if (!key || !weaponReference(std::string(key->value(), key->value_size())))
			badTemplate(animations.context, "invalid animation weapon key.");
		key->value(animations.doc.allocate_string(settings.weaponId.c_str()), settings.weaponId.size());
		++animationCount;
	}
	if (!animationCount) badTemplate(animations.context, "missing weapon animation sets.");
	auto* unholster = personality.required(personality.root, "MovementModeUnholsterData");
	auto* movement = personality.required(personality.root, "MovementModes");
	if (!replacePersonalityWeapons(personality, personality.root, settings) &&
		(unholster->first_node("Item") || movement->first_node("Item")))
		badTemplate(personality.context, "missing personality weapon references.");

	auto* initDatas = archetypes.required(archetypes.root, "InitDatas");
	Node* baseArchetype = nullptr;
	const bool populated = initDatas->first_node("Item") != nullptr;
	for (auto* entry = initDatas->first_node("Item"); entry; entry = entry->next_sibling("Item"))
	{
		auto* archetypeModel = archetypes.required(entry, "modelName");
		archetypes.required(entry, "txdName");
		archetypes.required(entry, "ptfxAssetName");
		numericValue(archetypes, archetypes.required(entry, "lodDist"));
		if (originalModel.CmpNoCase(wxString::FromUTF8(text(archetypeModel).c_str())) == 0)
		{
			if (baseArchetype) badTemplate(archetypes.context, "duplicate base model archetype.");
			baseArchetype = entry;
		}
	}
	if (populated && !baseArchetype) badTemplate(archetypes.context, "missing base model archetype.");
	if (baseArchetype)
	{
		// Retain the base model's settings, not the template's separate stock
		// magazines or cosmetic variants: this resource streams just one model.
		initDatas->remove_node(baseArchetype);
		initDatas->remove_all_nodes();
		initDatas->append_node(baseArchetype);
		archetypes.set(archetypes.required(baseArchetype, "modelName"), settings.modelName);
		archetypes.set(archetypes.required(baseArchetype, "txdName"), settings.modelName);
	}
	else
	{
		initDatas->remove_all_nodes();
		baseArchetype = archetypes.doc.allocate_node(rapidxml::node_element, "Item");
		initDatas->append_node(baseArchetype);
		archetypes.append(baseArchetype, "modelName", settings.modelName);
		archetypes.append(baseArchetype, "txdName", settings.modelName);
		archetypes.append(baseArchetype, "ptfxAssetName", "NULL");
		auto* lod = archetypes.doc.allocate_node(rapidxml::node_element, "lodDist");
		lod->append_attribute(archetypes.doc.allocate_attribute("value", "500"));
		baseArchetype->append_node(lod);
	}

	for (const auto& component : settings.components)
	{
		auto* entry = archetypes.doc.allocate_node(rapidxml::node_element, "Item");
		initDatas->append_node(entry);
		archetypes.append(entry, "modelName", component.modelName);
		archetypes.append(entry, "txdName", component.modelName);
		archetypes.append(entry, "ptfxAssetName", "NULL");
		auto* lod = archetypes.doc.allocate_node(rapidxml::node_element, "lodDist");
		lod->append_attribute(archetypes.doc.allocate_attribute("value", "500"));
		entry->append_node(lod);
	}
	std::vector<std::pair<const char*, const char*>> registrations;
	if (!settings.components.empty()) registrations.push_back({"weaponcomponents.meta", "WEAPONCOMPONENTSINFO_FILE"});
	for (const auto& registration : std::vector<std::pair<const char*, const char*>>{
		{"weaponarchetypes.meta", "WEAPON_METADATA_FILE"},
		{"weaponanimations.meta", "WEAPON_ANIMATIONS_FILE"},
		{"pedpersonality.meta", "PED_PERSONALITY_FILE"},
		{"weapons.meta", "WEAPONINFO_FILE"}
	}) registrations.push_back(registration);
	std::string manifest = "fx_version 'cerulean'\ngame 'gta5'\n"
		"description " + luaString("Add-on weapon: " + settings.resourceName) + "\n\nfiles {\n";
	for (const auto& registration : registrations)
		manifest += "    'meta/" + std::string(registration.first) + "',\n";
	manifest += "}\n\n";
	for (const auto& registration : registrations)
		manifest += "data_file '" + std::string(registration.second) + "' 'meta/" + registration.first + "'\n";
	manifest += "\nclient_script 'cl_weaponNames.lua'\n";

	std::vector<WeaponResourceFile> result;
	const auto add = [&](const std::string& path, const std::string& bytes)
	{
		result.push_back({path, std::vector<uint8_t>(bytes.begin(), bytes.end())});
	};
	add("fxmanifest.lua", manifest);
	add("cl_weaponNames.lua", "AddTextEntry(" + luaString(settings.weaponId) + ", " + luaString(settings.displayName) + ")\n");
	add("meta/weapons.meta", weapons.serialize());
	add("meta/weaponanimations.meta", animations.serialize());
	add("meta/pedpersonality.meta", personality.serialize());
	add("meta/weaponarchetypes.meta", archetypes.serialize());
	if (!settings.components.empty())
		add("meta/weaponcomponents.meta", "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			"<CWeaponComponentInfoBlob>\n<Infos>\n" + componentItems +
			"</Infos>\n<InfoBlobName>" + settings.weaponId + "_COMPONENTS</InfoBlobName>\n</CWeaponComponentInfoBlob>\n");
	return result;
}
