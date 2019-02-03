/*

Copyright (c) 2017-2018, Feral Interactive
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
 * Neither the name of Feral Interactive nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

 */

#define _GNU_SOURCE

#include "gpu-control.h"
#include "logging.h"

// NVIDIA
// Running these commands:
// nvidia-settings -a '[gpu:0]/GPUMemoryTransferRateOffset[3]=1400'
// nvidia-settings -a '[gpu:0]/GPUGraphicsClockOffset[3]=50'

// AMD
// We'll want to set both the following:
// core: device/pp_sclk_od (0%+ additional)
// mem: device/pp_mclk_od (0%+ additional)
// Guide from https://www.maketecheasier.com/overclock-amd-gpu-linux/

/**
 * Get the gpu state
 * Populates the struct with the GPU info on the system
 */
int get_gpu_state(struct GameModeGPUInfo *info)
{
	return 0;
}

/**
 * Set the gpu state based on input parameters
 * Only works when run as root
 */
int set_gpu_state(struct GameModeGPUInfo *info)
{
	return 0;
}
