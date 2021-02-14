#include "IDigitalInput.h"

void IDigitalInput::Enable()
{
	is_enabled_ = true;
}

void IDigitalInput::Disable()
{
	is_enabled_ = false;
}

bool IDigitalInput::InvertState()
{
	state_ = !state_;
	return state_;
}
