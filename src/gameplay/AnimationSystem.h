#pragma once

#include "../core/Filesystem.h"
#include "../core/RessourceManager.h"
#include "../core/Common.h"

#include <functional>
#include <vector>
#include <utility>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace lynx
{
  // ---------------------------------------------------------------------
  // playable : interface commune à tout ce que l'anim_manager peut jouer
  // (animation, blend_space, ...).
  // ---------------------------------------------------------------------
  struct LYNX_API playable
  {
   virtual ~playable() = default;

   // Rend la ressource visible (matériau du sprite, etc.)
   virtual void play() = 0;

   // Recommence depuis le début
   virtual void restart() = 0;

   virtual void update(double dt) = 0;

   virtual bool is_finished() const = 0;

   // Variable externe (ex : vitesse) fournie par le manager.
   // Ignorée par défaut ; utilisée par les blend_spaces.
   virtual void set_value(float) {}
  };

  // ---------------------------------------------------------------------
  // animation
  // ---------------------------------------------------------------------
  struct LYNX_API animation : playable
  {
   animation(
    uint32_t sprite_id,
    const char* texture,
    int frame_count,
    bool loop = true,
    float frame_time = 0.2f
   );

   void play() override;

   // Recommence l'animation depuis la première frame
   void restart() override;

   void set_loop(bool loop);

   // Ajoute un événement sur une frame.
   //
   // Les frames sont numérotées à partir de 1 :
   //   1 = première frame
   //   2 = deuxième frame
   //   etc.
   //
   // Plusieurs callbacks peuvent être ajoutés à la même frame.
   void add_event(int frame, std::function<void()> callback);

   // Supprime tous les événements de l'animation.
   void clear_events();

   // Supprime tous les événements d'une frame.
   void clear_events(int frame);

   void on_finished(std::function<void()> callback);

   bool is_finished() const override { return finished_; }

   // Frame courante, a partir de 0.
   int current_frame() const { return current_frame_; }

   int frame_count() const { return frame_count_; }

   void update(double dt) override;

   // time of one image
   float anim_time = 0.2f;

  private:

   struct frame_event
   {
    int frame;
    std::function<void()> callback;
   };

   void trigger_events(int frame);

   uint32_t sprite_;
   uint32_t hrl_texture_;
   uint32_t material_;

   int frame_count_;
   int current_frame_ = 0;

   double anim_clock_ = 0.0;

   bool loop_ = true;
   bool finished_ = false;

   // Vrai tant que les evenements de la frame 1 n'ont pas ete declenches
   // depuis le dernier restart() (l'index 0 n'est jamais atteint via ++).
   bool pending_start_events_ = true;

   std::function<void()> finished_callback_;

   std::vector<frame_event> events_;
  };

  // ---------------------------------------------------------------------
  // blend_space
  // ---------------------------------------------------------------------
  struct LYNX_API blend_space : playable
  {
   struct sample
   {
    float position;
    animation* anim;
   };

   void add(float position, animation& anim);

   void set_value(float value) override { value_ = value; }

   void on_finished(std::function<void()> callback);

   // Rien à faire : update() rend visible l'animation sélectionnée.
   void play() override {}

   void restart() override;

   void update(double dt) override;

   // Terminé quand l'animation actuellement sélectionnée est terminée.
   bool is_finished() const override
   {
    return current_ && current_->is_finished();
   }

  private:
   std::vector<sample> samples_;

   animation* current_ = nullptr;

   float value_ = 0.0f;

   std::function<void()> finished_callback_;
  };

  // ---------------------------------------------------------------------
  // anim_state_options
  // ---------------------------------------------------------------------
  struct LYNX_API anim_state_options
  {
   // false : l'état ne peut pas être interrompu avant sa fin
   // (à réserver aux états qui se terminent, sinon blocage).
   bool interruptible = true;

   // Relance l'état depuis le début à l'entrée.
   bool restart_on_enter = true;

   // État joué automatiquement quand celui-ci est terminé
   // (si aucune transition ne s'est déclenchée). Vide = default_state.
   std::string next_state;

   // Nom du paramètre float envoyé à set_value() de l'état à chaque update
   // (ex : "speed" pour un blend_space). Vide = aucune variable.
   std::string variable;

   std::function<void()> on_enter;
   std::function<void()> on_exit;
  };

  // ---------------------------------------------------------------------
  // anim_manager
  // ---------------------------------------------------------------------
  class LYNX_API anim_manager
  {
  public:
   using condition = std::function<bool(const anim_manager&)>;

   // ---------- Paramètres ----------

   void set_bool(const std::string& name, bool v)   { params_[name] = v ? 1.0f : 0.0f; }
   void set_int(const std::string& name, int v)     { params_[name] = static_cast<float>(v); }
   void set_float(const std::string& name, float v) { params_[name] = v; }

   // Un trigger reste actif jusqu'à la fin du prochain update(), puis est effacé.
   void set_trigger(const std::string& name) { triggers_.insert(name); }

   float get_float(const std::string& name) const;
   bool  get_bool(const std::string& name) const { return get_float(name) != 0.0f; }
   int   get_int(const std::string& name) const  { return static_cast<int>(get_float(name)); }
   bool  has_trigger(const std::string& name) const { return triggers_.count(name) > 0; }

   // ---------- Conditions prêtes à l'emploi ----------

   condition is_true(std::string name);
   condition is_false(std::string name);
   condition greater(std::string name, float v);
   condition less(std::string name, float v);
   condition triggered(std::string name);

   // ---------- États ----------

   void add_state(
    const std::string& name,
    playable& p,
    anim_state_options opts = {}
   );

   void set_default_state(const std::string& name) { default_state_ = name; }

   // ---------- Transitions ----------

   void add_transition(
    const std::string& from,
    const std::string& to,
    condition cond,
    int priority = 0,
    bool wait_finished = false
   );

   void add_any_transition(
    const std::string& to,
    condition cond,
    int priority = 0
   );

   // Force un état immédiatement (ignore les règles et le verrouillage).
   void force_state(const std::string& name) { switch_to(name); }

   const std::string& current_state() const { return current_name_; }

   bool is_current_finished() const;

   // ---------- Animation control ----------

   // Met en pause toutes les animations gérées par ce manager.
   // L'état et la frame courante sont conservés.
   void StopAllAnimations();

   // Reprend les animations là où elles étaient arrêtées.
   void RestartAllAnimations();

   bool AreAnimationsStopped() const { return animations_stopped_; }

   // ---------- Update ----------

   void update(double dt);

  private:

   struct state
   {
    anim_state_options opts;
    playable* target = nullptr;
   };

   struct transition
   {
    std::string from;
    std::string to;
    condition cond;
    int priority;
    bool wait_finished;
   };

   void switch_to(const std::string& name);

   std::unordered_map<std::string, state> states_;
   std::vector<transition> transitions_;

   std::unordered_map<std::string, float> params_;
   std::unordered_set<std::string> triggers_;

   const state* current_ = nullptr;
   std::string current_name_;
   std::string default_state_;

   bool animations_stopped_ = false;

  public:
   anim_manager() = default;
   anim_manager(const anim_manager&) = delete;
   anim_manager& operator=(const anim_manager&) = delete;
   anim_manager(anim_manager&&) = default;
   anim_manager& operator=(anim_manager&&) = default;
  };
}