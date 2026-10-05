#include "UserWidget.h"

#include "Panels.h"
#include "Private/WidgetSystem.h"

#include "../core/Engine.h"
#include "../core/Filesystem.h"
#include "../gameplay/PlayerController.h"
#include "../scripting/Private/ScriptSystem.h"

#include <xml/tinyxml2.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

namespace lynx
{
	namespace
	{
		constexpr int kFileVersion = 1;
		constexpr int kMaxNesting = 16;      // UserWidget inside UserWidget...
		int g_load_depth = 0;

		// ---------------------------------------------------------------------
		// Properties <-> text (own parser : Object::SetPropertyValueStr reads
		// "12" as an int, so a string property "12" would be lost)
		// ---------------------------------------------------------------------

		std::string FormatFloat(float v)
		{
			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(v));
			return buffer;
		}

		std::string PropertyText(const property& prop)
		{
			return std::visit([](auto* ptr) -> std::string
			{
				using T = std::remove_pointer_t<decltype(ptr)>;
				if (!ptr)
					return {};

				if constexpr (std::is_same_v<T, int>)
					return std::to_string(*ptr);
				else if constexpr (std::is_same_v<T, float>)
					return FormatFloat(*ptr);
				else if constexpr (std::is_same_v<T, bool>)
					return *ptr ? "true" : "false";
				else if constexpr (std::is_same_v<T, std::string>)
					return *ptr;
				else if constexpr (std::is_same_v<T, vec2>)
					return FormatFloat(ptr->x) + " " + FormatFloat(ptr->y);
				else if constexpr (std::is_same_v<T, vec3>)
					return FormatFloat(ptr->x) + " " + FormatFloat(ptr->y) + " " + FormatFloat(ptr->z);
				else if constexpr (std::is_same_v<T, vec4>)
					return FormatFloat(ptr->x) + " " + FormatFloat(ptr->y) + " " +
					       FormatFloat(ptr->z) + " " + FormatFloat(ptr->w);
				else
					return {};
			}, prop.property_member);
		}

		// "1 2 3", "1,2,3" or "vec3:1,2,3" -> numbers.
		std::vector<float> ParseNumbers(std::string text)
		{
			const size_t colon = text.find(':');
			if (colon != std::string::npos)
				text = text.substr(colon + 1);
			std::replace(text.begin(), text.end(), ',', ' ');

			std::vector<float> out;
			std::istringstream in(text);
			float v = 0.f;
			while (in >> v)
				out.push_back(v);
			return out;
		}

		void SetPropertyText(Object& object, const std::string& name, const std::string& text)
		{
			const auto& properties = object.GetProperties();
			auto it = properties.find(name);
			if (it == properties.end())
				return;

			std::visit([&](auto* ptr)
			{
				using T = std::remove_pointer_t<decltype(ptr)>;
				if (!ptr)
					return;

				if constexpr (std::is_same_v<T, std::string>)
				{
					*ptr = text;
				}
				else if constexpr (std::is_same_v<T, bool>)
				{
					*ptr = text == "true" || text == "1";
				}
				else
				{
					const std::vector<float> n = ParseNumbers(text);
					if constexpr (std::is_same_v<T, int>)
					{
						if (!n.empty()) *ptr = static_cast<int>(n[0]);
					}
					else if constexpr (std::is_same_v<T, float>)
					{
						if (!n.empty()) *ptr = n[0];
					}
					else if constexpr (std::is_same_v<T, vec2>)
					{
						if (n.size() >= 2) *ptr = vec2(n[0], n[1]);
					}
					else if constexpr (std::is_same_v<T, vec3>)
					{
						if (n.size() >= 3) *ptr = vec3(n[0], n[1], n[2]);
					}
					else if constexpr (std::is_same_v<T, vec4>)
					{
						if (n.size() >= 4) { ptr->x = n[0]; ptr->y = n[1]; ptr->z = n[2]; ptr->w = n[3]; }
					}
				}
			}, it->second.property_member);
		}

		void WriteProperties(const Object& object, tinyxml2::XMLElement* element)
		{
			for (const auto& [name, prop] : object.GetProperties())
			{
				if (prop.GetType() == 7)    // transform : not used by widgets
					continue;
				element->SetAttribute(name.c_str(), PropertyText(prop).c_str());
			}
		}

		void ReadProperties(Object& object, const tinyxml2::XMLElement* element)
		{
			for (const tinyxml2::XMLAttribute* a = element->FirstAttribute(); a; a = a->Next())
				SetPropertyText(object, a->Name(), a->Value());
		}

		// Direct children of a widget (not the inside of a nested UserWidget).
		void ForEachDirectChild(Widget& widget, const std::function<void(Widget&)>& fn)
		{
			if (auto* panel = dynamic_cast<PanelWidget*>(&widget))
			{
				for (int i = 0; i < panel->GetChildCount(); ++i)
					fn(*panel->GetChildAt(i));
			}
		}


		// ---------------------------------------------------------------------
		// Tree <-> XML
		// ---------------------------------------------------------------------

		tinyxml2::XMLElement* WriteWidget(tinyxml2::XMLDocument& doc, Widget& widget)
		{
			tinyxml2::XMLElement* element = doc.NewElement(widget.GetTypeName().c_str());
			WriteProperties(widget, element);

			if (PanelSlot* slot = widget.GetSlot())
			{
				tinyxml2::XMLElement* slot_element = doc.NewElement("Slot");
				WriteProperties(*slot, slot_element);
				element->InsertEndChild(slot_element);
			}

			// A nested UserWidget is saved as a reference (its `asset`).
			if (!dynamic_cast<UserWidget*>(&widget))
			{
				ForEachDirectChild(widget, [&](Widget& child)
				{
					element->InsertEndChild(WriteWidget(doc, child));
				});
			}
			return element;
		}

		std::unique_ptr<Widget> ReadWidget(const tinyxml2::XMLElement* element, std::string& error)
		{
			const std::string class_name = element->Name();
			std::unique_ptr<Widget> widget = ui::NewWidget(class_name);
			if (!widget)
			{
				error += "unknown widget class \"" + class_name + "\" ; ";
				return nullptr;
			}

			// Nested UserWidget : its tree comes from its asset.
			if (auto* nested = dynamic_cast<UserWidget*>(widget.get()))
			{
				const char* asset = element->Attribute("asset");
				if (asset && *asset)
				{
					if (g_load_depth >= kMaxNesting)
						error += std::string("\"") + asset + "\" : too many nested user widgets (a loop ?) ; ";
					else if (!nested->LoadFromAsset(asset))
						error += std::string("could not load the user widget \"") + asset + "\" ; ";
				}
			}

			ReadProperties(*widget, element);

			auto* panel = dynamic_cast<PanelWidget*>(widget.get());

			for (const tinyxml2::XMLElement* child = element->FirstChildElement(); child; child = child->NextSiblingElement())
			{
				if (std::string(child->Name()) == "Slot")
					continue;

				if (!panel)
				{
					error += class_name + " can not have children ; ";
					break;
				}

				std::unique_ptr<Widget> child_widget = ReadWidget(child, error);
				if (!child_widget)
					continue;

				PanelSlot* slot = panel->AddChild(std::move(child_widget));
				if (!slot)
				{
					error += class_name + " is full ; ";
					continue;
				}

				if (const tinyxml2::XMLElement* slot_element = child->FirstChildElement("Slot"))
					ReadProperties(*slot, slot_element);
			}

			return widget;
		}

		// The constructed UserWidgets of a tree, children first.
		std::vector<UserWidget*> CollectUserWidgets(UserWidget& top)
		{
			std::vector<UserWidget*> list;
			top.ForEachWidget([&](Widget& w)
			{
				if (auto* u = dynamic_cast<UserWidget*>(&w))
					list.push_back(u);
			});
			std::reverse(list.begin(), list.end());
			return list;
		}
	}


	// =========================================================================
	// UserWidget
	// =========================================================================

	UserWidget::UserWidget()
	{
		HPROPERTY(asset, Exposed, Invalidate());
	}

	UserWidget::~UserWidget()
	{
		if (in_viewport_)
			RemoveFromParent();

		// The tree first : its natives / serials go before this widget.
		root_.reset();
	}

	bool UserWidget::LoadFromAsset(const std::string& asset_path)
	{
		const auto data = fs::ReadBinary(asset_path);
		if (data.empty())
		{
			std::cerr << "[WIDGET] Could not read assets/" << asset_path << "\n";
			return false;
		}

		std::string error;
		const bool ok = LoadFromString(std::string(reinterpret_cast<const char*>(data.data()), data.size()), &error);
		if (!error.empty())
			std::cerr << "[WIDGET] " << asset_path << " : " << error << "\n";

		asset_path_ = asset_path;
		if (asset.empty())
			asset = asset_path;
		return ok;
	}

	bool UserWidget::LoadFromFile(const std::string& file_path)
	{
		std::ifstream in(file_path, std::ios::binary);
		if (!in)
			return false;

		std::stringstream buffer;
		buffer << in.rdbuf();

		std::string error;
		const bool ok = LoadFromString(buffer.str(), &error);
		if (!error.empty())
			std::cerr << "[WIDGET] " << file_path << " : " << error << "\n";
		return ok;
	}

	bool UserWidget::LoadFromString(const std::string& xml, std::string* error_out)
	{
		using namespace tinyxml2;

		XMLDocument doc;
		if (doc.Parse(xml.c_str(), xml.size()) != XML_SUCCESS)
		{
			if (error_out)
				*error_out = std::string("invalid XML : ") + doc.ErrorStr();
			return false;
		}

		const XMLElement* file = doc.FirstChildElement("Widget");
		if (!file)
		{
			if (error_out)
				*error_out = "not a widget file (no <Widget> element)";
			return false;
		}

		if (file->IntAttribute("version", kFileVersion) > kFileVersion)
			std::cerr << "[WIDGET] File made by a newer version of the editor\n";

		const char* class_name = file->Attribute("class");
		class_name_ = class_name ? class_name : "";
		design_size_ = vec2(file->FloatAttribute("design_width", 1920.f),
		                    file->FloatAttribute("design_height", 1080.f));

		std::string error;
		std::unique_ptr<Widget> root;

		++g_load_depth;
		if (const XMLElement* element = file->FirstChildElement())
			root = ReadWidget(element, error);
		--g_load_depth;

		SetRoot(std::move(root));

		if (error_out)
			*error_out = error;
		return true;
	}

	std::string UserWidget::SaveToString() const
	{
		using namespace tinyxml2;

		XMLDocument doc;
		doc.InsertFirstChild(doc.NewDeclaration());

		XMLElement* file = doc.NewElement("Widget");
		file->SetAttribute("version", kFileVersion);
		file->SetAttribute("class", class_name_.c_str());
		file->SetAttribute("design_width", FormatFloat(design_size_.x).c_str());
		file->SetAttribute("design_height", FormatFloat(design_size_.y).c_str());
		doc.InsertEndChild(file);

		if (root_)
			file->InsertEndChild(WriteWidget(doc, *root_));

		XMLPrinter printer;
		doc.Print(&printer);
		return printer.CStr();
	}

	bool UserWidget::SaveToFile(const std::string& file_path) const
	{
		std::ofstream out(file_path, std::ios::binary | std::ios::trunc);
		if (!out)
			return false;
		out << SaveToString();
		return static_cast<bool>(out);
	}

	void UserWidget::SetRoot(std::unique_ptr<Widget> root)
	{
		if (root_)
			WidgetSystem::DestroyNatives(*root_);

		root_ = std::move(root);
		if (root_)
		{
			root_->slot_ = nullptr;
			root_->root_of_ = this;
		}

		if (in_viewport_ && root_)
			WidgetSystem::CreateNatives(*root_, *WidgetSystem::GetTop(this));

		Invalidate();
	}

	std::unique_ptr<Widget> UserWidget::TakeRoot()
	{
		if (root_)
		{
			WidgetSystem::DestroyNatives(*root_);
			root_->root_of_ = nullptr;
		}
		Invalidate();
		return std::move(root_);
	}

	Widget* UserWidget::GetWidgetFromName(const std::string& name) const
	{
		if (!root_)
			return nullptr;

		Widget* found = nullptr;
		std::function<void(Widget&)> visit = [&](Widget& w)
		{
			if (found)
				return;
			if (w.GetName() == name)
			{
				found = &w;
				return;
			}
			ForEachDirectChild(w, visit);    // not inside nested UserWidgets
		};
		visit(*root_);
		return found;
	}

	void UserWidget::ForEachWidget(const std::function<void(Widget&)>& fn)
	{
		fn(*this);
		if (root_)
			root_->ForEachWidget(fn);
	}

	void UserWidget::AddToViewport(int z_order)
	{
		if (GetParent() || root_of_)
		{
			std::cerr << "[WIDGET] AddToViewport : \"" << GetName() << "\" is inside another widget\n";
			return;
		}

		z_order_ = z_order;

		if (!owner_ && Engine::Get())
			owner_ = Engine::Get()->GetDefaultPlayer();

		if (!owner_)
		{
			std::cerr << "[WIDGET] AddToViewport : no player\n";
			return;
		}

		if (!in_viewport_)
		{
			in_viewport_ = true;
			WidgetSystem::CreateNatives(*this, *this);
		}

		dirty_ = true;
	}

	void UserWidget::RemoveFromParent()
	{
		if (in_viewport_)
		{
			WidgetSystem::DestroyNatives(*this);
			in_viewport_ = false;
			return;
		}

		Widget::RemoveFromParent();
	}

	void UserWidget::SetOwningPlayer(PlayerController* player)
	{
		if (player == owner_)
			return;

		const bool was_on_screen = in_viewport_;
		if (was_on_screen)
			RemoveFromParent();

		owner_ = player;

		if (was_on_screen)
			AddToViewport(z_order_);
	}

	void UserWidget::LayoutForDesign(float width, float height)
	{
		Arrange(WidgetRect{ 0.f, 0.f, width, height });
	}

	vec2 UserWidget::ComputeDesiredSize() const
	{
		return root_ ? root_->GetDesiredSize() : vec2{ 0.f, 0.f };
	}

	void UserWidget::ArrangeChildren(const WidgetRect& rect)
	{
		// The root fills the whole widget, like in Unreal.
		if (root_)
			root_->Arrange(rect);
	}


	// =========================================================================
	// CreateWidget / DestroyWidget
	// =========================================================================

	UserWidget* CreateWidget(PlayerController* owner, const std::string& asset_path)
	{
		const auto data = fs::ReadBinary(asset_path);
		if (data.empty())
		{
			std::cerr << "[WIDGET] CreateWidget : could not read assets/" << asset_path << "\n";
			return nullptr;
		}

		// Class written in the file (a UserWidget subclass of the game).
		std::unique_ptr<UserWidget> widget;
		{
			tinyxml2::XMLDocument doc;
			if (doc.Parse(reinterpret_cast<const char*>(data.data()), data.size()) == tinyxml2::XML_SUCCESS)
			{
				const tinyxml2::XMLElement* file = doc.FirstChildElement("Widget");
				const char* class_name = file ? file->Attribute("class") : nullptr;

				if (class_name && *class_name)
				{
					std::unique_ptr<Widget> made = ui::NewWidget(class_name);
					if (auto* user = dynamic_cast<UserWidget*>(made.get()))
					{
						made.release();
						widget.reset(user);
					}
					else if (scripting::IsWidgetClass(class_name))
					{
						// JavaScript class : a UserWidget bound to it at Construct.
					}
					else
					{
						std::cerr << "[WIDGET] " << asset_path << " : class \"" << class_name
						          << "\" is not a registered UserWidget class (LYNX_MODULE_REGISTER)\n";
					}
				}
			}
		}

		if (!widget)
			widget = std::make_unique<UserWidget>();

		if (!widget->LoadFromAsset(asset_path))
			return nullptr;

		return ui::AdoptWidget(std::move(widget), owner);
	}

	void DestroyWidget(UserWidget* widget)
	{
		WidgetSystem::Destroy(widget);
	}

	void WidgetSystem::Destroy(UserWidget* widget)
	{
		if (!widget)
			return;

		auto& owned = Owned();
		auto it = std::find_if(owned.begin(), owned.end(),
		                       [widget](const auto& w) { return w.get() == widget; });
		if (it == owned.end())
		{
			std::cerr << "[WIDGET] DestroyWidget : not made by CreateWidget (delete it yourself)\n";
			return;
		}

		std::unique_ptr<UserWidget> doomed = std::move(*it);
		owned.erase(it);

		for (UserWidget* user : CollectUserWidgets(*doomed))
		{
			if (user->constructed_)
			{
				user->constructed_ = false;
				user->NativeDestruct();
				scripting::OnUserWidgetEvent(*user, scripting::WidgetEvent::Destruct, 0.f);
			}
		}

		doomed.reset();
	}

	UserWidget* WidgetSystem::Adopt(std::unique_ptr<UserWidget> widget, PlayerController* owner)
	{
		if (!widget)
			return nullptr;

		if (!owner && Engine::Get())
			owner = Engine::Get()->GetDefaultPlayer();

		widget->owner_ = owner;

		UserWidget* raw = widget.get();
		Owned().push_back(std::move(widget));

		// Children first, like UMG.
		for (UserWidget* user : CollectUserWidgets(*raw))
		{
			if (!user->constructed_)
			{
				user->constructed_ = true;
				user->NativeConstruct();
				scripting::OnUserWidgetEvent(*user, scripting::WidgetEvent::Construct, 0.f);
			}
		}
		return raw;
	}

	namespace ui
	{
		UserWidget* AdoptWidget(std::unique_ptr<UserWidget> widget, PlayerController* owner)
		{
			return WidgetSystem::Adopt(std::move(widget), owner);
		}

		void DestroyAllWidgets()
		{
			// NativeDestruct may destroy other widgets : one at a time.
			while (!WidgetSystem::Owned().empty())
				DestroyWidget(WidgetSystem::Owned().back().get());
		}

		std::string CopyWidget(Widget& widget)
		{
			tinyxml2::XMLDocument doc;
			tinyxml2::XMLElement* clip = doc.NewElement("LynxWidgetClipboard");
			doc.InsertFirstChild(clip);
			clip->InsertEndChild(WriteWidget(doc, widget));

			tinyxml2::XMLPrinter printer;
			doc.Print(&printer);
			return printer.CStr();
		}

		Widget* PasteWidget(PanelWidget& panel, const std::string& text, int index)
		{
			tinyxml2::XMLDocument doc;
			if (doc.Parse(text.c_str(), text.size()) != tinyxml2::XML_SUCCESS)
				return nullptr;

			const tinyxml2::XMLElement* clip = doc.FirstChildElement("LynxWidgetClipboard");
			const tinyxml2::XMLElement* element = clip ? clip->FirstChildElement() : nullptr;
			if (!element || !panel.CanAddChild())
				return nullptr;

			std::string error;
			std::unique_ptr<Widget> widget = ReadWidget(element, error);
			if (!widget)
				return nullptr;

			Widget* raw = widget.get();
			PanelSlot* slot = panel.InsertChildAt(index < 0 ? panel.GetChildCount() : index, std::move(widget));
			if (!slot)
				return nullptr;

			// Unknown names are skipped : another kind of slot keeps its defaults.
			if (const tinyxml2::XMLElement* slot_element = element->FirstChildElement("Slot"))
				ReadProperties(*slot, slot_element);
			return raw;
		}

		std::vector<UserWidget*> GetAllWidgets()
		{
			std::vector<UserWidget*> out;
			for (const auto& w : WidgetSystem::Owned())
				out.push_back(w.get());
			return out;
		}
	}
}
