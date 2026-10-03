#include "AnimationSystem.h"

#include <algorithm>
#include <hrl/hrl.h>

namespace lynx
{
  // =====================================================================
  // animation
  // =====================================================================

  animation::animation(
   uint32_t sprite_id,
   const char* texture,
   int frame_count,
   bool loop,
   float frame_time
  )
  : anim_time(frame_time),
  sprite_(sprite_id),
  frame_count_(frame_count),
  loop_(loop)
  {
   hrl_texture_ = RessourceTex(texture);

   material_ = HRL_CreateMaterial(HRL_SPRITE_SHADER);

   HRL_MaterialSetTexture(
     material_,
     HRL_T_ALBEDO,
     hrl_texture_
   );

   HRL_SetMeshMaterial(sprite_, material_);

   HRL_SetSpriteRegion(
     sprite_,
     0.0f,
     0.0f,
     1.0f / static_cast<float>(frame_count_),
     1.0f
   );
  }

  void animation::play()
  {
   HRL_SetMeshMaterial(sprite_, material_);
  }

  void animation::restart()
  {
   current_frame_ = 0;
   anim_clock_ = 0.0;
   finished_ = false;

   HRL_SetSpriteRegion(
     sprite_,
     0.0f,
     0.0f,
     1.0f / static_cast<float>(frame_count_),
     1.0f
   );
  }

  void animation::set_loop(bool loop)
  {
   loop_ = loop;
  }

  void animation::add_event(int frame, std::function<void()> callback)
  {
   if (frame < 1 || frame > frame_count_)
    return;

   if (!callback)
    return;

   events_.push_back({
    frame,
    std::move(callback)
   });
  }

  void animation::clear_events()
  {
   events_.clear();
  }

  void animation::clear_events(int frame)
  {
   events_.erase(
    std::remove_if(
     events_.begin(),
     events_.end(),
     [frame](const frame_event& event)
     {
      return event.frame == frame;
     }
    ),
    events_.end()
   );
  }

  void animation::on_finished(std::function<void()> callback)
  {
   finished_callback_ = std::move(callback);
  }

  void animation::update(double dt)
  {
   if (finished_)
    return;

   anim_clock_ += dt;

   while (anim_clock_ >= anim_time)
   {
    anim_clock_ -= anim_time;
    current_frame_++;

    // Animation terminée
    if (current_frame_ >= frame_count_)
    {
     if (loop_)
     {
      current_frame_ = 0;
     }
     else
     {
      current_frame_ = frame_count_ - 1;
      finished_ = true;

      trigger_events(current_frame_);

      if (finished_callback_)
       finished_callback_();

      break;
     }
    }

    // On vient d'entrer dans cette frame.
    trigger_events(current_frame_);
   }

   const float frame_width =
     1.0f / static_cast<float>(frame_count_);

   const float min_u =
     static_cast<float>(current_frame_) * frame_width;

   const float max_u =
     min_u + frame_width;

   HRL_SetSpriteRegion(
     sprite_,
     min_u,
     0.0f,
     max_u,
     1.0f
   );
  }

  void animation::trigger_events(int frame)
  {
   // current_frame_ est 0-based alors que l'API
   // utilisateur utilise des numéros de frame 1-based.
   const int event_frame = frame + 1;

   for (auto& event : events_)
   {
    if (event.frame == event_frame && event.callback)
     event.callback();
   }
  }

  // =====================================================================
  // blend_space
  // =====================================================================

  void blend_space::add(float position, animation& anim)
  {
   samples_.push_back({
    position,
    &anim
   });

   std::sort(
    samples_.begin(),
    samples_.end(),
    [](const sample& a, const sample& b)
    {
     return a.position < b.position;
    }
   );
  }

  void blend_space::on_finished(std::function<void()> callback)
  {
   finished_callback_ = std::move(callback);
  }

  void blend_space::restart()
  {
   for (auto& sample : samples_)
    sample.anim->restart();

   current_ = nullptr;
  }

  void blend_space::update(double dt)
  {
   if (samples_.empty())
    return;

   animation* current = nullptr;

   if (value_ <= samples_.front().position)
   {
    current = samples_.front().anim;
   }
   else if (value_ >= samples_.back().position)
   {
    current = samples_.back().anim;
   }
   else
   {
    for (size_t i = 0; i < samples_.size() - 1; ++i)
    {
     auto& a = samples_[i];
     auto& b = samples_[i + 1];

     if (value_ >= a.position && value_ <= b.position)
     {
      float alpha =
       (value_ - a.position) /
       (b.position - a.position);

      current = alpha < 0.5f
       ? a.anim
       : b.anim;

      break;
     }
    }
   }

   if (!current)
    return;

   current_ = current;

   current->play();

   bool was_finished = current->is_finished();

   current->update(dt);

   if (!was_finished && current->is_finished())
   {
    if (finished_callback_)
     finished_callback_();
   }
  }

  // =====================================================================
  // anim_manager
  // =====================================================================

  float anim_manager::get_float(const std::string& name) const
  {
   auto it = params_.find(name);
   return it != params_.end() ? it->second : 0.0f;
  }

  // ---------- Conditions ----------

  anim_manager::condition anim_manager::is_true(std::string name)
  {
   return [name](const anim_manager& m) { return m.get_bool(name); };
  }

  anim_manager::condition anim_manager::is_false(std::string name)
  {
   return [name](const anim_manager& m) { return !m.get_bool(name); };
  }

  anim_manager::condition anim_manager::greater(std::string name, float v)
  {
   return [name, v](const anim_manager& m) { return m.get_float(name) > v; };
  }

  anim_manager::condition anim_manager::less(std::string name, float v)
  {
   return [name, v](const anim_manager& m) { return m.get_float(name) < v; };
  }

  anim_manager::condition anim_manager::triggered(std::string name)
  {
   return [name](const anim_manager& m) { return m.has_trigger(name); };
  }

  // ---------- États ----------

  void anim_manager::add_state(
   const std::string& name,
   playable& p,
   anim_state_options opts
  )
  {
   state& s = states_[name];
   s.opts = std::move(opts);
   s.target = &p;
  }

  // ---------- Transitions ----------

  void anim_manager::add_transition(
   const std::string& from,
   const std::string& to,
   condition cond,
   int priority,
   bool wait_finished
  )
  {
   transitions_.push_back({
    from, to, std::move(cond), priority, wait_finished
   });
  }

  void anim_manager::add_any_transition(
   const std::string& to,
   condition cond,
   int priority
  )
  {
   add_transition("", to, std::move(cond), priority, false);
  }

  bool anim_manager::is_current_finished() const
  {
   return current_ && current_->target->is_finished();
  }

  // ---------- Animation control ----------

  void anim_manager::StopAllAnimations()
  {
   animations_stopped_ = true;
  }

  void anim_manager::RestartAllAnimations()
  {
   animations_stopped_ = false;
  }

  // ---------- Update ----------

  void anim_manager::update(double dt)
  {
   if (animations_stopped_)
    return;

   if (!current_)
   {
    if (!default_state_.empty())
     switch_to(default_state_);

    if (!current_)
    {
     triggers_.clear();
     return;
    }
   }

   const bool finished = is_current_finished();
   const bool locked = !current_->opts.interruptible && !finished;

   if (!locked)
   {
    const transition* best = nullptr;

    for (const auto& t : transitions_)
    {
     if (!t.from.empty() && t.from != current_name_)
      continue;

     // Un état terminé peut être relancé par lui-même (ex : ré-attaquer).
     if (t.to == current_name_ && !finished)
      continue;

     if (t.wait_finished && !finished)
      continue;

     if (t.cond && !t.cond(*this))
      continue;

     if (!best || t.priority > best->priority)
      best = &t;
    }

    if (best)
    {
     switch_to(best->to);
    }
    else if (finished)
    {
     const std::string& next =
       !current_->opts.next_state.empty()
         ? current_->opts.next_state
         : default_state_;

     if (!next.empty() && next != current_name_)
      switch_to(next);
    }
   }

   if (current_)
   {
    // Variable liée à l'état (ex : "speed" -> position du blend_space)
    if (!current_->opts.variable.empty())
     current_->target->set_value(get_float(current_->opts.variable));

    current_->target->update(dt);
   }

   triggers_.clear();
  }

  void anim_manager::switch_to(const std::string& name)
  {
   auto it = states_.find(name);
   if (it == states_.end())
    return;

   if (current_)
   {
    if (current_->opts.on_exit)
     current_->opts.on_exit();

    // Reset automatique d'un état terminé quand on le quitte :
    // il sera prêt à être rejoué.
    if (current_->target->is_finished())
     current_->target->restart();
   }

   current_ = &it->second;
   current_name_ = name;

   // Reset juste avant de jouer. Un état déjà terminé est toujours remis
   // à zéro, même avec restart_on_enter = false.
   if (current_->opts.restart_on_enter || current_->target->is_finished())
    current_->target->restart();

   current_->target->play();

   if (current_->opts.on_enter)
    current_->opts.on_enter();
  }
}