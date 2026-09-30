#include "CameraShake.h"

static lynx::CameraShake camera_shake_;

namespace lynx
{
	void SetCameraShake(CameraShake &camera_shake)
	{
		camera_shake_ = camera_shake;
	}

	CameraShake &GetCameraShake()
	{
		return camera_shake_;
	}
}
