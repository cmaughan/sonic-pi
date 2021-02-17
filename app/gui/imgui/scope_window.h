#pragma once

#include <api/sonicpi_api.h>

void scope_window_show();
void scope_window_add(const SonicPi::ProcessedAudio& audio);
void scope_window_get_spectrum(float& s1, float& s2, float& s3, float& s4);