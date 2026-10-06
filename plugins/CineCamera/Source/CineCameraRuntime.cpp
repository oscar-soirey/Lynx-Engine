// =============================================================================
// Plugin CineCamera : runtime module (editor AND game)
// -----------------------------------------------------------------------------
//   CineCameraActor   camera of a cinematic : focal length, look-at, shake
//   CameraRailActor   moves a camera along a spline (Catmull-Rom)
//   SequencePlayer    plays a .sequence (play on start, loop)
//   Director          blends the view of player 0 between two cameras
//   JS                Cine.cutTo(actor, blend), Cine.release(),
//                     Sequence.play(path, loop), stop, pause, resume, seek,
//                     isPlaying, time ; Sequence.preview / endPreview (editor)
//   Interface         SequenceEvents : OnSequenceEvent(name, sequence),
//                     OnSequenceFinished(sequence)
// =============================================================================

#include <plugins/LynxPlugin.h>
#include <hrl/hrl.h>

#include "SequenceData.h"

#include <json/json.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <sstream>

using Json = nlohmann::json;

namespace
{
	constexpr float kPi = 3.14159265f;

	lynx::Level* CurrentLevel()
	{
		lynx::Engine* engine = lynx::Engine::Get();
		return engine ? engine->GetCurrentLevel() : nullptr;
	}

	lynx::Actor* FindActor(const std::string& id)
	{
		lynx::Level* level = CurrentLevel();
		return level && !id.empty() ? level->GetActorFromID(id.c_str()) : nullptr;
	}

	lynx::Actor* FindEntity(uint32_t entity)
	{
		lynx::Level* level = CurrentLevel();
		if (!level || entity == 0xFFFFFFFFu)
			return nullptr;
		for (lynx::Actor* a : level->GetActors())
			if (a && a->GetEntity() == entity)
				return a;
		return nullptr;
	}

	lynx::PlayerController* DefaultPlayer()
	{
		lynx::Engine* engine = lynx::Engine::Get();
		return engine ? engine->GetDefaultPlayer() : nullptr;
	}

	/** The CameraComponent whose HRL camera is `camera` (nullptr : none). */
	lynx::CameraComponent* CameraFromId(uint32_t camera)
	{
		lynx::Level* level = CurrentLevel();
		if (!level || camera == 0xFFFFFFFFu)
			return nullptr;
		for (lynx::Actor* a : level->GetActors())
			if (auto* c = a ? a->GetComponent<lynx::CameraComponent>() : nullptr)
				if (c->GetCameraId() == camera)
					return c;
		return nullptr;
	}

	float LerpAngle(float a, float b, float k)
	{
		float d = std::fmod(b - a, 360.f);
		if (d > 180.f) d -= 360.f;
		if (d < -180.f) d += 360.f;
		return a + d * k;
	}

	lynx::vec3 Lerp(const lynx::vec3& a, const lynx::vec3& b, float k)
	{
		return { a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k };
	}

	float Smooth(float k)
	{
		k = std::clamp(k, 0.f, 1.f);
		return k * k * (3.f - 2.f * k);
	}

	// Cheap smooth noise (shake) : sum of sines.
	float Noise(float t, float seed)
	{
		return 0.5f * std::sin(t * 1.7f + seed) + 0.3f * std::sin(t * 3.1f + seed * 2.3f) + 0.2f * std::sin(t * 5.3f + seed * 4.1f);
	}


	// =========================================================================
	// Director : blends the view of player 0
	// =========================================================================

	class Director
	{
	public:
		/** Shows `target` (its CameraComponent), blended over `blend` seconds. */
		void CutTo(lynx::Actor* target, float blend)
		{
			lynx::PlayerController* player = DefaultPlayer();
			auto* to = target ? target->GetComponent<lynx::CameraComponent>() : nullptr;
			if (!player || !to)
				return;

			if (!active_)
			{
				saved_view_ = player->GetViewCamera();
				active_ = true;
			}

			// From : what the player sees now (a camera component, or the blend).
			lynx::CameraComponent* from = CameraFromId(player->GetViewCamera());
			if (blending_)
			{
				from_location_ = blend_location_;
				from_rotation_ = blend_rotation_;
				from_fov_ = blend_fov_;
			}
			else if (from && from != to)
			{
				from_location_ = from->GetCurrentLocation();
				from_rotation_ = from->rotation;
				from_fov_ = from->fov;
			}
			else
			{
				blend = 0.f;
			}

			target_ = target->GetEntity();
			if (blend <= 0.f)
			{
				blending_ = false;
				player->SetViewCamera(to->GetCameraId());
				return;
			}

			blending_ = true;
			blend_time_ = 0.f;
			blend_duration_ = blend;
			Tick(0.f);
		}

		/** Static view between two cameras (editor preview). */
		void ShowBlend(lynx::Actor* a, lynx::Actor* b, float k)
		{
			lynx::PlayerController* player = DefaultPlayer();
			auto* from = a ? a->GetComponent<lynx::CameraComponent>() : nullptr;
			auto* to = b ? b->GetComponent<lynx::CameraComponent>() : nullptr;
			if (!player || !to)
				return;
			if (!active_)
			{
				saved_view_ = player->GetViewCamera();
				active_ = true;
			}
			blending_ = false;
			if (!from || k >= 1.f)
			{
				player->SetViewCamera(to->GetCameraId());
				return;
			}
			const float s = Smooth(k);
			Apply(Lerp(from->GetCurrentLocation(), to->GetCurrentLocation(), s),
			      { LerpAngle(from->rotation.x, to->rotation.x, s), LerpAngle(from->rotation.y, to->rotation.y, s),
			        LerpAngle(from->rotation.z, to->rotation.z, s) },
			      from->fov + (to->fov - from->fov) * s);
		}

		/** Back to the view the player had before the first cut. */
		void Release()
		{
			if (!active_)
				return;
			if (lynx::PlayerController* player = DefaultPlayer())
				player->SetViewCamera(saved_view_);
			active_ = false;
			blending_ = false;
		}

		void Forget()
		{
			active_ = false;
			blending_ = false;
			camera_ = 0xFFFFFFFFu;
		}

		void Tick(float dt)
		{
			if (!blending_)
				return;

			lynx::PlayerController* player = DefaultPlayer();
			lynx::Actor* target = FindEntity(target_);
			auto* to = target ? target->GetComponent<lynx::CameraComponent>() : nullptr;
			if (!player || !to)
			{
				blending_ = false;
				return;
			}

			blend_time_ += dt;
			const float k = Smooth(blend_duration_ > 0.f ? blend_time_ / blend_duration_ : 1.f);
			if (k >= 1.f)
			{
				blending_ = false;
				player->SetViewCamera(to->GetCameraId());
				return;
			}

			blend_location_ = Lerp(from_location_, to->GetCurrentLocation(), k);
			blend_rotation_ = { LerpAngle(from_rotation_.x, to->rotation.x, k), LerpAngle(from_rotation_.y, to->rotation.y, k),
			                    LerpAngle(from_rotation_.z, to->rotation.z, k) };
			blend_fov_ = from_fov_ + (to->fov - from_fov_) * k;
			Apply(blend_location_, blend_rotation_, blend_fov_);
		}

	private:
		void Apply(const lynx::vec3& location, const lynx::vec3& rotation, float fov)
		{
			lynx::PlayerController* player = DefaultPlayer();
			if (!player)
				return;
			if (camera_ == 0xFFFFFFFFu || !HRL_IsValidCamera(camera_))
			{
				camera_ = HRL_CreateCamera(lynx::Engine::GetScene(), HRL_PERSPECTIVE);
				HRL_SetCameraNearPlane(camera_, 0.1f);
				HRL_SetCameraFarPlane(camera_, 10000.f);
			}
			HRL_SetCameraPerspectiveFov(camera_, fov);
			HRL_SetCameraLocation(camera_, location.x, location.y, location.z);
			HRL_SetCameraRotation(camera_, rotation.x, rotation.y, rotation.z);
			player->SetViewCamera(camera_);
		}

		bool active_ = false;
		bool blending_ = false;
		uint32_t saved_view_ = 0xFFFFFFFFu;
		uint32_t target_ = 0xFFFFFFFFu;
		uint32_t camera_ = 0xFFFFFFFFu;
		float blend_time_ = 0.f;
		float blend_duration_ = 0.f;
		lynx::vec3 from_location_{ 0.f };
		lynx::vec3 from_rotation_{ 0.f };
		float from_fov_ = 20.f;
		lynx::vec3 blend_location_{ 0.f };
		lynx::vec3 blend_rotation_{ 0.f };
		float blend_fov_ = 20.f;
	};

	Director g_director;


	// =========================================================================
	// Sequence playback
	// =========================================================================

	class Playback
	{
	public:
		bool Play(const std::string& path, bool loop)
		{
			if (!lynx::fs::Exists(path))
			{
				std::cout << "[CineCamera] sequence not found : " << path << "\n";
				return false;
			}
			const auto data = lynx::fs::ReadBinary(path);
			std::string error;
			sequence::Sequence seq;
			if (!sequence::Parse(std::string(data.begin(), data.end()), seq, error))
			{
				std::cout << "[CineCamera] " << path << " : " << error << "\n";
				return false;
			}
			Stop(false);
			seq_ = std::move(seq);
			path_ = path;
			loop_ = loop;
			time_ = 0.f;
			last_time_ = -1.f;
			current_cut_ = -2;
			playing_ = true;
			paused_ = false;
			SaveFade();
			Apply(0.f, true, true);
			return true;
		}

		void Stop(bool notify = true)
		{
			if (!playing_)
				return;
			playing_ = false;
			g_director.Release();
			RestoreFade();
			if (notify)
				lynx::interfaces::Broadcast("SequenceEvents", "OnSequenceFinished", { path_ });
		}

		void Tick(float dt)
		{
			if (!playing_ || paused_)
				return;
			time_ += dt;
			if (time_ >= seq_.length)
			{
				if (loop_)
				{
					Apply(seq_.length, true, true);
					time_ = std::fmod(time_, seq_.length);
					last_time_ = -1.f;
					current_cut_ = -2;
				}
				else
				{
					Apply(seq_.length, true, true);
					Stop();
					return;
				}
			}
			Apply(time_, true, true);
		}

		void Seek(float t)
		{
			if (!playing_)
				return;
			time_ = std::clamp(t, 0.f, seq_.length);
			last_time_ = time_;
			current_cut_ = -2;
			Apply(time_, false, true);
		}

		bool IsPlaying() const { return playing_; }
		bool paused_ = false;
		float Time() const { return time_; }

		// ---- Editor preview (no events, no blend in time : a static view) ----

		void Preview(const std::string& json, float t)
		{
			std::string error;
			sequence::Sequence seq;
			if (!sequence::Parse(json, seq, error))
				return;
			if (!previewing_)
			{
				previewing_ = true;
				SaveFade();
				originals_.clear();
			}
			for (const sequence::Track& track : seq.tracks)
				if (lynx::Actor* a = FindActor(track.actor); a && !originals_.count(track.actor))
					originals_[track.actor] = a->transform;

			ApplyTracks(seq, t);
			ApplyFade(seq, t);

			const int cut = seq.CutAt(t);
			if (cut >= 0)
			{
				const sequence::Cut& c = seq.cuts[cut];
				lynx::Actor* to = FindActor(c.camera);
				lynx::Actor* from = cut > 0 ? FindActor(seq.cuts[cut - 1].camera) : nullptr;
				const float k = c.blend > 0.f ? (t - c.t) / c.blend : 1.f;
				g_director.ShowBlend(from, to, k);
			}
		}

		void EndPreview()
		{
			if (!previewing_)
				return;
			previewing_ = false;
			for (const auto& [id, tr] : originals_)
				if (lynx::Actor* a = FindActor(id))
					a->transform = tr;
			originals_.clear();
			g_director.Release();
			RestoreFade();
		}

		void Forget()
		{
			playing_ = false;
			previewing_ = false;
			originals_.clear();
		}

	private:
		static void ApplyTracks(const sequence::Sequence& seq, float t)
		{
			for (const sequence::Track& track : seq.tracks)
			{
				lynx::Actor* a = FindActor(track.actor);
				sequence::V3 l, r, s;
				if (!a || !sequence::Sample(track, t, l, r, s))
					continue;
				a->transform.location = { l.x, l.y, l.z };
				a->transform.rotation = { r.x, r.y, r.z };
				a->transform.scale = { s.x, s.y, s.z };

			}
		}

		static void ApplyFade(const sequence::Sequence& seq, float t)
		{
			float value = 0.f;
			sequence::V3 color;
			if (seq.FadeAt(t, value, color))
			{
				lynx::postprocess::SetFloat("fadeAmount", value);
				lynx::postprocess::SetColor("fadeColor", color.x, color.y, color.z);
			}
		}

		void Apply(float t, bool events, bool cuts)
		{
			ApplyTracks(seq_, t);
			ApplyFade(seq_, t);

			if (cuts)
			{
				const int cut = seq_.CutAt(t);
				if (cut != current_cut_ && cut >= 0)
				{
					current_cut_ = cut;
					g_director.CutTo(FindActor(seq_.cuts[cut].camera), seq_.cuts[cut].blend);
				}
			}

			if (events)
			{
				for (const sequence::Event& e : seq_.events)
				{
					if (e.t > last_time_ && e.t <= t)
					{
						if (!e.name.empty())
							lynx::interfaces::Broadcast("SequenceEvents", "OnSequenceEvent", { e.name, path_ });
						if (!e.script.empty())
							lynx::ExecuteScript(e.script.c_str(), ("<sequence " + path_ + ">").c_str());
						if (!playing_)
							return;   // the event stopped the sequence
					}
				}
			}
			last_time_ = t;
		}

		void SaveFade()
		{
			lynx::postprocess::GetValue("fadeAmount", saved_fade_);
			lynx::postprocess::GetValue("fadeColor", saved_fade_color_);
		}

		void RestoreFade()
		{
			lynx::postprocess::SetValue("fadeAmount", saved_fade_, 1);
			lynx::postprocess::SetValue("fadeColor", saved_fade_color_, 3);
		}

		sequence::Sequence seq_;
		std::string path_;
		bool loop_ = false;
		bool playing_ = false;
		float time_ = 0.f;
		float last_time_ = -1.f;
		int current_cut_ = -2;
		float saved_fade_[3] = { 0.f, 0.f, 0.f };
		float saved_fade_color_[3] = { 0.f, 0.f, 0.f };

		bool previewing_ = false;
		std::map<std::string, lynx::transform> originals_;
	};

	Playback g_playback;

	// SequencePlayer actors that play on start (first tick of the game).
	std::vector<uint32_t> g_pending_players;
}


// =============================================================================
// CineCameraActor
// =============================================================================

class CineCameraActor : public lynx::Actor
{
public:
	/** Millimeters (sensor 24 mm high) : 18 = wide, 50 = normal, 135 = telephoto. */
	float focal_length = 68.f;
	/** false : `fov` (degrees) is used instead of the focal length. */
	bool use_focal_length = true;
	float fov = 20.f;
	/** Id of an actor to look at ("" : the rotation of the actor). */
	std::string look_at;
	lynx::vec3 look_at_offset{ 0.f, 0.f, 0.f };
	/** Hand-held shake : amount (world units / degrees), speed. */
	float shake_amount = 0.f;
	float shake_speed = 1.5f;
	/** Becomes the view of player 0 at the start of the game. */
	bool auto_activate = false;

	CineCameraActor()
	{
		HPROPERTY(focal_length, lynx::Exposed);
		HPROPERTY(use_focal_length, lynx::Exposed);
		HPROPERTY(fov, lynx::Exposed);
		HPROPERTY(look_at, lynx::Exposed);
		HPROPERTY(look_at_offset, lynx::Exposed);
		HPROPERTY(shake_amount, lynx::Exposed);
		HPROPERTY(shake_speed, lynx::Exposed);
		HPROPERTY(auto_activate, lynx::Exposed);
		HFUNCTION(Activate);
		HFUNCTION(CutTo);
	}

	void Init() override
	{
		Actor::Init();
		camera_ = &AddComponent<lynx::CameraComponent>();
		camera_->offset = { 0.f, 0.f, 0.f };
		camera_->follow_speed = 0.f;
		camera_->use_camera_shake = false;
		camera_->auto_activate = false;
		if (!lynx::Engine::IsReleaseMode())
			AddComponent<lynx::EditorIconComponent>(lynx::EngineActorIcon::Actor);
		Apply(0.f);
	}

	void StartGame() override
	{
		Actor::StartGame();
		playing_ = true;
		time_ = 0.f;
		if (auto_activate)
			Activate();
	}

	void EndGame() override
	{
		Actor::EndGame();
		playing_ = false;
	}

	void Update(double dt) override
	{
		Actor::Update(dt);
		time_ += static_cast<float>(dt);
		Apply(static_cast<float>(dt));

		// Editor : the direction of the camera.
		if (!playing_ && !lynx::Engine::IsReleaseMode() && camera_)
		{
			const float yaw = camera_->rotation.y * kPi / 180.f;
			const float pitch = camera_->rotation.x * kPi / 180.f;
			const lynx::vec3 p = transform.location;
			const float len = 6.f;
			HRL_DrawDebugSegment(lynx::Engine::GetScene(), p.x, p.y, p.z,
			                     p.x + std::cos(yaw) * std::cos(pitch) * len, p.y + std::sin(pitch) * len,
			                     p.z + std::sin(yaw) * std::cos(pitch) * len, 0.95f, 0.75f, 0.2f);
		}
	}

	/** The view of player 0 (no blend). */
	void Activate()
	{
		g_director.CutTo(this, 0.f);
	}

	/** The view of player 0, blended over `blend` seconds. */
	void CutTo(float blend)
	{
		g_director.CutTo(this, blend);
	}

private:
	void Apply(float)
	{
		if (!camera_)
			return;

		camera_->fov = use_focal_length
			? 2.f * std::atan(12.f / std::max(focal_length, 1.f)) * 180.f / kPi
			: fov;

		lynx::vec3 rotation{ transform.rotation.x, -90.f + transform.rotation.y, transform.rotation.z };

		if (lynx::Actor* target = FindActor(look_at); target && target != this)
		{
			const lynx::vec3 to = target->transform.location + look_at_offset;
			const lynx::vec3 d = to - transform.location;
			const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
			if (len > 1e-4f)
			{
				rotation.y = std::atan2(d.z, d.x) * 180.f / kPi;
				rotation.x = std::asin(std::clamp(d.y / len, -1.f, 1.f)) * 180.f / kPi;
			}
		}

		lynx::vec3 offset{ 0.f, 0.f, 0.f };
		if (shake_amount > 0.f && playing_)
		{
			const float t = time_ * shake_speed;
			offset = { Noise(t, 1.3f) * shake_amount, Noise(t, 7.9f) * shake_amount, 0.f };
			rotation.z += Noise(t, 4.2f) * shake_amount;
		}

		camera_->rotation = rotation;
		camera_->offset = offset;
	}

	lynx::CameraComponent* camera_ = nullptr;
	bool playing_ = false;
	float time_ = 0.f;
};


// =============================================================================
// CameraRailActor
// =============================================================================

class CameraRailActor : public lynx::Actor
{
public:
	/** Points of the rail, relative to the actor : "x,y,z; x,y,z; ...". */
	std::string points = "0,0,0; 20,0,0; 40,5,0";
	/** Id of the camera actor moved along the rail (a CineCameraActor). */
	std::string camera;
	/** Seconds from start to end. */
	float duration = 5.f;
	bool play_on_start = false;
	bool loop = false;
	/** 0..1 : position on the rail (editor preview, or set by code). */
	float progress = 0.f;

	CameraRailActor()
	{
		HPROPERTY(points, lynx::Exposed);
		HPROPERTY(camera, lynx::Exposed);
		HPROPERTY(duration, lynx::Exposed);
		HPROPERTY(play_on_start, lynx::Exposed);
		HPROPERTY(loop, lynx::Exposed);
		HPROPERTY(progress, lynx::Exposed);
		HFUNCTION(Play);
		HFUNCTION(Stop);
		HFUNCTION(SetProgress);
	}

	void Init() override
	{
		Actor::Init();
		if (!lynx::Engine::IsReleaseMode())
			AddComponent<lynx::EditorIconComponent>(lynx::EngineActorIcon::Actor);
	}

	void StartGame() override
	{
		Actor::StartGame();
		game_ = true;
		running_ = play_on_start;
		if (running_)
			progress = 0.f;
	}

	void EndGame() override
	{
		Actor::EndGame();
		game_ = false;
		running_ = false;
	}

	void Play() { progress = 0.f; running_ = true; }
	void Stop() { running_ = false; }
	void SetProgress(float p) { progress = std::clamp(p, 0.f, 1.f); MoveCamera(); }

	void Tick(double dt) override
	{
		Actor::Tick(dt);
		if (!running_)
			return;
		progress += static_cast<float>(dt) / std::max(duration, 0.01f);
		if (progress >= 1.f)
		{
			if (loop)
				progress = std::fmod(progress, 1.f);
			else
			{
				progress = 1.f;
				running_ = false;
			}
		}
		MoveCamera();
	}

	void Update(double dt) override
	{
		Actor::Update(dt);
		if (!game_)
		{
			MoveCamera();   // editor preview of `progress`
			if (!lynx::Engine::IsReleaseMode())
				DrawRail();
		}
	}

private:
	std::vector<lynx::vec3> Points() const
	{
		std::vector<lynx::vec3> out;
		std::stringstream all(points);
		std::string item;
		while (std::getline(all, item, ';'))
		{
			std::replace(item.begin(), item.end(), ',', ' ');
			std::stringstream ss(item);
			lynx::vec3 p{ 0.f };
			if (ss >> p.x >> p.y)
			{
				if (!(ss >> p.z))
					p.z = 0.f;
				out.push_back(transform.location + p);
			}
		}
		return out;
	}

	static lynx::vec3 CatmullRom(const lynx::vec3& p0, const lynx::vec3& p1, const lynx::vec3& p2, const lynx::vec3& p3, float t)
	{
		const float t2 = t * t, t3 = t2 * t;
		auto f = [&](float a, float b, float c, float d)
		{
			return 0.5f * ((2.f * b) + (-a + c) * t + (2.f * a - 5.f * b + 4.f * c - d) * t2 + (-a + 3.f * b - 3.f * c + d) * t3);
		};
		return { f(p0.x, p1.x, p2.x, p3.x), f(p0.y, p1.y, p2.y, p3.y), f(p0.z, p1.z, p2.z, p3.z) };
	}

	static lynx::vec3 Evaluate(const std::vector<lynx::vec3>& pts, float u)
	{
		if (pts.empty())
			return lynx::vec3{ 0.f };
		if (pts.size() == 1)
			return pts[0];
		const int segments = static_cast<int>(pts.size()) - 1;
		const float s = std::clamp(u, 0.f, 1.f) * segments;
		const int i = std::min(static_cast<int>(s), segments - 1);
		const float t = s - static_cast<float>(i);
		const auto at = [&](int k) { return pts[std::clamp(k, 0, segments)]; };
		return CatmullRom(at(i - 1), at(i), at(i + 1), at(i + 2), t);
	}

	void MoveCamera()
	{
		lynx::Actor* cam = FindActor(camera);
		if (!cam || cam == this)
			return;
		const lynx::vec3 p = Evaluate(Points(), progress);
		if (cam->transform.location.x != p.x || cam->transform.location.y != p.y || cam->transform.location.z != p.z)
		{
			cam->transform.location = p;

		}
	}

	void DrawRail()
	{
		const auto pts = Points();
		if (pts.size() < 2)
			return;
		const uint32_t scene = lynx::Engine::GetScene();
		lynx::vec3 prev = Evaluate(pts, 0.f);
		for (int i = 1; i <= 64; ++i)
		{
			const lynx::vec3 p = Evaluate(pts, static_cast<float>(i) / 64.f);
			HRL_DrawDebugSegment(scene, prev.x, prev.y, prev.z, p.x, p.y, p.z, 0.3f, 0.8f, 1.f);
			prev = p;
		}
		for (const lynx::vec3& p : pts)
			HRL_DrawDebugPoint(scene, p.x, p.y, p.z, 6.f, 1.f, 1.f, 1.f);
	}

	bool game_ = false;
	bool running_ = false;
};


// =============================================================================
// SequencePlayer
// =============================================================================

class SequencePlayer : public lynx::Actor
{
public:
	/** .sequence in assets/. */
	std::string sequence;
	bool play_on_start = true;
	bool loop = false;

	SequencePlayer()
	{
		HPROPERTY(sequence, lynx::Exposed);
		HPROPERTY(play_on_start, lynx::Exposed);
		HPROPERTY(loop, lynx::Exposed);
		HFUNCTION(Play);
		HFUNCTION(Stop);
	}

	void Init() override
	{
		Actor::Init();
		if (!lynx::Engine::IsReleaseMode())
			AddComponent<lynx::EditorIconComponent>(lynx::EngineActorIcon::Actor);
	}

	void StartGame() override
	{
		Actor::StartGame();
		// First frame of the game : every actor has begun to play.
		if (play_on_start && !sequence.empty())
			g_pending_players.push_back(GetEntity());
	}

	bool Play() { return g_playback.Play(sequence, loop); }
	void Stop() { g_playback.Stop(); }
};


// =============================================================================
// Plugin
// =============================================================================

LYNX_PLUGIN(CineCamera)

LYNX_LINK_MODULE(
	LYNX_MODULE_REGISTER(CineCameraActor);
	LYNX_MODULE_REGISTER(CameraRailActor);
	LYNX_MODULE_REGISTER(SequencePlayer);
)

namespace
{
	std::string ArgString(const lynx::InterfaceArgs& args, size_t i)
	{
		return lynx::InterfaceArgString(args, i);
	}

	lynx::Actor* ArgActor(const lynx::InterfaceArgs& args, size_t i)
	{
		if (lynx::Actor* a = lynx::InterfaceArgActor(args, i))
			return a;
		return FindActor(ArgString(args, i));
	}

	void RegisterScripting()
	{
		using lynx::InterfaceArg;
		using lynx::InterfaceArgs;

		lynx::RegisterScriptFunction("Cine", "cutTo", [](const InterfaceArgs& a) -> InterfaceArg
		{
			g_director.CutTo(ArgActor(a, 0), lynx::InterfaceArgFloat(a, 1, 0.f));
			return {};
		});
		lynx::RegisterScriptFunction("Cine", "release", [](const InterfaceArgs&) -> InterfaceArg { g_director.Release(); return {}; });

		lynx::RegisterScriptFunction("Sequence", "play", [](const InterfaceArgs& a) -> InterfaceArg
		{
			return g_playback.Play(ArgString(a, 0), lynx::InterfaceArgBool(a, 1, false));
		});
		lynx::RegisterScriptFunction("Sequence", "stop", [](const InterfaceArgs&) -> InterfaceArg { g_playback.Stop(); return {}; });
		lynx::RegisterScriptFunction("Sequence", "pause", [](const InterfaceArgs&) -> InterfaceArg { g_playback.paused_ = true; return {}; });
		lynx::RegisterScriptFunction("Sequence", "resume", [](const InterfaceArgs&) -> InterfaceArg { g_playback.paused_ = false; return {}; });
		lynx::RegisterScriptFunction("Sequence", "seek", [](const InterfaceArgs& a) -> InterfaceArg
		{
			g_playback.Seek(lynx::InterfaceArgFloat(a, 0, 0.f));
			return {};
		});
		lynx::RegisterScriptFunction("Sequence", "isPlaying", [](const InterfaceArgs&) -> InterfaceArg { return g_playback.IsPlaying(); });
		lynx::RegisterScriptFunction("Sequence", "time", [](const InterfaceArgs&) -> InterfaceArg { return g_playback.Time(); });

		// Editor : sequencer preview (JSON of the edited sequence, time).
		lynx::RegisterScriptFunction("Sequence", "preview", [](const InterfaceArgs& a) -> InterfaceArg
		{
			g_playback.Preview(ArgString(a, 0), lynx::InterfaceArgFloat(a, 1, 0.f));
			return {};
		});
		lynx::RegisterScriptFunction("Sequence", "endPreview", [](const InterfaceArgs&) -> InterfaceArg { g_playback.EndPreview(); return {}; });
	}
}

LYNX_PLUGIN_STARTUP()
{
	lynx::DefineInterface("SequenceEvents", { "OnSequenceEvent", "OnSequenceFinished" });
	RegisterScripting();
}

LYNX_PLUGIN_SHUTDOWN()
{
	g_playback.Forget();
	g_director.Forget();
}

LYNX_GAME_EXPORT void LynxGame_OnGameStart()
{
	g_playback.EndPreview();
}

LYNX_GAME_EXPORT void LynxGame_OnGameEnd()
{
	g_playback.Stop(false);
	g_director.Forget();
	g_pending_players.clear();
}

LYNX_GAME_EXPORT void LynxGame_OnLevelLoaded(uint32_t)
{
	// The actors of the old level are gone.
	g_playback.Forget();
	g_director.Forget();
}

LYNX_PLUGIN_TICK(dt, playing)
{
	if (playing)
	{
		if (!g_pending_players.empty())
		{
			const auto pending = g_pending_players;
			g_pending_players.clear();
			for (uint32_t entity : pending)
				if (auto* player = dynamic_cast<SequencePlayer*>(FindEntity(entity)))
					player->Play();
		}
		g_playback.Tick(dt);
	}
	g_director.Tick(dt);
}
