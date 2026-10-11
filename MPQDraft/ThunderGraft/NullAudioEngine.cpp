/*
	The contents of this file are subject to the Common Development and Distribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at http://www.sun.com/cddl/cddl.html.

	Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
*/

/*
	The audio backend used until a real one is linked in: there is no engine, so the hooks forward everything to Storm and the game plays its music as it would without ThunderGraft.
*/

#include "ThunderGraft.h"

IAudioEngine *CreateAudioEngine()
{
	return NULL;
}
