#pragma once

/**
 * Post process du jeu : le shader de post process par defaut de HRL
 * (HRL_DEFAULT_POST_PROCESS_SHADER), applique au viewport de chaque joueur.
 * Ses reglages (exposition, contraste, bloom, vignette...) sont des uniforms
 * du materiau ; ce module les garde, les applique et les sauvegarde dans
 * assets/postprocess.json (charge par l'editeur ET le jeu livre).
 *
 *   lynx::postprocess::SetFloat("exposure", 0.5f);
 *   lynx::postprocess::SetColor("tintColor", 1.f, 0.9f, 0.8f);
 *   lynx::postprocess::Save();       // editeur : assets/postprocess.json
 *
 * JS : PostProcess.set("bloomStrength", 1.5), PostProcess.get("exposure"),
 *      PostProcess.reset(), PostProcess.params().
 * Editeur : fenetre "Post Process" (Windows).
 */

#include <cstdint>
#include <string>
#include <vector>

#include "Common.h"

namespace lynx::postprocess
{
	enum class ParamType
	{
		Float,    // uniform float
		Toggle,   // uniform int / bool (0 / 1)
		Color,    // uniform vec3
	};

	struct ParamInfo
	{
		const char* name;      // nom de l'uniform
		const char* label;
		const char* group;
		ParamType type;
		float defaults[3];
		float min;
		float max;
		const char* tooltip;
		bool is_bool;          // Toggle : uniform bool (sinon int)
	};

	/** Tous les reglages du shader, dans l'ordre d'affichage. */
	LYNX_API const std::vector<ParamInfo>& GetParams();
	LYNX_API const ParamInfo* FindParam(const std::string& name);

	/**
	 * Cree le materiau et le pose sur le viewport de chaque joueur (et des
	 * joueurs crees plus tard), charge assets/postprocess.json et applique.
	 * Apres Engine::CreateScene. Une seule fois.
	 */
	LYNX_API void Install();

	/** Materiau HRL du post process (0xFFFFFFFF avant Install). */
	LYNX_API uint32_t GetMaterial();

	/** Valeur actuelle (Float / Toggle : out[0]). false : nom inconnu. */
	LYNX_API bool GetValue(const std::string& name, float out[3]);
	LYNX_API float GetFloat(const std::string& name);

	/** Change et applique tout de suite. false : nom inconnu. */
	LYNX_API bool SetValue(const std::string& name, const float* values, int count);
	LYNX_API bool SetFloat(const std::string& name, float value);
	LYNX_API bool SetColor(const std::string& name, float r, float g, float b);

	/** Valeur par defaut d'un reglage ("" : tous). */
	LYNX_API void Reset(const std::string& name = "");

	/** Reglage different de sa valeur par defaut. */
	LYNX_API bool IsModified(const std::string& name);

	/** Renvoie toutes les valeurs au materiau. */
	LYNX_API void Apply();

	/** Lit un fichier (relatif a assets/, lynx::fs). Absent : valeurs par defaut. */
	LYNX_API bool Load(const std::string& path = "postprocess.json");

	/** Editeur : ecrit assets/<path> sur le disque. */
	LYNX_API bool Save(const std::string& path = "postprocess.json");

	/** Change depuis le dernier Load / Save (editeur : "non sauvegarde"). */
	LYNX_API bool IsDirty();
}
