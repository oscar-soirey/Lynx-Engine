#pragma once

#include <hrl/hrl.h>
#include <core/Filesystem.h>

#include <functional>
#include <vector>
#include <algorithm>
#include <utility>

namespace lynx
{
  struct animation
  {
   animation(
    uint32_t sprite_id,
    const char* texture,
    int frame_count,
    bool loop = true,
    float frame_time = 0.2f
   )
   : sprite_(sprite_id),
   frame_count_(frame_count),
   anim_time(frame_time),
   loop_(loop)
   {
    auto texture_data = fs::ReadBinary(texture);

    hrl_texture_ = HRL_CreateTexture(
      reinterpret_cast<const char*>(texture_data.data()),
      texture_data.size()
    );

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

   void play()
   {
    HRL_SetMeshMaterial(sprite_, material_);
   }

   // Recommence l'animation depuis la première frame
   void restart()
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

   void set_loop(bool loop)
   {
    loop_ = loop;
   }

   // Ajoute un événement sur une frame.
   //
   // Les frames sont numérotées à partir de 1 :
   //   1 = première frame
   //   2 = deuxième frame
   //   etc.
   //
   // Plusieurs callbacks peuvent être ajoutés à la même frame.
   void add_event(
    int frame,
    std::function<void()> callback
   )
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

   // Supprime tous les événements de l'animation.
   void clear_events()
   {
    events_.clear();
   }

   // Supprime tous les événements d'une frame.
   void clear_events(int frame)
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

   void on_finished(std::function<void()> callback)
   {
    finished_callback_ = std::move(callback);
   }

   bool is_finished() const
   {
    return finished_;
   }

   void update(double dt)
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

   // time of one image
   float anim_time = 0.2f;

  private:

   struct frame_event
   {
    int frame;
    std::function<void()> callback;
   };

   void trigger_events(int frame)
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

   uint32_t sprite_;
   HRL_id hrl_texture_;
   uint32_t material_;

   int frame_count_;
   int current_frame_ = 0;

   double anim_clock_ = 0.0;

   bool loop_ = true;
   bool finished_ = false;

   std::function<void()> finished_callback_;

   std::vector<frame_event> events_;
  };


  struct blend_space
  {
    struct sample
    {
        float position;
        animation anim;
    };

    void add(float position, animation anim)
    {
        samples_.push_back({
            position,
            std::move(anim)
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

    void set_value(float value)
    {
        value_ = value;
    }

    void on_finished(std::function<void()> callback)
    {
        finished_callback_ = std::move(callback);
    }

    void restart()
    {
        for (auto& sample : samples_)
            sample.anim.restart();
    }

    void update(double dt)
    {
        if (samples_.empty())
            return;

        animation* current = nullptr;

        if (value_ <= samples_.front().position)
        {
            current = &samples_.front().anim;
        }
        else if (value_ >= samples_.back().position)
        {
            current = &samples_.back().anim;
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
                        ? &a.anim
                        : &b.anim;

                    break;
                }
            }
        }

        if (!current)
            return;

        current->play();

        bool was_finished = current->is_finished();

        current->update(dt);

        if (!was_finished && current->is_finished())
        {
            if (finished_callback_)
                finished_callback_();
        }
    }

  private:
    std::vector<sample> samples_;

    float value_ = 0.0f;

    std::function<void()> finished_callback_;
  };
}