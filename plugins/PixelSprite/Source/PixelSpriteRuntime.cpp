// =============================================================================
// Plugin PixelSprite : runtime module (editor AND game)
// -----------------------------------------------------------------------------
//   - ".lsprite" becomes a texture format of the engine (lynx::RegisterTextureDecoder) :
//     StaticSprite, AnimationSprite, Image widgets, voxel types... accept it.
//     The texture is built in memory (frames side by side), never saved.
//   - PixelSpriteActor : place it, pick a .lsprite, it plays its frames.
//   - C++ : pixelsprite::Apply(component, "sprites/hero.lsprite")
//   - JS  : PixelSprite.apply(actor, "sprites/hero.lsprite"), PixelSprite.info(path)
// =============================================================================

#include <plugins/LynxPlugin.h>

#include "LSprite.h"

#include <json/json.hpp>

#include <iostream>
#include <unordered_map>

namespace
{
	bool DecodeLSprite(const std::vector<std::uint8_t>& file, std::vector<std::uint8_t>& image)
	{
		lsprite::Sprite sprite;
		std::string error;
		if (!lsprite::Parse(std::string(file.begin(), file.end()), sprite, error))
			return false;
		image = lsprite::BuildTga(sprite);
		return !image.empty();
	}

	/** Size and timing of a .lsprite (cached per texture revision). */
	struct Info
	{
		bool valid = false;
		int width = 0, height = 0, frames = 1;
		float fps = 8.f;
		bool loop = true;
		uint32_t revision = ~0u;
	};

	const Info& GetInfo(const std::string& path)
	{
		static std::unordered_map<std::string, Info> cache;
		Info& info = cache[path];
		const uint32_t revision = lynx::GetTextureRevision(path.c_str());
		if (info.revision == revision)
			return info;

		info = Info{};
		info.revision = revision;
		const auto data = lynx::fs::ReadBinary(path);
		lsprite::Sprite sprite;
		std::string error;
		if (!data.empty() && lsprite::Parse(std::string(data.begin(), data.end()), sprite, error))
		{
			info.valid = true;
			info.width = sprite.width;
			info.height = sprite.height;
			info.frames = sprite.FrameCount();
			info.fps = sprite.fps;
			info.loop = sprite.loop;
		}
		return info;
	}
}

namespace pixelsprite
{
	/**
	 * Plays the .lsprite on an AnimationSprite (Single mode : its frames, fps,
	 * loop). `pixels_per_unit` : sprite pixels per voxel (size of the sprite).
	 */
	bool Apply(lynx::AnimationSpriteComponent& sprite, const std::string& path, float pixels_per_unit = 8.f)
	{
		const Info& info = GetInfo(path);
		if (!info.valid)
			return false;
		sprite.SetAnimation(path, info.frames, 1.f / std::max(0.1f, info.fps), info.loop);
		const float ppu = std::max(0.01f, pixels_per_unit);
		sprite.size = { info.width / ppu, info.height / ppu };
		return true;
	}
}


/** A pixel art sprite (.lsprite) in the level : its frames play in the game. */
class PixelSpriteActor : public lynx::Actor
{
public:
	/** .lsprite in assets/. */
	std::string sprite;
	/** Sprite pixels per voxel : 8 -> a 16 x 16 sprite is 2 x 2 voxels. */
	float pixels_per_unit = 8.f;
	bool playing = true;
	bool flip_x = false;
	bool visible = true;

	PixelSpriteActor()
	{
		HPROPERTY(sprite, lynx::Exposed);
		HPROPERTY(pixels_per_unit, lynx::Exposed);
		HPROPERTY(playing, lynx::Exposed);
		HPROPERTY(flip_x, lynx::Exposed);
		HPROPERTY(visible, lynx::Exposed);
	}

	void Init() override
	{
		Actor::Init();
		anim_ = &AddComponent<lynx::AnimationSpriteComponent>();
		if (!lynx::Engine::IsReleaseMode())
			icon_ = &AddComponent<lynx::EditorIconComponent>(lynx::EngineActorIcon::Sprite);
		Update(0.0);
	}

	void Update(double dt) override
	{
		Actor::Update(dt);
		if (!anim_)
			return;

		// The file changed (saved in the editor) or another sprite : applied again.
		const uint32_t revision = sprite.empty() ? 0u : lynx::GetTextureRevision(sprite.c_str());
		if (sprite != applied_ || revision != applied_revision_ || pixels_per_unit != applied_ppu_)
		{
			applied_ = sprite;
			applied_revision_ = revision;
			applied_ppu_ = pixels_per_unit;
			valid_ = !sprite.empty() && pixelsprite::Apply(*anim_, sprite, pixels_per_unit);
			if (valid_ && playing_now_)
				anim_->Play();
		}

		anim_->flip_x = flip_x;
		anim_->visible = visible && valid_;
		if (icon_)
			icon_->show = !valid_ || !visible;

		if (playing_now_ && playing != anim_->IsPlaying() && valid_)
		{
			if (playing)
				anim_->Play();
			else
				anim_->Pause();
		}
	}

	void StartGame() override
	{
		Actor::StartGame();
		playing_now_ = true;
		if (anim_ && valid_ && playing)
			anim_->Play();
	}

	void EndGame() override
	{
		Actor::EndGame();
		playing_now_ = false;
	}

private:
	lynx::AnimationSpriteComponent* anim_ = nullptr;
	lynx::EditorIconComponent* icon_ = nullptr;
	std::string applied_;
	uint32_t applied_revision_ = ~0u;
	float applied_ppu_ = -1.f;
	bool valid_ = false;
	bool playing_now_ = false;
};


LYNX_PLUGIN(PixelSprite)

LYNX_LINK_MODULE(
	LYNX_MODULE_REGISTER(PixelSpriteActor);
)

LYNX_PLUGIN_STARTUP()
{
	lynx::RegisterTextureDecoder(".lsprite", DecodeLSprite, true);

	using lynx::InterfaceArg;
	using lynx::InterfaceArgs;

	auto arg_string = [](const InterfaceArgs& a, size_t i) -> std::string
	{
		if (i < a.size())
			if (const auto* s = std::get_if<std::string>(&a[i]))
				return *s;
		return {};
	};

	// PixelSprite.apply(actor, "sprites/hero.lsprite", pixelsPerUnit = 8) :
	// plays it on the AnimationSprite of the actor (added when missing).
	lynx::RegisterScriptFunction("PixelSprite", "apply", [arg_string](const InterfaceArgs& a) -> InterfaceArg
	{
		lynx::Actor* actor = lynx::InterfaceArgActor(a, 0);
		const std::string path = arg_string(a, 1);
		if (!actor || path.empty())
			return false;
		float ppu = 8.f;
		if (a.size() > 2)
		{
			if (const auto* f = std::get_if<float>(&a[2])) ppu = *f;
			else if (const auto* i = std::get_if<int>(&a[2])) ppu = static_cast<float>(*i);
		}
		auto* anim = actor->GetComponent<lynx::AnimationSpriteComponent>();
		if (!anim)
			anim = &actor->AddComponent<lynx::AnimationSpriteComponent>();
		const bool ok = pixelsprite::Apply(*anim, path, ppu);
		if (ok)
			anim->Play();
		return ok;
	});

	// PixelSprite.info(path) -> { width, height, frames, fps, loop } (JSON text).
	lynx::RegisterScriptFunction("PixelSprite", "_info", [arg_string](const InterfaceArgs& a) -> InterfaceArg
	{
		const Info& info = GetInfo(arg_string(a, 0));
		if (!info.valid)
			return std::string("null");
		nlohmann::json j = { { "width", info.width }, { "height", info.height }, { "frames", info.frames },
		                     { "fps", info.fps }, { "loop", info.loop } };
		return j.dump();
	});
	lynx::RegisterScriptPrelude("<PixelSprite plugin>",
		"PixelSprite.info = function (path) { return JSON.parse(PixelSprite._info(path)); };\n");
}

LYNX_PLUGIN_SHUTDOWN()
{
	lynx::UnregisterTextureDecoder(".lsprite");
}
