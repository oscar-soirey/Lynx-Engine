// =============================================================================
// Plugin Dialogue : runtime module (editor AND game)
// -----------------------------------------------------------------------------
//   - DialogueRunner : plays a .dialogue (one dialogue at a time)
//   - the default dialogue box (widgets), can be turned off for a custom UI
//   - actors : DialogueNPC (Interactable : talk to it), DialogueTrigger (zone)
//   - interfaces : DialogueEvents, StoryEvents (see Story.h)
//   - JavaScript : Dialogue, Story, Quest
// =============================================================================

#include <plugins/LynxPlugin.h>

#include "DialogueData.h"
#include "Story.h"

#include <json/json.hpp>

#include <algorithm>
#include <iostream>
#include <memory>

using Json = nlohmann::json;

namespace
{
	// =========================================================================
	// JS helpers
	// =========================================================================

	bool Truthy(const Json& j)
	{
		if (j.is_null() || j.is_discarded())
			return false;
		if (j.is_boolean())
			return j.get<bool>();
		if (j.is_number())
			return j.get<double>() != 0.0;
		if (j.is_string())
			return !j.get<std::string>().empty();
		return true;
	}

	/** JS condition ("" = true). */
	bool Condition(const std::string& code)
	{
		if (code.empty())
			return true;
		std::string result, error;
		if (!lynx::EvaluateScript(("(" + code + ")").c_str(), result, error, "<dialogue condition>"))
		{
			std::cout << "[Dialogue] condition \"" << code << "\" : " << error << "\n";
			return false;
		}
		return Truthy(Json::parse(result, nullptr, false));
	}

	lynx::InterfaceArg JsonToArg(const Json& j)
	{
		if (j.is_boolean()) return j.get<bool>();
		if (j.is_number()) return j.get<float>();
		if (j.is_string()) return j.get<std::string>();
		if (j.is_null() || j.is_discarded()) return std::monostate{};
		return j.dump();
	}

	Json ArgToJson(const lynx::InterfaceArg& a)
	{
		return std::visit([](const auto& v) -> Json
		{
			using T = std::decay_t<decltype(v)>;
			if constexpr (std::is_same_v<T, std::monostate>) return nullptr;
			else if constexpr (std::is_same_v<T, bool>) return v;
			else if constexpr (std::is_same_v<T, int> || std::is_same_v<T, float>)
			{
				const double d = static_cast<double>(v);
				if (d == static_cast<double>(static_cast<long long>(d)))
					return static_cast<long long>(d);
				return d;
			}
			else if constexpr (std::is_same_v<T, std::string>) return v;
			else if constexpr (std::is_same_v<T, lynx::Actor*>) return v ? Json(v->object_id_) : Json(nullptr);
			else return Json(nullptr);
		}, a);
	}


	// =========================================================================
	// Runner
	// =========================================================================

	class Runner
	{
	public:
		enum class State { Idle, Line, Choice };

		struct VisibleChoice
		{
			std::string text;
			int index;
		};

		bool Start(const std::string& path, lynx::Actor* speaker, lynx::Actor* listener)
		{
			if (!lynx::fs::Exists(path))
			{
				std::cout << "[Dialogue] not found : " << path << "\n";
				return false;
			}
			const auto data = lynx::fs::ReadBinary(path);
			dialogue::Asset asset;
			std::string error;
			if (!dialogue::Parse(std::string(data.begin(), data.end()), asset, error))
			{
				std::cout << "[Dialogue] " << path << " : " << error << "\n";
				return false;
			}

			if (state_ != State::Idle)
				Stop();

			asset_ = std::move(asset);
			path_ = path;
			speaker_ = speaker ? speaker->GetEntity() : 0xFFFFFFFFu;
			listener_ = listener ? listener->GetEntity() : 0xFFFFFFFFu;
			default_speaker_ = speaker ? DefaultSpeakerName(speaker) : std::string();

			lynx::interfaces::Broadcast("DialogueEvents", "OnDialogueStarted", { path_ });
			Enter(asset_.start);
			return true;
		}

		void Continue()
		{
			if (state_ == State::Line)
				Enter(current_ ? current_->next : std::string());
		}

		void Choose(int visible_index)
		{
			if (state_ != State::Choice || !current_ || visible_index < 0 ||
			    visible_index >= static_cast<int>(choices_.size()))
				return;
			const int index = choices_[visible_index].index;
			Enter(current_->choices[index].next);
		}

		void Stop()
		{
			if (state_ == State::Idle)
				return;
			End();
		}

		bool IsActive() const { return state_ != State::Idle; }
		State GetState() const { return state_; }
		const std::string& Speaker() const { return speaker_name_; }
		const std::string& Text() const { return text_; }
		const std::vector<VisibleChoice>& Choices() const { return choices_; }
		uint64_t Revision() const { return revision_; }

		Json Current() const
		{
			Json j = Json::object();
			j["active"] = IsActive();
			j["state"] = state_ == State::Line ? "line" : state_ == State::Choice ? "choice" : "idle";
			j["dialogue"] = path_;
			j["speaker"] = speaker_name_;
			j["text"] = text_;
			Json choices = Json::array();
			for (const VisibleChoice& c : choices_)
				choices.push_back(c.text);
			j["choices"] = choices;
			return j;
		}

		lynx::Actor* SpeakerActor() const { return lynx::Engine::Get() && lynx::Engine::Get()->GetCurrentLevel() ? FindEntity(speaker_) : nullptr; }

	private:
		static lynx::Actor* FindEntity(uint32_t entity)
		{
			if (entity == 0xFFFFFFFFu)
				return nullptr;
			for (lynx::Actor* a : lynx::Engine::Get()->GetCurrentLevel()->GetActors())
				if (a && a->GetEntity() == entity)
					return a;
			return nullptr;
		}

		static std::string DefaultSpeakerName(lynx::Actor* actor)
		{
			const auto& props = actor->GetProperties();
			const auto it = props.find("speaker_name");
			if (it != props.end())
				if (auto* s = std::get_if<std::string*>(&it->second.property_member); s && *s && !(*s)->empty())
					return **s;
			return actor->object_id_;
		}

		void Enter(const std::string& id)
		{
			// Non-interactive nodes in a row (a loop is cut).
			std::string next = id;
			for (int guard = 0; guard < 512; ++guard)
			{
				const dialogue::Node* node = next.empty() ? nullptr : asset_.Find(next);
				if (!node || node->type == "end")
				{
					End();
					return;
				}
				current_ = node;

				if (node->type == "line" || node->type == "choice")
				{
					speaker_name_ = story::Format(node->speaker.empty() ? default_speaker_ : node->speaker);
					text_ = story::Format(node->text);
					choices_.clear();

					if (node->type == "choice")
					{
						for (int i = 0; i < static_cast<int>(node->choices.size()); ++i)
							if (Condition(node->choices[i].condition))
								choices_.push_back({ story::Format(node->choices[i].text), i });
						// No choice left : the dialogue goes on as if it were a line.
						if (choices_.empty())
						{
							next = node->next;
							if (text_.empty())
								continue;
							state_ = State::Line;
						}
						else
						{
							state_ = State::Choice;
						}
					}
					else
					{
						state_ = State::Line;
					}

					++revision_;
					lynx::interfaces::Broadcast("DialogueEvents", "OnDialogueLine", { speaker_name_, text_ });
					return;
				}

				if (node->type == "branch")
				{
					next = Condition(node->condition) ? node->next : node->else_next;
					continue;
				}

				if (node->type == "set")
				{
					const Json value = Json::parse(node->value_json, nullptr, false);
					if (node->op == "add")
						story::Add(node->variable, value.is_number() ? value.get<double>() : 1.0);
					else if (node->op == "toggle")
						story::Set(node->variable, !Truthy(story::Get(node->variable)));
					else
						story::Set(node->variable, value.is_discarded() ? Json(node->value_json) : value);
					next = node->next;
					continue;
				}

				if (node->type == "event")
				{
					if (!node->event.empty())
						lynx::interfaces::Broadcast("DialogueEvents", "OnDialogueEvent", { node->event, path_ });
					if (!node->script.empty())
						lynx::ExecuteScript(node->script.c_str(), ("<dialogue " + path_ + ">").c_str());
					next = node->next;
					continue;
				}

				if (node->type == "quest")
				{
					if (node->action == "complete")
						story::SetQuestState(node->quest, "completed");
					else if (node->action == "fail")
						story::SetQuestState(node->quest, "failed");
					else if (node->action == "objective")
						story::CompleteObjective(node->quest, node->objective);
					else
						story::SetQuestState(node->quest, "active");
					next = node->next;
					continue;
				}

				// Unknown type : skipped.
				next = node->next;
			}
			std::cout << "[Dialogue] " << path_ << " : too many nodes in a row (loop ?), stopped\n";
			End();
		}

		void End()
		{
			const std::string path = path_;
			state_ = State::Idle;
			current_ = nullptr;
			choices_.clear();
			text_.clear();
			speaker_name_.clear();
			++revision_;
			lynx::interfaces::Broadcast("DialogueEvents", "OnDialogueEnded", { path });
		}

		dialogue::Asset asset_;
		std::string path_;
		const dialogue::Node* current_ = nullptr;
		State state_ = State::Idle;
		std::string speaker_name_;
		std::string default_speaker_;
		std::string text_;
		std::vector<VisibleChoice> choices_;
		uint32_t speaker_ = 0xFFFFFFFFu;
		uint32_t listener_ = 0xFFFFFFFFu;
		uint64_t revision_ = 0;
	};

	Runner g_runner;


	// =========================================================================
	// Default dialogue box (widgets)
	// =========================================================================

	class Box
	{
	public:
		bool enabled = true;

		void Update()
		{
			const bool want = enabled && g_runner.IsActive();

			if (!want)
			{
				Hide();
				return;
			}

			if (!Alive())
				Create();
			if (!Alive())
				return;

			if (shown_revision_ != g_runner.Revision())
				Fill();
		}

		void Forget()
		{
			widget_ = nullptr;
			content_ = nullptr;
		}

		void Hide()
		{
			if (Alive())
				lynx::DestroyWidget(widget_);
			Forget();
		}

	private:
		bool Alive() const
		{
			if (!widget_)
				return false;
			const auto all = lynx::ui::GetAllWidgets();
			return std::find(all.begin(), all.end(), widget_) != all.end();
		}

		void Create()
		{
			auto canvas = std::make_unique<lynx::CanvasPanel>();
			auto border = std::make_unique<lynx::Border>();
			border->brush_color = { 0.04f, 0.04f, 0.07f, 0.88f };
			border->padding = { 28.f, 18.f, 28.f, 18.f };
			auto box = std::make_unique<lynx::VerticalBox>();
			content_ = box.get();
			border->AddChild(std::move(box));

			lynx::Border* border_raw = border.get();
			if (auto* slot = canvas->AddChildToCanvas(std::move(border)))
			{
				// Bottom of the screen, full width with margins, 300 high.
				slot->anchor_min = { 0.08f, 1.f };
				slot->anchor_max = { 0.92f, 1.f };
				slot->offsets = { 0.f, -330.f, 0.f, 300.f };
			}
			(void)border_raw;

			auto widget = std::make_unique<lynx::UserWidget>();
			widget->SetRoot(std::move(canvas));
			widget_ = lynx::ui::AdoptWidget(std::move(widget), nullptr);
			if (widget_)
				widget_->AddToViewport(100);
			shown_revision_ = ~0ull;
		}

		static std::vector<std::string> Wrap(const std::string& text, size_t width)
		{
			std::vector<std::string> lines;
			std::string line;
			size_t start = 0;
			while (start <= text.size())
			{
				size_t end = text.find_first_of(" \n", start);
				const std::string word = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
				if (!line.empty() && line.size() + 1 + word.size() > width)
				{
					lines.push_back(line);
					line.clear();
				}
				line += (line.empty() ? "" : " ") + word;
				if (end == std::string::npos)
					break;
				if (text[end] == '\n')
				{
					lines.push_back(line);
					line.clear();
				}
				start = end + 1;
			}
			if (!line.empty())
				lines.push_back(line);
			return lines;
		}

		void Fill()
		{
			shown_revision_ = g_runner.Revision();
			content_->ClearChildren();

			if (!g_runner.Speaker().empty())
			{
				auto* name = content_->AddNew<lynx::TextBlock>();
				name->text = g_runner.Speaker();
				name->font_size = 30.f;
				name->color = { 1.f, 0.82f, 0.35f, 1.f };
			}

			for (const std::string& line : Wrap(g_runner.Text(), 70))
			{
				auto* text = content_->AddNew<lynx::TextBlock>();
				text->text = line;
				text->font_size = 28.f;
			}

			auto* buttons = content_->AddNew<lynx::HorizontalBox>();
			if (auto* slot = buttons ? buttons->GetSlotAs<lynx::BoxSlot>() : nullptr)
				slot->padding = { 0.f, 14.f, 0.f, 0.f };

			if (g_runner.GetState() == Runner::State::Choice)
			{
				const auto& choices = g_runner.Choices();
				for (int i = 0; i < static_cast<int>(choices.size()); ++i)
				{
					auto* button = buttons->AddNew<lynx::Button>();
					button->text = std::to_string(i + 1) + ". " + choices[i].text;
					button->font_size = 24.f;
					button->OnClicked.Subscribe([i]() { g_runner.Choose(i); });
					if (auto* slot = button->GetSlotAs<lynx::BoxSlot>())
						slot->padding = { 0.f, 0.f, 12.f, 0.f };
				}
			}
			else
			{
				auto* button = buttons->AddNew<lynx::Button>();
				button->text = "Continue";
				button->font_size = 24.f;
				button->OnClicked.Subscribe([]() { g_runner.Continue(); });
			}

			if (widget_)
				widget_->Invalidate();
		}

		lynx::UserWidget* widget_ = nullptr;
		lynx::VerticalBox* content_ = nullptr;
		uint64_t shown_revision_ = ~0ull;
	};

	Box g_box;

	// Keyboard / gamepad : action "DialogueNext" (input.json), and the
	// number keys for the choices are left to the game.
	std::unique_ptr<lynx::InputAction> g_next_action;
}


// =============================================================================
// Actors
// =============================================================================

/**
 * A character you can talk to : Interactable (lynx::Interact(npc, player), JS
 * npc.send("Interactable", "Interact", player)) or Talk().
 */
class DialogueNPC : public lynx::Actor
{
public:
	/** .dialogue in assets/. */
	std::string dialogue;
	/** Name shown for the lines without a speaker. */
	std::string speaker_name;

	DialogueNPC()
	{
		HPROPERTY(dialogue, lynx::Exposed);
		HPROPERTY(speaker_name, lynx::Exposed);
		HFUNCTION(Talk);

		BindInterfaceFunction("Interactable", "Interact", [this](const lynx::InterfaceArgs& args) -> lynx::InterfaceArg
		{
			return g_runner.Start(dialogue, this, lynx::InterfaceArgActor(args, 0));
		});
		BindInterfaceFunction("Interactable", "CanInteract", [this](const lynx::InterfaceArgs&) -> lynx::InterfaceArg
		{
			return !dialogue.empty() && !g_runner.IsActive();
		});
	}

	void Init() override
	{
		Actor::Init();
		if (!lynx::Engine::IsReleaseMode())
			AddComponent<lynx::EditorIconComponent>(lynx::EngineActorIcon::Actor);
	}

	bool Talk()
	{
		return g_runner.Start(dialogue, this, nullptr);
	}
};


/** A zone : the dialogue starts when a possessed actor enters it. */
class DialogueTrigger : public lynx::Actor
{
public:
	std::string dialogue;
	std::string speaker_name;
	lynx::vec2 size{ 4.f, 4.f };
	/** Only the first time (per game). */
	bool once = true;

	DialogueTrigger()
	{
		HPROPERTY(dialogue, lynx::Exposed);
		HPROPERTY(speaker_name, lynx::Exposed);
		HPROPERTY(size, lynx::Exposed);
		HPROPERTY(once, lynx::Exposed);
	}

	void Init() override
	{
		Actor::Init();
		collider_ = &AddComponent<lynx::ColliderComponent>();
		collider_->trigger = true;
		collider_->movable = false;
		Update(0.0);
	}

	void Update(double dt) override
	{
		Actor::Update(dt);
		if (collider_)
		{
			collider_->size = size;
			collider_->debug_draw = !playing_ && !lynx::Engine::IsReleaseMode();
		}
	}

	void StartGame() override { Actor::StartGame(); playing_ = true; done_ = false; Update(0.0); }
	void EndGame() override { Actor::EndGame(); playing_ = false; Update(0.0); }

	void OnBeginOverlap(lynx::Actor* other) override
	{
		if (!other || !other->IsPossessed() || (once && done_) || g_runner.IsActive())
			return;
		done_ = g_runner.Start(dialogue, this, other);
	}

private:
	lynx::ColliderComponent* collider_ = nullptr;
	bool playing_ = false;
	bool done_ = false;
};


// =============================================================================
// Plugin
// =============================================================================

LYNX_PLUGIN(Dialogue)

LYNX_LINK_MODULE(
	LYNX_MODULE_REGISTER(DialogueNPC);
	LYNX_MODULE_REGISTER(DialogueTrigger);
)

namespace
{
	std::string ArgString(const lynx::InterfaceArgs& args, size_t i)
	{
		if (i >= args.size())
			return {};
		if (const auto* s = std::get_if<std::string>(&args[i]))
			return *s;
		return ArgToJson(args[i]).dump();
	}

	void RegisterScripting()
	{
		using lynx::InterfaceArg;
		using lynx::InterfaceArgs;

		// ---- Dialogue ----
		lynx::RegisterScriptFunction("Dialogue", "start", [](const InterfaceArgs& a) -> InterfaceArg
		{
			return g_runner.Start(ArgString(a, 0), lynx::InterfaceArgActor(a, 1), lynx::InterfaceArgActor(a, 2));
		});
		lynx::RegisterScriptFunction("Dialogue", "next", [](const InterfaceArgs&) -> InterfaceArg { g_runner.Continue(); return {}; });
		lynx::RegisterScriptFunction("Dialogue", "choose", [](const InterfaceArgs& a) -> InterfaceArg
		{
			g_runner.Choose(lynx::InterfaceArgInt(a, 0, -1));
			return {};
		});
		lynx::RegisterScriptFunction("Dialogue", "stop", [](const InterfaceArgs&) -> InterfaceArg { g_runner.Stop(); return {}; });
		lynx::RegisterScriptFunction("Dialogue", "isActive", [](const InterfaceArgs&) -> InterfaceArg { return g_runner.IsActive(); });
		lynx::RegisterScriptFunction("Dialogue", "_current", [](const InterfaceArgs&) -> InterfaceArg { return g_runner.Current().dump(); });
		lynx::RegisterScriptFunction("Dialogue", "setDefaultBox", [](const InterfaceArgs& a) -> InterfaceArg
		{
			g_box.enabled = lynx::InterfaceArgBool(a, 0, true);
			return {};
		});

		// ---- Story ----
		lynx::RegisterScriptFunction("Story", "get", [](const InterfaceArgs& a) -> InterfaceArg { return JsonToArg(story::Get(ArgString(a, 0))); });
		lynx::RegisterScriptFunction("Story", "has", [](const InterfaceArgs& a) -> InterfaceArg { return story::Has(ArgString(a, 0)); });
		lynx::RegisterScriptFunction("Story", "set", [](const InterfaceArgs& a) -> InterfaceArg
		{
			story::Set(ArgString(a, 0), a.size() > 1 ? ArgToJson(a[1]) : Json(true));
			return {};
		});
		lynx::RegisterScriptFunction("Story", "add", [](const InterfaceArgs& a) -> InterfaceArg
		{
			story::Add(ArgString(a, 0), a.size() > 1 ? lynx::InterfaceArgFloat(a, 1, 1.f) : 1.f);
			return JsonToArg(story::Get(ArgString(a, 0)));
		});
		lynx::RegisterScriptFunction("Story", "reset", [](const InterfaceArgs&) -> InterfaceArg { story::Reset(); return {}; });
		lynx::RegisterScriptFunction("Story", "save", [](const InterfaceArgs& a) -> InterfaceArg { return story::Save(a.empty() ? "default" : ArgString(a, 0)); });
		lynx::RegisterScriptFunction("Story", "load", [](const InterfaceArgs& a) -> InterfaceArg { return story::Load(a.empty() ? "default" : ArgString(a, 0)); });
		lynx::RegisterScriptFunction("Story", "_all", [](const InterfaceArgs&) -> InterfaceArg { return story::ToJson().dump(); });

		// ---- Quest ----
		lynx::RegisterScriptFunction("Quest", "start", [](const InterfaceArgs& a) -> InterfaceArg { story::SetQuestState(ArgString(a, 0), "active"); return {}; });
		lynx::RegisterScriptFunction("Quest", "complete", [](const InterfaceArgs& a) -> InterfaceArg { story::SetQuestState(ArgString(a, 0), "completed"); return {}; });
		lynx::RegisterScriptFunction("Quest", "fail", [](const InterfaceArgs& a) -> InterfaceArg { story::SetQuestState(ArgString(a, 0), "failed"); return {}; });
		lynx::RegisterScriptFunction("Quest", "objective", [](const InterfaceArgs& a) -> InterfaceArg
		{
			story::CompleteObjective(ArgString(a, 0), ArgString(a, 1));
			return {};
		});
		lynx::RegisterScriptFunction("Quest", "state", [](const InterfaceArgs& a) -> InterfaceArg
		{
			const story::Quest* q = story::FindQuest(ArgString(a, 0));
			return std::string(q ? q->state : "inactive");
		});

		lynx::RegisterScriptPrelude("<Dialogue plugin>",
			"Dialogue.current = function () { return JSON.parse(Dialogue._current()); };\n"
			"Story.all = function () { return JSON.parse(Story._all()).variables; };\n"
			"Quest.list = function () { return JSON.parse(Story._all()).quests; };\n"
			"Quest.get = function (id) { return Quest.list().find(q => q.id === id) || null; };\n");
	}
}

LYNX_PLUGIN_STARTUP()
{
	lynx::DefineInterface("DialogueEvents",
		{ "OnDialogueStarted", "OnDialogueLine", "OnDialogueEvent", "OnDialogueEnded" });
	lynx::DefineInterface("StoryEvents", { "OnStoryVariableChanged", "OnQuestChanged" });

	RegisterScripting();
	story::Reset();
}

LYNX_PLUGIN_SHUTDOWN()
{
	g_box.Forget();
	g_next_action.reset();
}

LYNX_GAME_EXPORT void LynxGame_OnGameStart()
{
	story::Reset();
}

LYNX_GAME_EXPORT void LynxGame_OnGameEnd()
{
	g_runner.Stop();
	// The engine deletes the widgets of the game.
	g_box.Forget();
}

LYNX_PLUGIN_TICK(dt, playing)
{
	(void)dt;
	if (!playing)
	{
		g_box.Forget();
		return;
	}

	if (!g_next_action)
		g_next_action = std::make_unique<lynx::InputAction>("DialogueNext");
	if (g_runner.GetState() == Runner::State::Line && g_next_action->IsPressed())
		g_runner.Continue();

	g_box.Update();
}
