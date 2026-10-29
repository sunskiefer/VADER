/*
 * Core/globals.h
 *
 *  Created on: 17.05.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * ------------------------------------------------------------------------------------------------------------------------
 *  Redistribution and use of the LXR02 Open-Source, hardware driver code, or any derivative works are permitted
 *  provided that the following conditions are met:
 *
 *       - The code may not be sold, nor may it be used in a commercial product or activity.
 *
 *       - Redistributions that are modified from the original source must include the complete
 *         source code, including the source code for all components used by a binary built
 *         from the modified sources. However, as a special exception, the source code distributed
 *         need not include anything that is normally distributed (in either source or binary form)
 *         with the major components (compiler, kernel, and so on) of the operating system on which
 *         the executable runs, unless that component itself accompanies the executable.
 *
 *       - Redistributions must reproduce the above copyright notice, this list of conditions and the
 *         following disclaimer in the documentation and/or other materials provided with the distribution.
 * ------------------------------------------------------------------------------------------------------------------------
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *   USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ------------------------------------------------------------------------------------------------------------------------
 */

/*
 * globals.h
 *
 * Port of original LXR mainboard globals.h. Provides the small set of
 * cross-module symbols that DSP and Sequencer source files expect.
 *
 * Definitions:
 *   - systick_ticks  → defined in Core/Hardware/timebase.c, ticked from the
 *                      SysTick handler at 4kHz, matching the original LXR
 *                      mainboard 0.25ms timebase. Front-panel/UI code should
 *                      use time_sysTick for millisecond timing instead.
 *
 *   - audioOutBuffer / audioOutBuffer2 → defined in AudioCodecManager.c.
 *                      Mixer-side widened output buffers (sample_mx_t domain).
 *                      Main loop fills each
 *                      AUDIO_DMA_FRAMES render slot by calling the mixer in
 *                      canonical OUTPUT_DMA_SIZE sub-blocks. DMA HT/TC ISR
 *                      packs these into the 24-bit dma_buffer/dma_buffer2
 *                      ping-pong halves.
 *
 *                      Differs from original LXR mainboard, which had
 *                      mixer write 16-bit samples directly into a 16-bit
 *                      I2S DMA buffer (no pack step needed). We keep
 *                      24-bit I2S so we get the pack via the ISR. The
 *                      buffer dimension here is [AUDIO_DMA_FRAMES*2] —
 *                      original declared [2] as a placeholder because
 *                      original's mixer wrote into dma_buffer halves
 *                      directly.
 *
 *   - bCurrentSampleValid → defined in AudioCodecManager.c. Cleared (=0)
 *                      by the DMA HT/TC ISR after a half is consumed,
 *                      set to SAMPLE_VALID by the mixer after filling
 *                      audioOutBuffer*. Main loop polls this and calls
 *                      mixer when cleared.
 */

#ifndef GLOBAL_VARS_FILE
#define GLOBAL_VARS_FILE

#include <stdint.h>
#include "config.h"          /* AUDIO_DMA_FRAMES */
#include "cmsis_intrinsics.h"  /* __QADD16, __SSAT, __CLZ — for ported DSP files */
#include "sample_mix.h"

extern volatile uint32_t systick_ticks;
extern sample_mx_t audioOutBuffer  [2][AUDIO_DMA_FRAMES * 2];
extern sample_mx_t audioOutBuffer2 [2][AUDIO_DMA_FRAMES * 2];
extern volatile uint8_t bCurrentSampleValid;

#define SAMPLE_VALID  0xff
#define FILTER_SHAPER -0.9f

#endif
