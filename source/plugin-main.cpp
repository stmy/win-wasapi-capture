/*
	win-wasapi-capture
	Copyright (C) 2017  stmy

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <obs-module.h>
#include "win-wasapi-capture.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("win-wasapi-capture", "en-US")

bool obs_module_load(void)
{
	obs_source_info win_wasapi_capture = {0};
	win_wasapi_capture.id = "wasapi_capture";
	win_wasapi_capture.type = OBS_SOURCE_TYPE_INPUT;
	win_wasapi_capture.output_flags = OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE;
	win_wasapi_capture.get_name = wasapi_capture::get_name;
	win_wasapi_capture.create = wasapi_capture::create;
	win_wasapi_capture.destroy = wasapi_capture::destroy;
	win_wasapi_capture.get_defaults = wasapi_capture::get_defaults;
	win_wasapi_capture.get_properties = wasapi_capture::get_properties;
	win_wasapi_capture.update = wasapi_capture::update;

	obs_register_source(&win_wasapi_capture);

	return true;
}
